#include "XMP_tools.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

// Helper function to execute command silently on Windows
static int executeCommandSilent(const std::string& cmd) {
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE hRead, hWrite;

    SECURITY_ATTRIBUTES saAttr; 
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
    saAttr.bInheritHandle = TRUE; 
    saAttr.lpSecurityDescriptor = NULL; 

    if (!CreatePipe(&hRead, &hWrite, &saAttr, 0)) return -1;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back('\0');

    if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }

    CloseHandle(hWrite);

    DWORD dwRead;
    CHAR chBuf[4096];
    std::string output;
    while (ReadFile(hRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead != 0) {
        chBuf[dwRead] = '\0';
        output += chBuf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    if (exitCode != 0) {
        std::cerr << "ExifTool Error Output: " << output << std::endl;
    }

    return static_cast<int>(exitCode);
#else
    return std::system(cmd.c_str());
#endif
}

// XMPTools namespace or class implementation
int XMPTools::writeXmpRating(const std::string& exiftoolPath, const std::string& sourceFile, const std::string& targetFile, int rating) {
    std::ostringstream cmd;
    
    cmd << "\"" << exiftoolPath << "\" -overwrite_original -xmp:Rating=" << rating 
        << " \"" << targetFile << "\"";
        
    return executeCommandSilent(cmd.str());
}