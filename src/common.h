#define vram ((uint16_t*)0xD40000)
#define inputBufferSize (32*(FAT_BLOCK_SIZE))
// Workaround to make VS code stop yelling at me
#ifndef uint24_t
typedef unsigned int uint24_t;
#endif

// Used by rgb888to565.
// Should be zeroed out at the start of a row.
typedef uint24_t ColorError;

#ifdef __cplusplus
extern "C" {
#endif
uint16_t rgb888to565(uint8_t* triplet, ColorError* err);
uint16_t rgb888beto565(uint8_t* triplet, ColorError* err);
void spiCmd(uint8_t cmd);
void spiParam(uint8_t cmd);
void boot_InitializeHardware();
#ifdef __cplusplus
}
#endif

// We need to do some buffer shennanigans because we can only read a whole number of blocks from the file at a time.
extern uint8_t inputBuffer[inputBufferSize];

// A pointer to the end of the input buffer
#define inputBufferEnd (inputBuffer+inputBufferSize)