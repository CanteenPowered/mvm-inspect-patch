#ifdef _WIN32
    #define PATCHER_WINDOWS
    #define _CRT_SECURE_NO_WARNINGS
#endif

#include <conio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef PATCHER_WINDOWS
    #include <windows.h>
    #include <tlhelp32.h>

    #pragma comment(lib, "user32.lib")
#endif

//
// Utility functions
//

void die(const char* error) {
#ifdef PATCHER_WINDOWS
    MessageBox(NULL, error, "Patcher - Error", MB_OK | MB_ICONERROR);
#endif
    exit(1);
}

//
// Process API wrapper
//

typedef struct {
    uintptr_t   pid;
#ifdef PATCHER_WINDOWS
    HANDLE      handle;
#endif
} process_t;

typedef struct {
    uintptr_t   base_address;
    char        path[1024];
} module_t;

bool open_process(const char* name, process_t* process) {
#ifdef PATCHER_WINDOWS
    // Snapshot all running processes
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        die("Failed to get process list");
    }

    // Find matching entry
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    if (Process32First(snap, &entry) == TRUE) {
        do {
            if (strcmp(entry.szExeFile, name) == 0) {
                process->pid = entry.th32ProcessID;
                process->handle = OpenProcess(PROCESS_ALL_ACCESS | PROCESS_VM_WRITE, FALSE, entry.th32ProcessID);
                if (process->handle == NULL) {
                    die("Failed to open proocess");
                }
                CloseHandle(snap);
                return true;
            }
        } while (Process32Next(snap, &entry));
    }

    CloseHandle(snap);
#endif
    return false;
}

void close_process(process_t* process) {
#ifdef PATCHER_WINDOWS
    CloseHandle(process->handle);
#endif
}

bool find_process_module(process_t* process, const char* name, module_t* module) {
#ifdef PATCHER_WINDOWS
    // Snapshot process modules
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process->pid);
    if (snap == INVALID_HANDLE_VALUE) {
        die("Failed to get process module list");
    }

    // Find matching module
    MODULEENTRY32  entry;
    entry.dwSize = sizeof(entry);
    if (Module32First(snap, &entry) == TRUE) {
        do {
            if (strcmp(entry.szModule, name) == 0 ) {
                module->base_address = (uintptr_t)entry.modBaseAddr;
                strncpy(module->path, entry.szExePath, sizeof(module->path));
                CloseHandle(snap);
                return true;
            }
        } while (Module32Next(snap, &entry));
    }

    CloseHandle(snap);
#endif
    return false;
}

bool write_process_memory(process_t* process, uintptr_t address, void* buf, size_t bufsz) {
    return WriteProcessMemory(process->handle, (LPVOID)address, buf, bufsz, NULL) != 0;
}

//
// Utility functions
//

typedef struct {
    uint8_t*    data;
    size_t      size;
} buffer_t;

buffer_t load_binary_file(const char* path) {
    buffer_t buf = { 0 };

    FILE* f = fopen(path, "rb");
    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        buf.size = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf.data = malloc(buf.size);
        if (buf.data != NULL) {
            fread(buf.data, 1, buf.size, f);
        }
        fclose(f);
    }

    return buf;
}

uintptr_t scan_binary_file(const buffer_t* buf, const char* pattern) {
    for (uintptr_t i = 0; i < buf->size; ++i) {
        // Match pattern
        for (uintptr_t j = 0; ; ++j) {
            // Don't overrun
            if (i + j >= buf->size) {
                break;
            }
            // End of pattern, successful match
            if (pattern[j] == '\0') {
                return i;
            }
            // Compare bytes
            if (pattern[j] != '\x2A' && (uint8_t)pattern[j] != buf->data[i + j]) {
                break;
            }
        }
    }
    return 0;
}

static int next_key(void) {
#ifdef PATCHER_WINDOWS
    return _getch();
#endif
}

bool ask_continue(void) {
    printf("Press Y to continue or any other key to quit\n");
    int c = next_key();
    return c == 'y' || c == 'Y';
}

//
// Main
//

const char* WARNING = 
    "=== !!! WARNING !!! ==========================================================\n"
    "This program is going to patch your TF2 client. Connecting to VAC-secured     \n"
    "servers with a modified client can get you VAC banned. It probably won't      \n"
    "happen, but it could. I am not responsible for any damages done to your       \n"
    "computer or Steam account.                                                    \n"
    "                                                                              \n"
    "Read more: https://help.steampowered.com/en/faqs/view/571A-97DA-70E9-FF74     \n"
    "===========================================================!!! WARNING !!! ===";

#define X86INS_NOP 0x90
#define X86INS_JMP 0xEB

#ifdef PATCHER_WINDOWS
    #define TF2_EXE         "hl2.exe"
    #define TF2_CLIENT      "client.dll"
    #define PATCH_TARGET    "\x75\x2A\x8B\x0D\x2A\x2A\x2A\x2A\x68\x2A\x2A\x2A\x2A\x8B\x01\xFF\x50\x2A\x5E\x5F"
    #define PATCH_DATA      X86INS_JMP
#endif

int main(int argc, char* argv[]) {
    // Disable prompt if --no-prompt flag is set
    bool disable_prompt = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-prompt") == 0) {
            disable_prompt = true;
        }
    }

    // Display warning and wait for user confirmation
    printf("%s\n", WARNING);
    if (!disable_prompt && !ask_continue()) {
        exit(0);
    }

    // Find running TF2 process
    process_t tf2;
    if (!open_process(TF2_EXE, &tf2)) {
        die("Couldn't find TF2 process. Is the game running?");
    }
    printf("Found running Team Fortress 2 process:\n");
    printf("    PID:            %u\n", tf2.pid);

    // Find client game library (client.dll, client.so)
    module_t client;
    if (!find_process_module(&tf2, TF2_CLIENT, &client)) {
        die("Failed to find TF2 client library");
    }

    printf("Found client library:\n");
    printf("    Base address:   0x%X\n", client.base_address);
    printf("    Path:           %s\n", client.path);

    // Load client library from disk
    buffer_t client_file = load_binary_file(client.path);
    if (client_file.data == NULL) {
        die("Failed to load TF2 client library from disk");
    }

    // Find address of call to ClientModeTFNormal::BIsFriendOrPartyMember()
    // in CHudInspectPanel::UserCmd_InspectTarget()
    uintptr_t patch_offset = scan_binary_file(&client_file, PATCH_TARGET);
    if (patch_offset == 0) {
        die("Failed to find patch target. Please create an issue on GitHub");
    }
    uintptr_t patch_target = client.base_address + patch_offset;
#ifdef PATCHER_WINDOWS
    // code addresses are mapped in by an extra 0xC00
    patch_target += 0xC00;
#endif

    printf("Found patch target:\n");
    printf("    File offset:    %s+0x%X\n", TF2_CLIENT, patch_offset);
    printf("    Virtual addr:   0x%X\n", patch_target);

    // Write patch to game memory
    uint8_t patch[] = { PATCH_DATA };
    if (!write_process_memory(&tf2, patch_target, patch, sizeof(patch))) {
        die("Failed to write to game memory");
    }

    printf("Applied %d byte patch\n", sizeof(patch));

    // Clean up
    close_process(&tf2);

    printf("Have a nice day.");
    if (!disable_prompt) {
        next_key();
    }
}