#include <ncurses.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Screen width and height
int scrHeight, scrWidth;
// Disassembly window width
int disWidth = 15;
// If ram needs to be updated
bool ramDirty = true;
// If regs needs to be updated
bool regsDirty = true;
// If screen needs to be updated
bool screenDirty = true;
// Step mode
bool stepMode = false;
// Delay
int delayTime = 100000;

// Program memory
uint8_t rom[256];
// Random access memory
uint8_t ram[128];
// Pointer to the current instruction
uint8_t pcPtr;
// Temporary PC Register
uint8_t tmpPcPtr;
// Location Register (used for RAM access)
uint8_t locPtr;
// ALU Registers
uint8_t regX, regY, regZ;
// If carry should be used for math
bool useCarry = false;
bool carry;

// Opcode enum
enum Opcodes {
    OP_NOP, // -
    OP_ADD, // Z = X + Y
    OP_SUB, // Z = X - Y
    OP_WT1, // locPtr = (locPtr & 0x0F) | ((val & 0xF) << 4)
    OP_WT2, // locPtr = (locPtr & 0xF0) | (val & 0xF)
    OP_WTX, // X = val
    OP_WTY, // Y = val
    OP_WTZ, // Z = val
    OP_ZTR, // ram[locPtr] = Z
    OP_RTZ, // Z = ram[locPtr]
    OP_PC1, // tmpPcPtr = (tmpPcPtr & 0x0F) | ((val & 0xF) << 4)
    OP_PC2, // tmpPcPtr = (tmpPcPtr & 0xF0) | (val & 0xF)
    OP_JMP, // pcPtr = tmpPcPtr
    OP_RTX, // ram[locPtr] = X
    OP_RTY, // ram[locPtr] = Y
    OP_USC  // useCarry = !useCarry
};

char* DecodeOpCode(uint8_t* buff, int addr) {
    uint8_t op = (buff[addr] & 0xF0) >> 4;
    switch(op) {
        case OP_NOP: return "NOP";
        case OP_ADD: return "ADD";
        case OP_SUB: return "SUB";
        case OP_WT1: return "WT1";
        case OP_WT2: return "WT2";
        case OP_WTX: return "WTX";
        case OP_WTY: return "WTY";
        case OP_WTZ: return "WTZ";
        case OP_ZTR: return "ZTR";
        case OP_RTZ: return "RTZ";
        case OP_PC1: return "PC1";
        case OP_PC2: return "PC2";
        case OP_JMP: return "JMP";
        case OP_RTX: return "RTX";
        case OP_RTY: return "RTY";
        case OP_USC: return "USC";
    }
    return "ERR";
}

// Write a 4-Bit value to the buffer
void WriteNibble(uint8_t* buff, uint8_t addr, uint8_t val) {
    if (addr % 2 == 0)
        buff[addr/2] = (buff[addr/2] & 0xF0) | (val & 0x0F);
    else
        buff[addr/2] = (buff[addr/2] & 0x0F) | ((val & 0x0F) << 4);
}

// Read a 4-Bit value from the buffer
uint8_t ReadNibble(uint8_t* buff, uint8_t addr) {
    if (addr % 2 == 0)
        return buff[addr/2] & 0x0F;
    else
        return (buff[addr/2] >> 4) & 0x0F;
}

// Limit registers to 4-Bit range
void LimitRegs() {
    regX &= 0xF;
    regY &= 0xF;
    regZ &= 0xF;
}

// Perform a single simulation step
void SimStep() {
    uint8_t op = rom[pcPtr] >> 4;
    uint8_t imm = rom[pcPtr] & 0xF;
    switch(op) {
        case OP_NOP:
            break;
        case OP_ADD:
            regZ = regX + regY + (useCarry ? (uint8_t)carry : 0);
            carry = (regZ >> 4) & 0x1;
            regsDirty = true;
            break;
        // This may not be 100% accurate, due to me
        // being unsure how logisim implements these
        case OP_SUB: {
            uint8_t subTmp = regY + (useCarry ? (uint8_t)carry : 0);
            regZ = regX - subTmp;
            carry = regX >= subTmp;
            regsDirty = true;
            break;
        }
        case OP_WT1:
            locPtr = (locPtr & 0x0F) | (imm << 4);
            regsDirty = true;
            break;
        case OP_WT2:
            locPtr = (locPtr & 0xF0) | (imm);
            regsDirty = true;
            break;
        case OP_WTX:
            regX = imm;
            regsDirty = true;
            break;
        case OP_WTY:
            regY = imm;
            regsDirty = true;
            break;
        case OP_WTZ:
            regZ = imm;
            regsDirty = true;
            break;
        case OP_ZTR:
            WriteNibble(ram, locPtr, regZ);
            screenDirty = (locPtr < 4);
            ramDirty = true;
            break;
        case OP_RTZ:
            regZ = ReadNibble(ram, locPtr);
            regsDirty = true;
            break;
        case OP_PC1:
            tmpPcPtr = (tmpPcPtr & 0xF0) | (imm);
            regsDirty = true;
            break;
        case OP_PC2:
            tmpPcPtr = (tmpPcPtr & 0x0F) | (imm << 4);
            regsDirty = true;
            break;
        case OP_JMP:
            tmpPcPtr--; // Needs to be here due to a hardware quirk
            pcPtr = tmpPcPtr;
            break;
        case OP_RTX:
            regX = ReadNibble(ram, locPtr);
            regsDirty = true;
            break;
        case OP_RTY:
            regY = ReadNibble(ram, locPtr);
            regsDirty = true;
            break;
        case OP_USC:
            useCarry = !useCarry;
            break;
    }
    LimitRegs();
    pcPtr++;
}

// Update the 4x4 screen
void UpdateScreen(WINDOW* win) {
    if (!ramDirty) return;
    for (uint8_t row = 0; row < 4*2; row++) {   
        wmove(win, row+1, 1); 
        uint8_t rowVal = ReadNibble(ram, row/2);
        for (uint8_t col = 0; col < 4; col++) {
            if ((rowVal >> (3 - col)) & 0x1) {
                waddnstr(win, "####", 4);
            } else {
                waddnstr(win, "    ", 4);
            }
        }
    }
    box(win, 0, 0);
    wnoutrefresh(win);
}

// Update the disassembly window
void UpdateDisassembly(WINDOW* win) {
    // Get window size
    int y,x;
    getmaxyx(win, y, x);
    werase(win);
    box(win, 0, 0);
    mvwaddstr(win, 0, 1, "[Disassembly]");
    int cursor_row = y / 2; // where the ">" is
    mvwaddch(win, cursor_row, 1, '>');
    int half_lines = (y - 2) / 2; // number of lines above/below cursor
    for (int offset = -half_lines; offset <=half_lines; offset++) {
        int line = cursor_row + offset;
        if (line <= 0 || line >= y-1) continue;
        int addr = pcPtr + offset;
        if (addr < 0 || addr >= (int)sizeof(rom)) continue;

        mvwprintw(
            win,
            line, pcPtr == addr ? 3 : 2,
            "%02X:  %s %01X",
            addr,
            DecodeOpCode(rom, addr),
            rom[addr] & 0xF
        );
    }
    wnoutrefresh(win);
}

// Update Register Window
void UpdateRegisters(WINDOW* win) {
    int y,x;
    getmaxyx(win, y, x);
    box(win, 0, 0);
    mvwaddstr(win, 0, 1, "[Registers]");
    mvwprintw(win, 1, 1, "  X: %01X Y: %01X Z: %01X", regX, regY, regZ);
    mvwprintw(win, 2, 2, "PC: %02X", pcPtr);
    mvwprintw(win, 2, x-2-6, "LC: %02X", locPtr);
    wnoutrefresh(win);
}

// Render memory contents

void UpdateMemory(WINDOW* win) {
    int h, w;
    getmaxyx(win, h, w);

    const int bytes_per_row = 16;
    const int max_bytes = 0x100;

    mvwprintw(win, 2+locPtr/bytes_per_row, 5+((locPtr%bytes_per_row)*2), "%01X", ReadNibble(ram, locPtr));
    wnoutrefresh(win);
}

// Init Memory
void InitMemory(WINDOW* win) {
    int h, w;
    getmaxyx(win, h, w);
    box(win, 0, 0);
    mvwaddstr(win, 0, 2, "[Memory]");

    const int bytes_per_row = 16;
    const int max_bytes = 0x100;
    for (int i = 0; i < bytes_per_row; ++i) {
        mvwprintw(win, 1, 5+(i*2), "%01X ", i);
    }

    for (int row = 0; row < h - 2; ++row) {
        int addr = row * bytes_per_row;
        if (addr >= max_bytes) break;

        mvwprintw(win, row + 2, 1, "%02X: ", addr);

        for (int col = 0; col < bytes_per_row; ++col) {
            int index = addr + col;
            if (index >= max_bytes) break;

            wprintw(win, "%01X ", ReadNibble(ram, addr));
        }
    }
    wnoutrefresh(win);
}

// Render info text
void UpdateText(WINDOW* win) {
    box(win,0,0);
    mvwaddstr(win, 1, 3, "PBPU-Emu 1.0.0");
    mvwaddstr(win, 2, 3, "by  PixelBrush");
    wnoutrefresh(win);
}

// Main function
int main(int argc, char** argv) {
    // Read other params
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("pbpu <file> [options]\n");
            printf("--help: Print help info\n");
            printf("--step: Single step mode\n");
            printf("--delay=<num>: Delay in microseconds\n");
            return 0;
        }
        if (strcmp(argv[i], "--step") == 0) {
            stepMode = true;
        }
        if (strncmp(argv[i], "--delay=", 8) == 0) {
            if (sscanf(argv[i] + 8, "%d", &delayTime) != 1) {
                printf("Invalid delay value!\n");
                return 1;
            }
            if (delayTime < 0) {
                printf("Delay can't be negative!\n");
                return 1;
            }
        }
    }
    // Check if program filename has been passed in
    if (argc < 2) {
        printf("No program passed in!\n");
        return 1;
    }
    // Scoping these so they don't stick
    // around in memory while we don't need them
    {
        FILE* prgFile;
        prgFile = fopen(argv[1], "rb");
        if (prgFile == NULL) {
            printf("Program not found!\n");
            fclose(prgFile);
            return 1;
        }
        size_t readBytes = fread(rom, sizeof(uint8_t), sizeof(rom) - 1, prgFile);
        printf("Read %zu bytes.\n", readBytes);
        if (readBytes <= 0) {
            printf("Program is empty!\n");
            fclose(prgFile);
            return 1;
        }
        fclose(prgFile);
    }

    // Init ncurses window
    initscr();

    getmaxyx(stdscr, scrHeight, scrWidth);

    // Define sub-windows
    WINDOW* regWin = newwin(4, 20, 0, 0);
    WINDOW* scrWin = newwin(4*2+2,4*4+2,4,1);
    WINDOW* memWin = newwin(scrHeight, 0xF*2 + 8, 0, 20);
    WINDOW* disWin = newwin(scrHeight,disWidth,0, 20 + 0xF*2 + 8);
    WINDOW* texWin = newwin(4, 20, scrHeight-4, 0);
    // Only needs to be rendered once
    InitMemory(memWin);

    noecho();
    cbreak();
    if (!stepMode)
        nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    idlok(stdscr, TRUE);
    idcok(stdscr, TRUE);
    curs_set(0);

    // Main program look
    while(true) {
        if (!stepMode) {
            usleep(delayTime);
        } else {
            getch();
        }

        SimStep();

        UpdateDisassembly(disWin);
        if (regsDirty) {
            UpdateRegisters(regWin);
            regsDirty = false;
        }
        if (screenDirty) {
            UpdateScreen(scrWin);
            screenDirty = false;
        }
        if (ramDirty) {
            UpdateMemory(memWin);
            ramDirty = false;
        }
        doupdate();
    }
    delwin(scrWin);
    endwin();
    return 0;
}