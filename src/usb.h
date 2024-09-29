#pragma once
typedef struct global global_t;
#define usb_callback_data_t global_t
#define fat_callback_usr_t msd_t

#include <fatdrvce.h>
#include <msddrvce.h>
#include <usbdrvce.h>

#define MAX_PARTITIONS 32

struct global {
    usb_device_t usb;
    msd_t msd;
    fat_t fat;
    bool storageInit;
    bool fatInit;
};

typedef enum seek_origin {
    set = 0,
    cur,
    end
} seek_origin_t;

#ifdef __cplusplus
extern "C" {
#endif

fat_dir_t* openDir(const char* sourcePath);
void closeDir(fat_dir_t* folder);
usb_error_t handleUsbEvent(usb_event_t event, void *event_data, usb_callback_data_t *global);

// Takes size in blocks
bool readFile(fat_file_t* file, size_t bufferSize, void* buffer);

bool writeFile(fat_file_t* file, size_t size, void* buffer);
bool createDirectory(const char* path, const char* name);
bool seekFile(fat_file_t* file, size_t blockOffset, seek_origin_t origin);
fat_file_t* openFile(const char* path, const char* name, bool create);
fat_file_t* openFileNoPath(const char* sourcePath);
void closeFile(fat_file_t* file);
uint32_t getSizeOf(fat_file_t* file);
void deleteFile(const char* path, const char* name);
bool init_USB();
void close_USB();
void stringToUpper(char* buffer, size_t bufferLength, const char* str);

#ifdef __cplusplus
}
#endif