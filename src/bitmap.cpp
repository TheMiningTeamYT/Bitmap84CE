#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fatdrvce.h>
#include "bitmap.hpp"
#include <ti/screen.h>
#include <ti/getcsc.h>

extern "C" {
    #include "usb.h"
}

#define inputBufferSize 10*512

/*
Left to do:
Implement scaling with linear interpolation
Check for invalid values in the bitmap header
*/

// Finds the shift required to take a bitmasked value and converted to a standard range (specifically 0-255)
// For example, for a bitmask of 0xFF00, to limit the range of the masked result to 0-255, you need to shift it right by 8
int8_t findBitMaskShift(uint32_t bitmask) {
    int8_t shifts = 0;
    while (bitmask) {
        bitmask >>= 1;
        shifts++;
    }
    return shifts - 5;
}

// Implement scaling later
// Draws a row using the conversion function provided
void displayRGBRow(uint8_t* rowBuffer, int width, unsigned int renderWidth, unsigned int bytesPerPixel, uint16_t* screenPointer, uint16_t(*pixelConvert)(uint8_t*)) {
    int xError = renderWidth;
    unsigned int x = 0;
    while (x < width) {
        while (xError > 0) {
            *screenPointer = pixelConvert(rowBuffer);
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            rowBuffer += bytesPerPixel;
            xError += renderWidth;
            x++;
        }
    }
}

// Implement scaling later
// Draws a row of indexed 8bpp color pixels using the provided palette
void displayIndexed8Row(uint8_t* rowBuffer, int width, unsigned int renderWidth, uint16_t* palette, uint16_t* screenPointer) {
    int xError = renderWidth;
    unsigned int x = 0;
    while (x < width) {
        while (xError > 0) {
            *screenPointer = palette[rowBuffer[x]];
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            xError += renderWidth;
            x++;
        }
    }
}

// Implement scaling later
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

void displayNativeRow(uint8_t* rowBuffer, int width, unsigned int renderWidth, uint16_t* screenPointer) {
    int xError = 0;
    unsigned int x = 0;
    while (x < width) {
        while (xError > 0) {
            *screenPointer = *((uint16_t*)rowBuffer);
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            rowBuffer += 2;
            xError += renderWidth;
            x++;
        }
    }
}

// Takes a bitmap color table and converts it to a BGR 565 palette
void generatePalette(unsigned int colors, uint8_t* colorTable, uint16_t* palette) {
    for (unsigned int i = 0; i < colors; i++) {
        palette[i] = rgb888to565(colorTable);
        colorTable += 4;
    }
}

// Implement scaling later
// Draws a row using the bitmasks in the BITMAPV4HEADER
void displayBitFieldRow(uint8_t* trueRowBuffer, int width, unsigned int renderWidth, unsigned int bytesPerPixel, uint16_t* screenPointer, bitmapInfoHeader* DIBheader) {
    // Could be determined ahead of time
    int8_t redMaskShift = findBitMaskShift(DIBheader->bV4RedMask);
    int8_t greenMaskShift = findBitMaskShift(DIBheader->bV4GreenMask) - 1;
    int8_t blueMaskShift = findBitMaskShift(DIBheader->bV4BlueMask);
    uint32_t* rowBuffer = (uint32_t*) trueRowBuffer;
    int xError = 0;
    unsigned int x = 0;
    while (x < width) {
        uint16_t red;
        uint16_t green;
        uint16_t blue;
        if (redMaskShift <= 0) {
            red = ((*rowBuffer) & DIBheader->bV4RedMask) << abs(redMaskShift + 11);
        } else {
            red = (((*rowBuffer) & DIBheader->bV4RedMask) >> redMaskShift) << 11;
        }
        if (greenMaskShift <= 0) {
            green = ((*rowBuffer) & DIBheader->bV4GreenMask) << abs(greenMaskShift + 5);
        } else {
            green = (((*rowBuffer) & DIBheader->bV4GreenMask) >> greenMaskShift) << 5;
        }
        if (blueMaskShift <= 0) {
            blue = ((*rowBuffer) & DIBheader->bV4BlueMask) << abs(blueMaskShift);
        } else {
            blue = ((*rowBuffer) & DIBheader->bV4BlueMask) >> blueMaskShift;
        }
        while (xError > 0) {
            *screenPointer = red + green + blue;
            screenPointer++;
            xError -= width;
        }
        while (xError <= 0) {
            trueRowBuffer += bytesPerPixel;
            rowBuffer = (uint32_t*) trueRowBuffer;
            xError += renderWidth;
            x++;
        }
    }
}

// Assumes that init_USB has already been callled
bool displayBitmap(const char* path, const char* name) {
    // There are almost certainly more edge cases that need to be checked for

    // Open the bitmap file for reading.
    // If the file fails to open, return.
    fat_file_t* bitmapFile = openFile(path, name, false);
    if (!bitmapFile) {
        return false;
    }

    // Allocate a buffer for reading data from the file
    // We need to do some buffer shennanigans because we can only read a whole number of blocks from the file at a time.
    uint8_t* inputBuffer = new uint8_t[inputBufferSize];

    if (!readFile(bitmapFile, inputBufferSize, inputBuffer)) {
        os_PutStrFull(" !Read failed.!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }

    // Pointer to our current location in the buffer
    uint8_t* inputPointer = inputBuffer;

    // We're going to destroy the input buffer in the future, so if we want to be able to take values from the header later, we need to save it
    // either to a newly allocated block of memory or a variable on the stack.
    bitmapFileHeader fileHeader = *((bitmapFileHeader*) inputPointer);

    // Work around for the fact that we're reading the bfType as an LE int, when really it's 2 chars, one after the other
    if (fileHeader.bfType != 'MB') {
        os_PutStrFull(" !Magic bytes are wrong!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }
    inputPointer += sizeof(bitmapFileHeader);

    // Grab the BITMAPINFOHEADER
    // Same rationale as the bitmap file header
    bitmapInfoHeader DIBheader = *((bitmapInfoHeader*) inputPointer);

    if (DIBheader.biSize < 40) {
        os_PutStrFull(" !DIB header too small!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }
    inputPointer += DIBheader.biSize;

    // Bool for if the header is BITMAPV4HEADER compatible or not.
    // If not, it is assumed to only be BITMAPINFOHEADER compatible.
    // Any header smaller than the BITMAPINFOHEADER is assumed to not be compatible with the BITMAPINFOHEADER and thus will not be supported.
    bool v4Header = DIBheader.biSize >= 108;

    // For now, only BI_BITFIELDS is supported because
    // for this proof of concept, the image is assumed to be in 5-6-5 BGR
    // which is not the default for 16bpp bitmaps
    if (DIBheader.biCompression != BI_RGB && DIBheader.biCompression != BI_BITFIELDS) {
        os_PutStrFull(" !Compression mode wrong!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }

    if (DIBheader.biCompression == BI_BITFIELDS && !v4Header) {
        os_PutStrFull(" !Compression mode or header type wrong!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }

    // How many bytes each pixel takes up
    unsigned int bytesPerPixel = DIBheader.biBitCount/8;

    // How many bytes each row of the bitmap takes up
    unsigned int rowSize = (((DIBheader.biBitCount*DIBheader.biWidth)+31)/32)*4;

    // Buffer for holding a complete row from the image
    uint8_t* rowBuffer;

    // Buffer for holding the palette (unused for now)
    // In the future, a function will be written to convert the 8-8-8-8 RGBA palette of bitmaps into 5-6-5 BGR colors for the display.
    uint16_t* palette = nullptr;

    int8_t displayMode;

    switch (DIBheader.biBitCount) {
        case 1:
        case 2:
        case 4:
        case 8:
            if (DIBheader.biCompression != BI_RGB) {
                os_PutStrFull(" !Unsupported bit depth!");
                delete[] inputBuffer;
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
                displayMode = rgb1555;
            } else if (DIBheader.bV4RedMask == 0xf800 && DIBheader.bV4GreenMask == 0x7e0 && DIBheader.bV4BlueMask == 0x1f) {
                displayMode = native;
            } else {
                displayMode = bitfields;
            }
            break;
        case 24:
        case 32:
            rowBuffer = new uint8_t[rowSize];
            if (DIBheader.biCompression == BI_RGB || (DIBheader.bV4RedMask == 0xFF0000 && DIBheader.bV4GreenMask == 0xFF00 && DIBheader.bV4BlueMask == 0xFF)) {
                displayMode = rgb888;
            } else {
                displayMode = bitfields;
            }
            break;
        default:
            os_PutStrFull(" !Unsupported bit depth!");
            delete[] inputBuffer;
            closeFile(bitmapFile);
            return false;
    }

    // Set input pointer to point to the start of bitmap data
    inputPointer = inputBuffer + fileHeader.bfOffBits;

    // A pointer to the end of the input buffer
    uint8_t* inputBufferEnd = inputBuffer + inputBufferSize;

    // A pointer to our current position in vram
    uint16_t* screenPointer = (DIBheader.biHeight < 0) ? vram : vram + (320*239);
    int rowOffset = (DIBheader.biHeight < 0) ? 320 : -320;

    // Figure out how we need to scale and reposition the image
    unsigned int renderWidth;
    unsigned int renderHeight;
    float xRatio = 320.0f/(float)DIBheader.biWidth;
    float yRatio = 240.0f/(float)abs(DIBheader.biHeight);
    if (xRatio < yRatio) {
        renderWidth = 320;
        renderHeight = ((float)abs(DIBheader.biHeight))*xRatio;
        if (DIBheader.biHeight < 0) {
            screenPointer += ((240-renderHeight)/2)*320;
        } else {
            screenPointer -= ((240-renderHeight)/2)*320;
        }
    } else {
        renderWidth = ((float)DIBheader.biWidth)*yRatio;
        screenPointer += (320-renderWidth)/2;
        renderHeight = 240;
    }
    if (renderWidth > 320) {
        renderWidth = 320;
    }
    if (renderHeight > 240) {
        renderHeight = 240;
    }
    // Clear out screen before writing the final image
    memset(vram, 0, (320*240)*sizeof(uint16_t));

    int yError = 0;
    unsigned int y = 0;

    // Not finished code -- Right now this assumes the image is bottom to top
    while(true) {
        // Incredibly wasteful -- Large images could be read much faster if we didn't read then discard so many rows
        while (yError <= 0) {
            if (y >= abs(DIBheader.biHeight)) {
                goto endOfImage;
            }
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
                if (!readFile(bitmapFile, inputBufferSize, inputBuffer)) {
                    os_PutStrFull(" !Read failed.!");
                    if (palette) {
                        delete[] palette;
                    }
                    delete[] rowBuffer;
                    delete[] inputBuffer;
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
        }
        // Decide how to draw the row
        // If the image is already in 5-6-5 BGR, copy the pixels to vram directly
        // (I'll implement scaling later)
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
                case rgb1555:
                    displayRGBRow(rowBuffer, DIBheader.biWidth, renderWidth, 2, screenPointer, rgb1555to565);
                    break;
                case rgb888:
                    displayRGBRow(rowBuffer, DIBheader.biWidth, renderWidth, bytesPerPixel, screenPointer, rgb888to565);
                    break;
                case bitfields:
                    displayBitFieldRow(rowBuffer, DIBheader.biWidth, renderWidth, bytesPerPixel, screenPointer, &DIBheader);
                    break;
                default:
                    break;
            }

            // Move up 1 row in vram
            screenPointer += rowOffset;
            if (screenPointer < vram || screenPointer + renderWidth > vram + (240*320)) {
                goto endOfImage;
            }
            yError -= abs(DIBheader.biHeight);
        }
    }
    endOfImage:
    // Remember to free that memory!
    if (palette) {
        delete[] palette;
    }
    delete[] rowBuffer;
    delete[] inputBuffer;
    closeFile(bitmapFile);
    return true;
}