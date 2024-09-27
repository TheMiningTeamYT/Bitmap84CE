typedef struct jpgrddta {
    // File handle
    fat_file_t* handle;
    // File size
    uint32_t size;
    // Current position in the file
    uint32_t pos;
    // Pointer to our current location in the buffer
    uint8_t* inputPointer;
} jpegReadData;

#ifdef __cplusplus
extern "C" {
#endif
unsigned char jpegRead(unsigned char* pBuf, unsigned char buf_size, unsigned char *pBytes_actually_read, 
    void *pCallback_data);
bool jpegOpenFile(const char* path, const char* name, jpegReadData* file);
void jpegCloseFile(jpegReadData* file);
#ifdef __cplusplus
}
#endif