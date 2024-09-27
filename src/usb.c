/*
Based on code from the TI CE C/C++ toolchain
Licensed under the LGPLv3 https://github.com/CE-Programming/toolchain/blob/master/license
*/
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <fatdrvce.h>
#include <msddrvce.h>
#include <usbdrvce.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include "usb.h"

static msd_partition_t partitions[MAX_PARTITIONS];
static global_t global;
static char name[16];
static char path[256];

void stringToUpper(char* buffer, size_t bufferLength, const char* str) {
    size_t i = 0;
    bufferLength -= 1;
    while (str[i] && bufferLength) {
        buffer[i] = toupper(str[i]);
        bufferLength--;
        i++;
    }
    buffer[i] = 0;
}

usb_error_t handleUsbEvent(usb_event_t event, void *event_data, usb_callback_data_t *global) {
    switch (event) {
        case USB_DEVICE_DISCONNECTED_EVENT:
            if (global->usb) {
                // msd_Close(&global.msd);
                global->storageInit = false;
            }
            global->usb = NULL;
            break;
        case USB_DEVICE_CONNECTED_EVENT:
            return usb_ResetDevice(event_data);
        case USB_DEVICE_ENABLED_EVENT:
            global->usb = event_data;
            break;
        case USB_DEVICE_DISABLED_EVENT:
            return USB_USER_ERROR;
        default:
            break;
    }
    return USB_SUCCESS;
}

bool init_USB() {
    usb_error_t usberr;
    msd_info_t msdinfo;
    uint8_t num_partitions;
    memset(&global, 0, sizeof(global_t));
    uint8_t key;
    global.usb = NULL;
    usberr = usb_Init((usb_event_callback_t) handleUsbEvent, &global, NULL, USB_DEFAULT_INIT_FLAGS);
    while (!global.usb) {
        if (usberr || os_GetCSC()) {
            goto cleanup;
        }
        usberr = usb_WaitForInterrupt();
    }
    if (msd_Open(&global.msd, global.usb)) {
        goto cleanup;
    }
    global.storageInit = true;
    if (msd_Info(&global.msd, &msdinfo)) {
        goto cleanup;
    }
    num_partitions = msd_FindPartitions(&global.msd, partitions, MAX_PARTITIONS);
    if (num_partitions < 1) {
        goto cleanup;
    }
    for (uint8_t i = 0; i < num_partitions; i++) {
        if (!fat_Open(&global.fat, (fat_read_callback_t) &msd_Read, (fat_write_callback_t) &msd_Write, &global.msd, partitions[i].first_lba)) {
            global.fatInit = true;
            return true;
        }
    }
    cleanup:
    usb_Cleanup();
    return false;
}

fat_file_t* openFile(const char* sourcePath, const char* sourceName, bool create) {
    fat_file_t* file = calloc(1, sizeof(fat_file_t));
    stringToUpper(name, 16, sourceName);
    if (sourcePath[0] == 0) {
        if (fat_OpenFile(&global.fat, name, 0, file) != FAT_SUCCESS) {
            /*printStringAndMoveDownCentered("Failed to open file");
            printStringAndMoveDownCentered(str);*/
            free(file);
            return NULL;
        }
    } else {
        stringToUpper(path, 256, sourcePath);
        if (path[strlen(path) - 1] != '/') {
            strncat(path, "/", 255-strlen(path));
        }
        strncat(path, name, 255-strlen(path));
        if (create) {
            fat_Create(&global.fat, path, name, 0);
        }
        if (fat_OpenFile(&global.fat, path, 0, file) != FAT_SUCCESS) {
            free(file);
            return NULL;
        }
    }
    // cursed hack to add support for created/modified dates
    time_t currentTime;
    time(&currentTime);
    struct tm* currentLocalTime = localtime(&currentTime);
    uint16_t* entryPointer = *((uint16_t**)(&file->priv[40]));
    ((uint8_t*)entryPointer)[11] = FAT_ARCHIVE;
    ((uint8_t*)entryPointer)[13] = 0;
    entryPointer[11] = ((currentLocalTime->tm_sec)>>1) + (currentLocalTime->tm_min<<5) + (currentLocalTime->tm_hour<<11);
    entryPointer[9] = entryPointer[12] = (currentLocalTime->tm_mday) + ((currentLocalTime->tm_mon + 1) << 5) + ((currentLocalTime->tm_year - 80)<<9);
    if (create) {
        entryPointer[7] = entryPointer[11];
        entryPointer[8] = entryPointer[12];
    }
    return file;
}

void closeFile(fat_file_t* file) {
    if (file != NULL) {
        fat_CloseFile(file);
        free(file);
    }
}

fat_dir_t* openDir(const char* sourcePath) {
    fat_dir_t* folder = calloc(1, sizeof(fat_dir_t));
    stringToUpper(path, 256, sourcePath);
    if (fat_OpenDir(&global.fat, path, folder) != FAT_SUCCESS) {
        free(folder);
        return NULL;
    }
    return folder;
}

void closeDir(fat_dir_t* folder) {
    if (folder) {
        fat_CloseDir(folder);
        free(folder);
    }
}

bool readFile(fat_file_t* file, size_t bufferSize, void* buffer) {
    if (file == NULL) {
        return false;
    }
    size_t readSize = ((fat_GetFileSize(file) + 511)/FAT_BLOCK_SIZE) - fat_GetFileBlockOffset(file);
    if (readSize > bufferSize) {
        readSize = bufferSize;
    }
    bool good = fat_ReadFile(file, readSize, buffer) == readSize;
    return good;
}

bool writeFile(fat_file_t* file, size_t size, void* buffer) {
    if (file == NULL) {
        return false;
    }
    size_t writeBlocks = (size + FAT_BLOCK_SIZE - 1)/FAT_BLOCK_SIZE;
    if (fat_SetFileSize(file, size)) {
        // printStringAndMoveDownCentered("Failed to set size");
        return false;
    }
    bool good = fat_WriteFile(file, writeBlocks, buffer) == writeBlocks;
    // cursed hack to add support for created/modified dates
    time_t currentTime;
    time(&currentTime);
    struct tm* currentLocalTime = localtime(&currentTime);
    uint16_t* entryPointer = *((uint16_t**)(&file->priv[40]));
    ((uint8_t*)entryPointer)[11] = FAT_ARCHIVE;
    entryPointer[11] = ((currentLocalTime->tm_sec)>>1) + (currentLocalTime->tm_min<<5) + (currentLocalTime->tm_hour<<11);
    entryPointer[9] = entryPointer[12] = (currentLocalTime->tm_mday) + ((currentLocalTime->tm_mon + 1) << 5) + ((currentLocalTime->tm_year - 80)<<9);
    return good;
}

bool seekFile(fat_file_t* file, size_t blockOffset, seek_origin_t origin) {
    // All of these numbers are in blocks.
    size_t pos;
    size_t fileSize;
    if (file == NULL) {
        return false;
    }
    fileSize = ((fat_GetFileSize(file) + 511)/FAT_BLOCK_SIZE);
    switch (origin) {
        case set:
            pos = blockOffset;
            break;
        case cur:
            pos = fat_GetFileBlockOffset(file) + blockOffset;
            break;
        case end:
            pos = fileSize - blockOffset;
            break;
        default:
            break;
    }
    if (pos < 0 || pos > fileSize) {
        return false;
    }
    return fat_SetFileBlockOffset(file, pos) == FAT_SUCCESS;
}

bool createDirectory(const char* sourcePath, const char* sourceName) {
    fat_error_t faterr;
    stringToUpper(name, 16, sourceName);
    stringToUpper(path, 256, sourcePath);
    faterr = fat_Create(&global.fat, path, name, FAT_DIR);
    if (faterr != FAT_SUCCESS && faterr != FAT_ERROR_EXISTS) {
        return false;
    }
    return true;
} 

uint32_t getSizeOf(fat_file_t* file) {
    if (file == NULL) {
        return 0;
    }
    return fat_GetFileSize(file);
}

void deleteFile(const char* sourcePath, const char* sourceName) {
    stringToUpper(name, 16, sourceName);
    stringToUpper(path, 256, sourcePath);
    if (path[strlen(path) - 1] != '/') {
        strncat(path, "/", 255-strlen(path));
    }
    strncat(path, name, 255-strlen(path));
    fat_Delete(&global.fat, path);
}

void close_USB() {
    if (global.fatInit == true) {
        fat_Close(&global.fat);
    }
    if (global.storageInit == true) {
        msd_Close(&global.msd);
    }
    usb_Cleanup();
}