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

// Assumes that init_USB has already been callled
bool displayBitmap(const char* path, const char* name) {
    // Open the bitmap file for reading.
    // If the file fails to open, return.
    fat_file_t* bitmapFile = openFile(path, name, false);
    if (!bitmapFile) {
        return false;
    }
    // Allocate a buffer for reading data from the file
    // We need to do some buffer shennanigans because we can only read a whole number of blocks from the file at a time.
    uint8_t* inputBuffer = new uint8_t[10240];
    if (!readFile(bitmapFile, 10240, inputBuffer)) {
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
    bool v4Header = false;
    if (DIBheader.biSize >= 108) {
        v4Header = true;
    }
    // For now, only BI_BITFIELDS is supported because
    // for this proof of concept, the image is assumed to be in 5-6-5 BGR
    // which is not the default for 16bpp bitmaps
    if (DIBheader.biCompression != BI_RGB && DIBheader.biCompression != BI_BITFIELDS) {
        os_PutStrFull(" !Compression mode wrong!");
        delete[] inputBuffer;
        closeFile(bitmapFile);
        return false;
    }
    // How many bytes each row of the bitmap takes up
    unsigned int rowSize = (((DIBheader.biBitCount*DIBheader.biWidth)+31)/32)*4;
    // Buffer for holding a complete row from the image
    uint8_t* rowBuffer;
    if (DIBheader.biBitCount <= 8) {
        rowBuffer = new uint8_t[(((DIBheader.biWidth)+3)/4)*4];
    } else {
        rowBuffer = new uint8_t[rowSize];
    }
    // Just a placeholder for the rest of the image rendering code.
    memcpy(rowBuffer, inputBuffer + fileHeader.bfOffBits, rowSize);
    memcpy(vram, rowBuffer, rowSize);
    delete[] rowBuffer;
    delete[] inputBuffer;
    closeFile(bitmapFile);
    return true;
}