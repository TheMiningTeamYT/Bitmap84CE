#pragma once
#include <cstdint>
#include <fatdrvce.h>

// i'm not gonna lie to ya: i'm just rewriting Microsoft's structs
struct bitmapFileHeader {
    int16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct bitmapInfoHeader {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
    // Since bitmap headers are backwards compatible and identified by their size, I'm just going to use a combined struct
    // Then decide which fields are safe to pull afterward, based on the size.
    uint32_t bV4RedMask;
    uint32_t bV4GreenMask;
    uint32_t bV4BlueMask;
    uint32_t bV4AlphaMask;
    uint32_t bV4CSType;
    uint32_t bV4Endpoints[9]; // Unused for my purposes
    uint32_t bV4GammaRed;
    uint32_t bV4GammaGreen;
    uint32_t bV4GammaBlue;
};

enum biCompressionMode {
    BI_RGB = 0,
    BI_BITFIELDS = 3
};

enum bppModes {
    indexed = 0,
    indexed8,
    native,
    rgb888,
    rgba8888,
    bitfields
};

struct BitfieldMasks {
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
    int8_t redMaskShift;
    int8_t greenMaskShift;
    int8_t blueMaskShift;
    int8_t alphaMaskShift;
};

bool displayBitmap(const char* path, const char* name);