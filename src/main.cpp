#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include "bitmap.hpp"

extern "C" {
    #include <msddrvce.h>
    #include "usb.h"
}

int main() {
    os_ClrHomeFull();
    os_PutStrFull("Attempting to read bitmap, please wait... Press any key to cancel.");
    if (!init_USB()) {
        os_PutStrFull(" Failed to open USB. Press any key to continue.");
        while (!os_GetCSC());
        return 1;
    }
    displayBitmap("/", "test.bmp");
    os_PutStrFull(" Press any key to continue.");
    close_USB();
    while (!os_GetCSC());
}