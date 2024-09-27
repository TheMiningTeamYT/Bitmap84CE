#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fatdrvce.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include "bitmap.hpp"
#include "common.h"

extern "C" {
    #include "usb.h"
    int32_t abs_long(int32_t x);
    // Draws a row of rgb888 pixels
    // Only used in cases where each pixel is 3 bytes
    void displayRGBRow(uint8_t* rowBuffer, unsigned int width, unsigned int renderWidth, uint16_t* screenPointer);
    // Draws a row of rgba8888 pixels
    // Only used in cases where each pixel is 4 bytes
    void displayRGBARow(uint8_t* rowBuffer, unsigned int width, unsigned int renderWidth, uint16_t* screenPointer);
    // Draws a row of indexed 8bpp color pixels using the provided palette
    void displayIndexed8Row(uint8_t* rowBuffer, int width, unsigned int renderWidth, uint16_t* palette, uint16_t* screenPointer);
    // Draws a row of native pixels
    void displayNativeRow(uint8_t* rowBuffer, int width, unsigned int renderWidth, uint16_t* screenPointer);
}

/*
Left to do:
Check for invalid values in the bitmap header
*/

// Finds the shift required to take a bitmasked value and converted to a standard range (specifically 0-255)
// For example, for a bitmask of 0xFF00, to limit the range of the masked result to 0-255, you need to shift it right by 8
int8_t findBitMaskShift(uint32_t bitmask) {
    int8_t shifts = -5;
    if (!bitmask) {
        return shifts;
    }
    while (bitmask) {
        bitmask >>= 1;
        shifts++;
    }
    return shifts;
}

// Draws a row of indexed color pixels in cases where the bit depth is less than 8
void displayIndexedRow(uint8_t* rowBuffer, int width, unsigned int renderWidth, uint8_t bitsPerPixel, uint16_t* palette, uint16_t* screenPointer) {
    uint8_t bitMask = (1 << bitsPerPixel)-1;
    uint8_t pixelShift = 8 - bitsPerPixel;
    int xError = 0;
    unsigned int x = 0;
    while (x < width) {
        uint8_t index = (((*rowBuffer) >> pixelShift) & bitMask);
        while (xError > 0) {
            *screenPointer = palette[index];
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            if (pixelShift < bitsPerPixel) {
                rowBuffer++;
                pixelShift = 8 - bitsPerPixel;
            } else {
                pixelShift -= bitsPerPixel;
            }
            xError += renderWidth;
            x++;
        }
    }
}

// Takes a bitmap color table and converts it to a BGR 565 palette
void generatePalette(unsigned int colors, uint8_t* colorTable, uint16_t* palette) {
    for (unsigned int i = 0; i < colors; i++) {
        ColorError err;
        palette[i] = rgb888to565(colorTable, &err);
        colorTable += 4;
    }
}

// Implement scaling later
// Draws a row using the bitmasks in the BITMAPV4HEADER
void displayBitFieldRow(uint8_t* trueRowBuffer, int width, unsigned int renderWidth, size_t bytesPerPixel, uint16_t* screenPointer, BitfieldMasks* mask) {
    uint32_t* rowBuffer = reinterpret_cast<uint32_t*>(trueRowBuffer);
    int xError = 0;
    unsigned int x = 0;
    while (x < width) {
        uint16_t red;
        uint16_t green;
        uint16_t blue;
        uint16_t alpha = 0;
        uint16_t alphaMax = 0;
        if (mask->alphaMask != 0) {
            if (mask->alphaMaskShift <= 0) {
                alpha = ((*rowBuffer) & mask->alphaMask) << abs(mask->alphaMaskShift);
                alphaMax = mask->alphaMask << abs(mask->alphaMaskShift);
            } else {
                alpha = ((*rowBuffer) & mask->alphaMask) >> mask->alphaMaskShift;
                alphaMax = mask->alphaMask >> mask->alphaMaskShift;
            }
        }
        if (mask->redMaskShift <= 0) {
            red = ((*rowBuffer) & mask->redMask) << abs(mask->redMaskShift + 11);
        } else {
            red = (((*rowBuffer) & mask->redMask) >> mask->redMaskShift) << 11;
        }
        if (mask->greenMaskShift <= 0) {
            green = ((*rowBuffer) & mask->greenMask) << abs(mask->greenMaskShift + 5);
        } else {
            green = (((*rowBuffer) & mask->greenMask) >> mask->greenMaskShift) << 5;
        }
        if (mask->blueMaskShift <= 0) {
            blue = ((*rowBuffer) & mask->blueMask) << abs(mask->blueMaskShift);
        } else {
            blue = ((*rowBuffer) & mask->blueMask) >> mask->blueMaskShift;
        }
        if (alpha != alphaMax) {
            red = (red * alpha)/alphaMax;
            green = (green * alpha)/alphaMax;
            blue = (blue * alpha)/alphaMax;
        }
        while (xError > 0) {
            *screenPointer = red + green + blue;
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            trueRowBuffer += bytesPerPixel;
            rowBuffer = reinterpret_cast<uint32_t*>(trueRowBuffer);
            xError += renderWidth;
            x++;
        }
    }
}

// Assumes that init_USB has already been callled
bool displayBitmap(const char* path, const char* name) {
    // File handle
    fat_file_t* bitmapFile;
    // Pointer to our current location in the buffer
    uint8_t* inputPointer;
    // Bitmap file header
    bitmapFileHeader fileHeader;
    // Bitmap DIB header
    bitmapInfoHeader DIBheader;
    // Bool for if the header is BITMAPV4HEADER compatible or not.
    // If not, it is assumed to only be BITMAPINFOHEADER compatible.
    // Any header smaller than the BITMAPINFOHEADER is assumed to not be compatible with the BITMAPINFOHEADER and thus will not be supported.
    bool v4Header;
    // How many bytes each pixel takes up
    size_t bytesPerPixel;
    // How many bytes each row of the bitmap takes up
    size_t rowSize;
    // Buffer for holding a complete row from the image
    uint8_t* rowBuffer;
    // Buffer for holding the palette
    uint16_t* palette = nullptr;
    // Display mode of the file
    bppModes displayMode;
    // A pointer to our current position in vram
    uint16_t* screenPointer;
    // How many pixels to offset each row in vram by
    int rowOffset;
    // Dimensions to scale the image to
    unsigned int renderWidth;
    unsigned int renderHeight;
    // Scaling ratios
    float xRatio;
    float yRatio;
    // Used for scaling on the y axis
    int yError = 0;
    unsigned int y = 0;
    // Settings for displayBitFieldRow
    BitfieldMasks mask;


    // There are almost certainly more edge cases that need to be checked for

    // Open the bitmap file for reading.
    // If the file fails to open, return.
    bitmapFile = openFile(path, name, false);
    if (!bitmapFile) {
        return false;
    }
    if (!readFile(bitmapFile, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
        os_PutStrFull(" !Read failed.!");
        closeFile(bitmapFile);
        return false;
    }
    inputPointer = inputBuffer;

    // We're going to destroy the input buffer in the future, so if we want to be able to take values from the header later, we need to save it
    // either to a newly allocated block of memory or a variable on the stack.
    fileHeader = *(reinterpret_cast<bitmapFileHeader*>(inputPointer));

    // Work around for the fact that we're reading the bfType as an LE int, when really it's 2 chars, one after the other
    if (fileHeader.bfType != 'MB') {
        os_PutStrFull(" !Magic bytes are wrong!");
        closeFile(bitmapFile);
        return false;
    }
    inputPointer += sizeof(bitmapFileHeader);

    // Grab the BITMAPINFOHEADER
    // Same rationale as the bitmap file header
    DIBheader = *(reinterpret_cast<bitmapInfoHeader*>(inputPointer));
    if (DIBheader.biSize < 40) {
        os_PutStrFull(" !DIB header too small!");
        closeFile(bitmapFile);
        return false;
    }
    inputPointer += DIBheader.biSize;
    v4Header = DIBheader.biSize >= 108;
    if (DIBheader.biCompression != BI_RGB && DIBheader.biCompression != BI_BITFIELDS) {
        os_PutStrFull(" !Compression mode wrong!");
        closeFile(bitmapFile);
        return false;
    }
    if (DIBheader.biCompression == BI_BITFIELDS && !v4Header) {
        os_PutStrFull(" !Compression mode or header type wrong!");
        closeFile(bitmapFile);
        return false;
    }
    bytesPerPixel = DIBheader.biBitCount/8;

    // This weird math is to account for the fact that each row is padded to be a multiple of 4 bytes long
    rowSize = (((DIBheader.biBitCount*DIBheader.biWidth)+31)/32)*4;

    switch (DIBheader.biBitCount) {
        case 1:
        case 2:
        case 4:
        case 8:
            if (DIBheader.biCompression != BI_RGB) {
                os_PutStrFull(" !Unsupported bit depth!");
                closeFile(bitmapFile);
                return false;
            }
            rowBuffer = new uint8_t[rowSize];
            if (DIBheader.biClrUsed == 0) {
                palette = new uint16_t[1 << DIBheader.biBitCount];
                generatePalette(1 << DIBheader.biBitCount, inputPointer, palette);
            } else {
                palette = new uint16_t[DIBheader.biClrUsed];
                generatePalette(DIBheader.biClrUsed, inputPointer, palette);
            }
            if (DIBheader.biBitCount == 8) { 
                displayMode = indexed8;
            } else {
                displayMode = indexed;
            }
            break;
        case 16:
            rowBuffer = new uint8_t[rowSize];
            if (DIBheader.biCompression == BI_RGB || (DIBheader.bV4RedMask == 0x7c00 && DIBheader.bV4GreenMask == 0x3e0 && DIBheader.bV4BlueMask == 0x1f)) {
                // set display to BGR1555 mode
                *(reinterpret_cast<uint8_t*>(0xE30018)) = (*(reinterpret_cast<uint8_t*>(0xE30018)) & 0xF1) | 0x8;
                displayMode = native;
            } else if (DIBheader.bV4RedMask == 0xf800 && DIBheader.bV4GreenMask == 0x7e0 && DIBheader.bV4BlueMask == 0x1f) {
                displayMode = native;
            } else {
                displayMode = bitfields;
            }
            break;
        case 24:
            rowBuffer = new uint8_t[rowSize];
            if (DIBheader.biCompression == BI_RGB || (DIBheader.bV4RedMask == 0xFF0000 && DIBheader.bV4GreenMask == 0xFF00 && DIBheader.bV4BlueMask == 0xFF)) {
                displayMode = rgb888;
            } else {
                displayMode = bitfields;
            }
            break;
        case 32:
            rowBuffer = new uint8_t[rowSize];
            if (DIBheader.biCompression == BI_RGB || (DIBheader.bV4AlphaMask == 0xFF000000 && DIBheader.bV4RedMask == 0xFF0000 && DIBheader.bV4GreenMask == 0xFF00 && DIBheader.bV4BlueMask == 0xFF)) {
                displayMode = rgba8888;
            } else {
                displayMode = bitfields;
            }
            break;
        default:
            os_PutStrFull(" !Unsupported bit depth!");
            closeFile(bitmapFile);
            return false;
    }

    // Check that rowBuffer actually got allocated
    if (rowBuffer == nullptr) {
        os_PutStrFull(" !Failed to allocate the row buffer!");
        closeFile(bitmapFile);
        return false;
    }

    // If using bitfields mode, initialize the masks for displayBitFieldsRow
    if (displayMode == bitfields) {
        mask.redMask = DIBheader.bV4RedMask;
        mask.redMaskShift = findBitMaskShift(DIBheader.bV4RedMask);
        mask.greenMask = DIBheader.bV4GreenMask;
        mask.greenMaskShift = findBitMaskShift(DIBheader.bV4GreenMask) - 1;
        mask.blueMask = DIBheader.bV4BlueMask;
        mask.blueMaskShift = findBitMaskShift(DIBheader.bV4BlueMask);
        mask.alphaMask = DIBheader.bV4AlphaMask;
        mask.alphaMaskShift = findBitMaskShift(DIBheader.bV4AlphaMask);
    }

    // Set input pointer to point to the start of bitmap data
    inputPointer = inputBuffer + fileHeader.bfOffBits;

    // A pointer to our current position in vram
    screenPointer = (DIBheader.biHeight < 0) ? vram : vram + (320*239);
    rowOffset = (DIBheader.biHeight < 0) ? 320 : -320;

    // Figure out how we need to scale and reposition the image
    if (DIBheader.biWidth == 320 && abs_long(DIBheader.biHeight) == 240) {
        renderWidth = 320;
        renderHeight = 240;
    } else {
        xRatio = 320.0f/static_cast<float>(DIBheader.biWidth);
        yRatio = 240.0f/static_cast<float>(abs_long(DIBheader.biHeight));
        if (xRatio < yRatio) {
            renderWidth = 320;
            renderHeight = static_cast<float>(abs_long(DIBheader.biHeight))*xRatio;
            if (DIBheader.biHeight < 0) {
                screenPointer += ((240-renderHeight)/2)*320;
            } else {
                screenPointer -= ((240-renderHeight)/2)*320;
            }
        } else {
            renderWidth = static_cast<float>(DIBheader.biWidth)*yRatio;
            screenPointer += (320-renderWidth)/2;
            renderHeight = 240;
        }
        if (renderWidth > 320) {
            renderWidth = 320;
        }
        if (renderHeight > 240) {
            renderHeight = 240;
        }
    }
    // Clear out screen before writing the final image
    memset(vram, 0, (320*240)*sizeof(uint16_t));

    while(!os_GetCSC()) {
        // Incredibly wasteful -- Large images could be read much faster if we didn't read then discard so many rows
        // Unfortunately, it seems FATDRVCE doesn't support efficiently seeking forward.
        while (-yError >= renderHeight) {
            // How many bytes are left to copy from the input buffer to the row buffer
            unsigned int bytesRemainingInRow = rowSize;

            // If the end of the row is outside the input buffer, copy what's in the input buffer and load the next chunk into the input buffer
            while (inputPointer + bytesRemainingInRow > inputBufferEnd) {
                bytesRemainingInRow -= inputBufferEnd - inputPointer;
                inputPointer = inputBuffer;
                if (!readFile(bitmapFile, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
                    os_PutStrFull(" !Read failed.!");
                    if (palette) {
                        delete[] palette;
                    }
                    delete[] rowBuffer;
                    closeFile(bitmapFile);
                    return false;
                }
            }

            // Advance the pointer into the input buffer
            inputPointer += bytesRemainingInRow;
            yError += renderHeight;
            y++;
            if (y > abs_long(DIBheader.biHeight)) {
                goto endOfImage;
            }
        }
        {
            // A pointer to our current position on the row buffer
            uint8_t* rowPointer = rowBuffer;

            // How many bytes are left to copy from the input buffer to the row buffer
            unsigned int bytesRemainingInRow = rowSize;

            // If the end of the row is outside the input buffer, copy what's in the input buffer and load the next chunk into the input buffer
            while (inputPointer + bytesRemainingInRow > inputBufferEnd) {
                memcpy(rowPointer, inputPointer, inputBufferEnd - inputPointer);
                rowPointer += inputBufferEnd - inputPointer;
                bytesRemainingInRow -= inputBufferEnd - inputPointer;
                inputPointer = inputBuffer;
                if (!readFile(bitmapFile, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
                    os_PutStrFull(" !Read failed.!");
                    if (palette) {
                        delete[] palette;
                    }
                    delete[] rowBuffer;
                    closeFile(bitmapFile);
                    return false;
                }
            }

            // Copy the rest of the row from the input buffer
            memcpy(rowPointer, inputPointer, bytesRemainingInRow);

            // Advance the pointer into the input buffer
            inputPointer += bytesRemainingInRow;
            yError += renderHeight;
            y++;
            if (y > abs_long(DIBheader.biHeight)) {
                goto endOfImage;
            }
        }

        // Decide how to draw the row
        // If the image is already in 5-6-5 BGR, copy the pixels to vram directly
        // Else, if the image is in RGB mode (it uses the corresponding standard pixel storage mode)
        // draw it using the simpler displayRGBRow function.
        // Else, if it the image is in BITFIELDS mode, and it's not a native image, draw it using the slower but more comprehensive
        // displayBitFieldRow function
        while (yError > 0) {
            switch (displayMode) {
                case indexed:
                    displayIndexedRow(rowBuffer, DIBheader.biWidth, renderWidth, DIBheader.biBitCount, palette, screenPointer);
                    break;
                case indexed8:
                    displayIndexed8Row(rowBuffer, DIBheader.biWidth, renderWidth, palette, screenPointer);
                    break;
                case native:
                    displayNativeRow(rowBuffer, DIBheader.biWidth, renderWidth, screenPointer);
                    break;
                case rgb888:
                    displayRGBRow(rowBuffer, DIBheader.biWidth, renderWidth, screenPointer);
                    break;
                case rgba8888:
                    displayRGBARow(rowBuffer, DIBheader.biWidth, renderWidth, screenPointer);
                    break;
                case bitfields:
                    displayBitFieldRow(rowBuffer, DIBheader.biWidth, renderWidth, bytesPerPixel, screenPointer, &mask);
                    break;
                default:
                    break;
            }

            // Move up 1 row in vram
            screenPointer += rowOffset;
            if (screenPointer < vram || screenPointer + renderWidth > vram + (240*320)) {
                goto endOfImage;
            }
            yError -= abs_long(DIBheader.biHeight);
        }
    }
    endOfImage:
    // Remember to free that memory!
    if (palette) {
        delete[] palette;
    }
    delete[] rowBuffer;
    closeFile(bitmapFile);
    return true;
}