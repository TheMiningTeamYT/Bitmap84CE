#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fatdrvce.h>
#include "bitmap.hpp"
#include <ti/screen.h>

extern "C" {
    #include "usb.h"
}

#define vram ((uint16_t*)0xD40000)
#define inputBufferSize 10*512

/*
Left to do:
Handle indexed colors
Handle images that are smaller/bigger than 320x240 (better than just cropping them)
Handle negative heights
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
void displayRGBrow(uint8_t* rowBuffer, int width, unsigned int bytesPerPixel, uint16_t* screenPointer, uint16_t(*pixelConvert)(uint8_t*)) {
    for (unsigned int i = 0; i < width && i < 320; i++) {
        *screenPointer = rgb888to565(rowBuffer);
        rowBuffer += bytesPerPixel;
        screenPointer++;
    }
}

// Implement scaling later
// Draws a row using the bitmasks in the BITMAPV4HEADER
void displayBitFieldRow(uint8_t* trueRowBuffer, int width, unsigned int bytesPerPixel, uint16_t* screenPointer, bitmapInfoHeader* DIBheader) {
    int8_t redMaskShift = findBitMaskShift(DIBheader->bV4RedMask);
    int8_t greenMaskShift = findBitMaskShift(DIBheader->bV4GreenMask) - 1;
    int8_t blueMaskShift = findBitMaskShift(DIBheader->bV4BlueMask);
    uint32_t* rowBuffer = (uint32_t*) trueRowBuffer;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    for (unsigned int i = 0; i < width; i++) {
        if (redMaskShift <= 0) {
            red = ((*rowBuffer) & DIBheader->bV4RedMask) << (redMaskShift + 11);
        } else {
            red = (((*rowBuffer) & DIBheader->bV4RedMask) >> redMaskShift) << 11;
        }
        if (greenMaskShift <= 0) {
            green = ((*rowBuffer) & DIBheader->bV4GreenMask) << (greenMaskShift + 5);
        } else {
            green = (((*rowBuffer) & DIBheader->bV4GreenMask) >> greenMaskShift) << 5;
        }
        if (blueMaskShift <= 0) {
            blue = ((*rowBuffer) & DIBheader->bV4BlueMask) << blueMaskShift;
        } else {
            blue = ((*rowBuffer) & DIBheader->bV4BlueMask) >> blueMaskShift;
        }
        *screenPointer = red + green + blue;
        trueRowBuffer += bytesPerPixel;
        rowBuffer = (uint32_t*) trueRowBuffer;
        screenPointer++;
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

    // Whether we can display the image directly
    bool native = DIBheader.biCompression == BI_BITFIELDS && DIBheader.bV4RedMask == 0xf800 && DIBheader.bV4GreenMask == 0x7e0 && DIBheader.bV4BlueMask == 0x1f;

    // How many bytes each pixel takes up
    unsigned int bytesPerPixel = DIBheader.biBitCount/8;

    // How many bytes each row of the bitmap takes up
    unsigned int rowSize = (((DIBheader.biBitCount*DIBheader.biWidth)+31)/32)*4;

    // Buffer for holding a complete row from the image
    uint8_t* rowBuffer;

    // Buffer for holding the palette (unused for now)
    // In the future, a function will be written to convert the 8-8-8-8 RGBA palette of bitmaps into 5-6-5 BGR colors for the display.
    uint16_t* palette;

    switch (DIBheader.biBitCount) {
        case 1:
        case 2:
        case 4:
        case 8:
            rowBuffer = new uint8_t[((((DIBheader.biWidth)+3)/4)*4)];
            if (DIBheader.biClrUsed == 0) {
                palette = new uint16_t[1 << DIBheader.biBitCount];
            } else {
                palette = new uint16_t[DIBheader.biClrUsed];
            }
            break;
        case 16:
        case 24:
        case 32:
            rowBuffer = new uint8_t[rowSize];
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
    uint16_t* screenPointer = vram + (320*239);

    // If the image doesn't fill the whole screen, adjust its starting position in vram to center the image
    if (DIBheader.biWidth < 320) {
        screenPointer += (320 - DIBheader.biWidth)/2;
    }
    if (abs(DIBheader.biHeight) < 240) {
        // Simplifiying (x/2)*320 to x*160
        screenPointer -= (240 - DIBheader.biHeight)*160;
    }

    // Clear out screen before writing the final image
    memset(vram, 0, (320*240)*sizeof(uint16_t));

    // No where near to finished code -- Right now this assumes the image is bottom to top and already stored in 5-6-5 BGR
    for (unsigned int i = 0; i < abs(DIBheader.biHeight) && screenPointer < vram + (320*240); i++) {
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

        // Decide how to draw the row
        if (DIBheader.biCompression == BI_RGB) {
            switch (DIBheader.biBitCount) {
                case 32:
                    displayRGBrow(rowBuffer, DIBheader.biWidth, 4, screenPointer, rgb888to565);
                    break;
                case 24:
                    displayRGBrow(rowBuffer, DIBheader.biWidth, 3, screenPointer, rgb888to565);
                    break;
                case 16:
                    displayRGBrow(rowBuffer, DIBheader.biWidth, 2, screenPointer, rgb1555to565);
                default:
                    break;
            }
        } else {
            if (native) {
                memcpy(screenPointer, rowBuffer, 320*sizeof(uint16_t));
            } else {
                displayBitFieldRow(rowBuffer, DIBheader.biWidth, bytesPerPixel, screenPointer, &DIBheader);
            }
        }

        // Move up 1 row in vram
        screenPointer -= 320;
    }
    delete[] rowBuffer;
    delete[] inputBuffer;
    closeFile(bitmapFile);
    return true;
}