#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include "bitmap.hpp"
#include "font.hpp"
#include <graphx.h>
#include "gfx/gfx.h"

extern "C" {
    #include <msddrvce.h>
    #include "usb.h"
}

struct fileEntry {
    char* name;
    bool dir;
};

void gfxStart() {
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTextTransparentColor(127);
    memcpy(gfx_palette, global_palette, sizeof(uint16_t)*256);
    gfx_SetTransparentColor(1);
}

void drawFileSelection(fileEntry file, int row, bool selected) {
    if (selected) {
        gfx_SetColor(255);
        gfx_FillRectangle_NoClip(0, (40*row)+40, 320, 40);
        gfx_SetTextFGColor(0);
        gfx_SetTextBGColor(255);
    } else {
        gfx_SetColor(0);
        gfx_FillRectangle_NoClip(0, (40*row)+40, 320, 40);
        gfx_SetTextFGColor(255);
        gfx_SetTextBGColor(0);
    }
    if (file.dir) {
        gfx_TransparentSprite_NoClip(directory_closed, 4, (40*row)+44);
    } else {
        gfx_TransparentSprite_NoClip(image_old_jpeg, 4, (40*row)+44);
    }
    gfx_PrintStringXY(file.name, 40, (40*row)+44);
}

void fileSelectMenu() {
    gfx_SetTextScale(2, 2);
    char currentDirPath[256] = "/";
    parseDir:
    fat_dir_t* currentDir = openDir(currentDirPath);
    if (!currentDir) {
        return;
    }
    fat_dir_entry_t currentDirEntry;
    fat_ReadDir(currentDir, &currentDirEntry);
    fileEntry* entries = (fileEntry*) malloc(0);
    unsigned int numberOfEntries = 0;
    while (currentDirEntry.name[0]) {
        if ((currentDirEntry.attrib & FAT_DIR) || (strcmp(currentDirEntry.name + (strlen(currentDirEntry.name)-4), ".BMP") == 0)) {
            entries = (fileEntry*) realloc(entries, sizeof(fileEntry)*(numberOfEntries + 1));
            entries[numberOfEntries].name = new char[strlen(currentDirEntry.name) + 1];
            strcpy(entries[numberOfEntries].name, currentDirEntry.name);
            if (currentDirEntry.attrib & FAT_DIR) {
                entries[numberOfEntries].dir = true;
            } else {
                entries[numberOfEntries].dir = false;
            }
            numberOfEntries++;
        }
        fat_ReadDir(currentDir, &currentDirEntry);
    }
    unsigned int offset = 0;
    unsigned int selectedFile = 0;
    bool quit = false;
    while (!quit) {
        gfx_SetTextFGColor(255);
        gfx_SetTextBGColor(0);
        gfx_FillScreen(0);
        printStringCentered("Please select an", 4);
        printStringCentered("image to open", 20);
        for (unsigned int i = 0; i + offset < numberOfEntries && i < 5; i++) {
            drawFileSelection(entries[i + offset], i, i == selectedFile);
        }
        gfx_SwapDraw();
        bool quit2 = false;
        while (!quit2) {
            switch(os_GetCSC()) {
                case sk_Enter:
                    if (entries[selectedFile + offset].dir) {
                        if (strcmp(entries[selectedFile + offset].name, ".")) {
                            if (currentDirPath[strlen(currentDirPath) - 1] != '/') {
                                strncat(currentDirPath, "/", 256);
                                currentDirPath[255] = 0;
                            }
                            if (strcmp(entries[selectedFile + offset].name, "..") == 0) {
                                char* pathPointer = currentDirPath + strlen(currentDirPath) - 1;
                                if (*pathPointer == '/') {
                                    pathPointer--;
                                }
                                while (pathPointer > currentDirPath && *pathPointer != '/') {
                                    pathPointer--;
                                }
                                if (pathPointer == currentDirPath) {
                                    *(pathPointer + 1) = 0;
                                } else {
                                    *pathPointer = 0;
                                }
                            } else {
                                strncat(currentDirPath, entries[selectedFile + offset].name, 256);
                                currentDirPath[255] = 0;
                            }
                            closeDir(currentDir);
                            for (unsigned int i = 0; i < numberOfEntries; i++) {
                                delete[] entries[i].name;
                            }
                            free(entries);
                            goto parseDir;
                        }
                    } else {
                        quit2 = true;
                        gfx_End();
                        bool status = displayBitmap(currentDirPath, entries[selectedFile + offset].name);
                        while (!os_GetCSC());
                        gfxStart();
                        gfx_SetTextScale(2, 2);
                        gfx_SetTextFGColor(255);
                        gfx_SetTextBGColor(0);
                        gfx_FillScreen(0);
                        if (!status) {
                            printStringCentered("Failed to open image", 4);
                            printStringCentered(entries[selectedFile + offset].name, 23);
                            printStringCentered("Press any key to", 42);
                            printStringCentered("continue", 61);
                            gfx_SwapDraw();
                            while (!os_GetCSC());
                        }
                    }
                    break;
                case sk_Up:
                    gfx_BlitScreen();
                    if (selectedFile > 0) {
                        drawFileSelection(entries[selectedFile + offset], selectedFile, false);
                        selectedFile--;
                        drawFileSelection(entries[selectedFile + offset], selectedFile, true);
                    } else if (selectedFile + offset > 0) {
                        offset--;
                        quit2 = true;
                    }
                    gfx_SwapDraw();
                    break;
                case sk_Down:
                    gfx_BlitScreen();
                    if (selectedFile + offset + 1 < numberOfEntries) {
                        if (selectedFile < 4) {
                            drawFileSelection(entries[selectedFile + offset], selectedFile, false);
                            selectedFile++;
                            drawFileSelection(entries[selectedFile + offset], selectedFile, true);
                        } else {
                            offset++;
                            quit2 = true;
                        }
                    }
                    gfx_SwapDraw();
                    break;
                case sk_Clear:
                    closeDir(currentDir);
                    for (unsigned int i = 0; i < numberOfEntries; i++) {
                        delete[] entries[i].name;
                    }
                    free(entries);
                    return;
                default:
                    break;
            }
        }
    }
    closeDir(currentDir);
    for (unsigned int i = 0; i < numberOfEntries; i++) {
        delete[] entries[i].name;
    }
    free(entries);
}

int main() {
    gfxStart();
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(255);
    gfx_SetTextBGColor(0);
    gfx_FillScreen(0);
    printStringCentered("Welcome to Bitmap84CE!", 3);
    gfx_SetTextScale(1, 1);
    gfx_SetTextXY(0, 20);
    printStringAndMoveDownCentered("The high quality bitmap image viewer");
    printStringAndMoveDownCentered("for the TI 84 Plus CE.");
    printStringAndMoveDownCentered("Made by Logan C.");
    printStringAndMoveDownCentered("Controls:");
    printStringAndMoveDownCentered("Press up/down to scroll up/down the list of");
    printStringAndMoveDownCentered("files.");
    printStringAndMoveDownCentered("Press enter to enter a folder or display an");
    printStringAndMoveDownCentered("image.");
    printStringAndMoveDownCentered("To back out of a folder, enter the folder");
    printStringAndMoveDownCentered("called \"..\".");
    printStringAndMoveDownCentered("To exit, press \"clear\".");
    printStringAndMoveDownCentered("Please insert a FAT32 formatted USB drive");
    printStringAndMoveDownCentered("containing any images you want to view,");
    printStringAndMoveDownCentered("(do not remove it until you exit),");
    printStringAndMoveDownCentered("and press any key to continue.");
    printStringAndMoveDownCentered("(For best results, resize the images");
    printStringAndMoveDownCentered("to be 320x240 pixels or smaller before");
    printStringAndMoveDownCentered("loading them onto your calculator.");
    printStringAndMoveDownCentered("Only images in bitmap format");
    printStringAndMoveDownCentered("are currently supported.)");
    gfx_SwapDraw();
    while (!os_GetCSC());
    if (!init_USB()) {
        gfx_BlitScreen();
        printStringAndMoveDownCentered("Failed to open USB. Press any key to continue.");
        gfx_SwapDraw();
        while (!os_GetCSC());
        close_USB();
        gfx_End();
    }
    fileSelectMenu();
    gfx_SetDrawBuffer();
    gfx_SetTextScale(2, 2);
    gfx_SetTextFGColor(255);
    gfx_SetTextBGColor(0);
    gfx_FillScreen(0);
    printStringCentered("You may now remove", 0);
    printStringCentered("your USB drive.", 20);
    gfx_SetTextScale(1, 1);
    printStringCentered("Thank you for using Bitmap84CE.", 40);
    gfx_SwapDraw();
    close_USB();
    gfx_End();
}