#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fatdrvce.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include <sys/lcd.h>
#include "picojpeg/picojpeg.h"
#include "jpeg.hpp"
#include "common.h"
#include "usb.h"

struct jpegReadData {
    // File handle
    fat_file_t* handle;
    // File size
    uint32_t size;
    // Current position in the file
    uint32_t pos;
    // Pointer to our current location in the buffer
    uint8_t* inputPointer;
};

bool jpegOpenFile(const char* path, const char* name, jpegReadData* file) {
    // Open the file
    file->handle = openFile(path, name, false);
    if (!file->handle) {
        return false;
    }

    // Initialize the buffer
    if (!readFile(file->handle, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
        os_PutStrFull(" !Read failed.!");
        closeFile(file->handle);
        return false;
    }

    // Initialize our struct
    file->size = fat_GetFileSize(file->handle);
    file->pos = 0;
    file->inputPointer = inputBuffer;
    return true;
}

void jpegCloseFile(jpegReadData* file) {
    closeFile(file->handle);
}

unsigned char jpegRead(unsigned char* pBuf, unsigned char buf_size, unsigned char *pBytes_actually_read, 
    void *pCallback_data) {
    jpegReadData* callbackData = (jpegReadData*) pCallback_data;

    // How many bytes are left to copy from the input buffer to pBuf
    uint8_t bytesRemaining = buf_size;

    // Type cast probably unnecessary but I want to be safe
    // If EOF is less than buf_size away, only read to EOF.
    if (callbackData->size - callbackData->pos < (uint32_t)buf_size) {
        bytesRemaining = callbackData->size - callbackData->pos;
    }

    // Write how many bytes we're going to read.
    *pBytes_actually_read = bytesRemaining;

    // Update our current position in the file.
    callbackData->pos += bytesRemaining;

    // While the end of the requested area is outside the input buffer, 
    // copy what's in the input buffer and load the next chunk into the input buffer.
    while (callbackData->inputPointer + bytesRemaining > inputBufferEnd) {
        memcpy(pBuf, callbackData->inputPointer, inputBufferEnd - callbackData->inputPointer);
        pBuf += inputBufferEnd - callbackData->inputPointer;
        bytesRemaining -= inputBufferEnd - callbackData->inputPointer;
        callbackData->inputPointer = inputBuffer;
        if (!readFile(callbackData->handle, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
            os_PutStrFull(" !Read failed.!");
            return PJPG_STREAM_READ_ERROR;
        }
    }

    // Copy the rest of the requested area from the input buffer
    if (bytesRemaining) {
        memcpy(pBuf, callbackData->inputPointer, bytesRemaining);
        
        // Advance the pointer into the input buffer
        callbackData->inputPointer += bytesRemaining;
    }

    return 0;
}

// Assumes that init_USB has already been callled
bool displayJPEG(const char* path, const char* name) {
    // JPEG decompression context
    pjpeg_image_info_t context;

    // JPEG read callback data
    jpegReadData callbackData;

    // Pointer to our current position in vram
    uint16_t* screenPointer = vram;
    uint16_t* rowPointer;

    // Used for converting rgb888 to 565
    ColorError err[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    // The current MCU in the row we're on
    unsigned int currentMCU = 0;

    // Used for scaling on the x axis
    int xError = 0;
    unsigned int x = 0;

    // Used for scaling on the y axis
    int yError = 0;
    unsigned int y = 0;

    // Dimensions to scale the image to
    unsigned int renderWidth;
    unsigned int renderHeight;

    // Scaling ratios
    float xRatio;
    float yRatio;

    // Decode status
    bool status;

    // This code is nowhere near done, it's just for testing to see if picojpeg will work.
    // Open the JPEG file
    if (!jpegOpenFile(path, name, &callbackData)) {
        jpegCloseFile(&callbackData);
        return false;
    }

    // Init picojpeg
    if (pjpeg_decode_init(&context, jpegRead, &callbackData, 0)) {
        jpegCloseFile(&callbackData);
        return false;
    };

    // Figure out how we need to scale and reposition the image
    if (context.m_width == 320 && context.m_height == 240) {
        renderWidth = 320;
        renderHeight = 240;
    } else {
        xRatio = 320.0f/static_cast<float>(context.m_width);
        yRatio = 240.0f/static_cast<float>(context.m_height);
        if (xRatio < yRatio) {
            renderWidth = 320;
            renderHeight = static_cast<float>(context.m_height)*xRatio;
            screenPointer += ((240-renderHeight)/2)*320;
        } else {
            renderWidth = static_cast<float>(context.m_width)*yRatio;
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

    rowPointer = screenPointer;

    // Clear out screen before writing the final image
    memset(vram, 0, (320*240)*sizeof(uint16_t));

    // Decode the MCUs and draw them to the screen!
    while ((status = pjpeg_decode_mcu()) != PJPG_NO_MORE_BLOCKS && !os_GetCSC()) {
        uint8_t color[3];
        uint8_t mcuWidth = context.m_MCUWidth;
        unsigned int mcuY = 0;
        unsigned int mcuHeight = context.m_MCUHeight;
        if (status) {
            return false;
        }
        if (x + mcuWidth > context.m_width) {
            mcuWidth = context.m_width - x;
        }
        if (y + mcuHeight > context.m_height) {
            mcuHeight = context.m_height - y;
        }
        // If this is the last MCU in the row, give it special treatment
        if (currentMCU == context.m_MCUSPerRow - 1) {
            while (mcuY < mcuHeight) {
                while (yError >= 0) {
                    int localXError = xError;
                    uint8_t mcuX = 0;
                    uint16_t* rowBuffer = rowPointer;
                    while (mcuX < mcuWidth) {
                        uint16_t pixel;
                        size_t index = (mcuY*8) + mcuX;
                        if (mcuX >= 8) {
                            index += 56;
                        }
                        if (mcuY >= 8 && context.m_scanType == PJPG_YH2V2) {
                            index += 64;
                        }
                        if (context.m_scanType == PJPG_GRAYSCALE) {
                            color[0] = context.m_pMCUBufR[index];
                            color[1] = color[0];
                            color[2] = color[0];
                        } else {
                            color[0] = context.m_pMCUBufB[index];
                            color[1] = context.m_pMCUBufG[index];
                            color[2] = context.m_pMCUBufR[index];
                        }
                        pixel = rgb888to565(color, &err[mcuY]);
                        while (localXError >= 0) {
                            *rowBuffer = pixel;
                            rowBuffer++;
                            localXError -= context.m_width;
                        }
                        while (localXError < 0 && mcuX < mcuWidth) {
                            mcuX++;
                            localXError += renderWidth;
                        }
                    }
                    rowPointer += 320;
                    screenPointer += 320;
                    yError -= context.m_height;
                }
                while (yError < 0 && mcuY < mcuHeight) {
                    mcuY++;
                    yError += renderHeight;
                }
            }
            rowPointer = screenPointer;
            for (uint8_t i = 0; i < 16; i++) {
                err[i] = 0;
            }
            x = 0;
            xError = 0;
            currentMCU = 0;
        } else {
            uint16_t* localScreenPointer = rowPointer;
            uint16_t* rowBuffer = rowPointer;
            int localYError = yError;
            int localXError;
            while (mcuY < mcuHeight) {
                while (localYError >= 0) {
                    uint8_t mcuX = 0;
                    localXError = xError;
                    rowBuffer = localScreenPointer;
                    while (mcuX < mcuWidth) {
                        uint16_t pixel;
                        size_t index = (mcuY*8) + mcuX;
                        if (mcuX >= 8) {
                            index += 56;
                        }
                        if (mcuY >= 8 && context.m_scanType == PJPG_YH2V2) {
                            index += 64;
                        }
                        if (context.m_scanType == PJPG_GRAYSCALE) {
                            color[0] = context.m_pMCUBufR[index];
                            color[1] = color[0];
                            color[2] = color[0];
                        } else {
                            color[0] = context.m_pMCUBufB[index];
                            color[1] = context.m_pMCUBufG[index];
                            color[2] = context.m_pMCUBufR[index];
                        }
                        pixel = rgb888to565(color, &err[mcuY]);
                        while (localXError >= 0) {
                            *rowBuffer = pixel;
                            rowBuffer++;
                            localXError -= context.m_width;
                        }
                        while (localXError < 0 && mcuX < mcuWidth) {
                            mcuX++;
                            localXError += renderWidth;
                        }
                    }
                    localScreenPointer += 320;
                    localYError -= context.m_height;
                }
                while (localYError < 0 && mcuY < mcuHeight) {
                    mcuY++;
                    localYError += renderHeight;
                }
            }
            x += mcuWidth;
            xError = localXError;
            rowPointer += (rowBuffer - localScreenPointer) + 320;
            currentMCU++;
        }
    }
    jpegCloseFile(&callbackData);

    return true;
}