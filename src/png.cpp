#ifndef _EZ80
#define _EZ80
#endif

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fatdrvce.h>
#include <debug.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include <sys/lcd.h>
#include "pngle/pngle.h"
#include "jpeg.hpp"
#include "common.h"
#include "usb.h"

struct pngDrawData {
    uint16_t* startingScreenPointer = vram;

    // Last location we drew to on the screen.
    uint16_t* lastScreenPointer = 0;

    // Dimensions of the image
    unsigned int width;
    unsigned int height;
    
    // Dimensions to scale the image to
    unsigned int renderWidth;
    unsigned int renderHeight;
};

struct util_FileHandle {
    fat_file_t handle;
    uint32_t len;
    uint32_t pos;
    uint8_t* inputPointer;
};

// thanks I hate this
// what do the w and h values mean???
void PNG_draw(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
    pngDrawData* callbackData = (pngDrawData*)pngle_get_user_data(pngle);
    if (!callbackData) {
        return;
    }
    unsigned int screenX = (((unsigned int)x)*(callbackData->renderWidth))/(callbackData->width);
    unsigned int screenY = (((unsigned int)y)*(callbackData->renderHeight))/(callbackData->height);
    uint16_t* screenPointer = callbackData->startingScreenPointer + (screenY*320) + screenX;
    ColorError err = 0;
    if ((*screenPointer) == 0 && screenPointer != callbackData->lastScreenPointer) {
        *screenPointer = rgb888beto565(rgba, &err);
    }
}

void PNG_init(pngle_t* pngle, uint32_t w, uint32_t h) {
    pngDrawData* callbackData = (pngDrawData*)pngle_get_user_data(pngle);
    if (!callbackData) {
        return;
    }
    // Figure out how we need to scale and reposition the image
    if (w == 320 && h == 240) {
        callbackData->renderWidth = 320;
        callbackData->renderHeight = 240;
    } else {
        float xRatio = 320.0f/static_cast<float>(w);
        float yRatio = 240.0f/static_cast<float>(h);
        if (xRatio < yRatio) {
            callbackData->renderWidth = 320;
            callbackData->renderHeight = static_cast<float>(h)*xRatio;
            callbackData->startingScreenPointer += ((240-callbackData->renderHeight)/2)*320;
        } else {
            callbackData->renderWidth = static_cast<float>(w)*yRatio;
            callbackData->startingScreenPointer += (320-callbackData->renderWidth)/2;
            callbackData->renderHeight = 240;
        }
        if (callbackData->renderWidth > 320) {
            callbackData->renderWidth = 320;
        }
        if (callbackData->renderHeight > 240) {
            callbackData->renderHeight = 240;
        }
    }
    callbackData->width = w;
    callbackData->height = h;
}

util_FileHandle* util_OpenFile(const char* path, const char* name) {
    util_FileHandle* file;
    // Open the file
    fat_file_t* handlePtr = openFile(path, name, false);
    fat_file_t handle;
    if (!handlePtr) {
        return nullptr;
    }

    handle = *handlePtr;
    free(handlePtr);

    // Initialize the buffer
    if (!readFile(&handle, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
        os_PutStrFull(" !Read failed.!");
        fat_CloseFile(&handle);
        return nullptr;
    }

    // Initialize our struct
    file = new util_FileHandle;
    if (!file) {
        fat_CloseFile(&handle);
        return nullptr;
    }
    file->handle = handle;
    file->inputPointer = inputBuffer;
    file->len = fat_GetFileSize(&file->handle);
    file->inputPointer = inputBuffer;
    return file;
}

void util_CloseFile(util_FileHandle* handle) {
    if (handle) {
        fat_CloseFile(&handle->handle);
        delete handle;
    }
}

// Not a callback. Just a utility function.
int util_ReadFile(util_FileHandle* handle, uint8_t* buf, size_t len) {
    // How many bytes are left to copy from the input buffer
    size_t bytesRemaining = len;

    // How many bytes we actually read
    size_t bytesRead;

    // Type cast probably unnecessary but I want to be safe
    // If EOF is less than buf_size away, only read to EOF.
    if (handle->len - handle->pos < (uint32_t)len) {
        bytesRemaining = handle->len - handle->pos;
    }
    bytesRead = bytesRemaining;

    // While the end of the requested area is outside the input buffer, 
    // copy what's in the input buffer and load the next chunk into the input buffer.
    while (handle->inputPointer + bytesRemaining > inputBufferEnd) {
        memcpy(buf, handle->inputPointer, inputBufferEnd - handle->inputPointer);
        buf += inputBufferEnd - handle->inputPointer;
        bytesRemaining -= inputBufferEnd - handle->inputPointer;
        handle->inputPointer = inputBuffer;
        if (!readFile(&handle->handle, inputBufferSize/FAT_BLOCK_SIZE, inputBuffer)) {
            os_PutStrFull(" !Read failed.!");
            return -1;
        }
    }

    // Copy the rest of the requested area from the input buffer
    if (bytesRemaining) {
        memcpy(buf, handle->inputPointer, bytesRemaining);
        
        // Advance the pointer into the input buffer
        handle->inputPointer += bytesRemaining;
    }

    // Update our position in the file
    handle->pos += bytesRead;

    return bytesRead;
}

bool displayPNG(const char* path, const char* name) {
    pngDrawData persistent;
    uint8_t buf[1024];
    int remain = 0;
    int len;
    util_FileHandle* handle;
    pngle_t* pngle = pngle_new();
    // If pngle failed to intialize, return
    if (!pngle) {
        return false;
    }
    // Open the file
    if (!(handle = util_OpenFile(path, name))) {
        pngle_destroy(pngle);
        return false;
    }
    // Set the callback functions
    pngle_set_user_data(pngle, &persistent);
    pngle_set_init_callback(pngle, PNG_init);
    pngle_set_draw_callback(pngle, PNG_draw);

    // Clear out screen before writing the final image
    memset(vram, 0, (320*240)*sizeof(uint16_t));

    // Feed data to pngle
    while ((len = util_ReadFile(handle, buf + remain, sizeof(buf) - remain)) > 0 && !os_GetCSC()) {
        int fed = pngle_feed(pngle, buf, remain + len);
        if (fed < 0) {
            os_PutStrFull("!PNGLE error! ");
            os_PutStrFull(pngle_error(pngle));
            pngle_destroy(pngle);
            return false;
        }
        remain = remain + len - fed;
        if (remain > 0) {
            memmove(buf, buf + fed, remain);
        }
    }
    pngle_destroy(pngle);
    return true;
}