#include "XMP_tools.h"
#include <iostream>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

// Helper function to execute command silently on Windows
static int executeCommandSilent(const std::string& cmd) {
#ifdef _WIN32
    // On Windows, use CreateProcess without creating a console window
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));
    std::string cmdCopy = cmd;

    // CREATE_NO_WINDOW to prevents the console from flashing
    if (!CreateProcessA(NULL, &cmdCopy[0], NULL, NULL, FALSE, 
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    // Close process and thread handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
#else
    // On Linux/macOS, keep the standard system call
    return std::system(cmd.c_str());
#endif
}

int XMPTools::writeXmpRating(const std::string& filename, int rating) {
    std::ostringstream cmd;
    
    cmd << "exiftool -overwrite_original -XMP-xmp:Rating=" << rating
        << " \"" << filename << "\" > " << NULL_DEVICE << " 2>&1";

    return executeCommandSilent(cmd.str());
}