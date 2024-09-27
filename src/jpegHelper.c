#include <stdint.h>
#include <string.h>
#include <fatdrvce.h>
#include <ti/screen.h>
#include "picojpeg/picojpeg.h"
#include "jpegHelper.h"
#include "common.h"
#include "usb.h"

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