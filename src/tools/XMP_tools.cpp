#include "XMP_tools.h"
#include <iostream>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// Helper: execute a command silently, with full Unicode support
#ifdef _WIN32
static int executeCommandSilentW(const QString& cmd) {
    std::wstring wcmd = cmd.toStdWString();

    STARTUPINFOW si;
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
    si.hStdError  = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags   |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }

    CloseHandle(hWrite);

    DWORD dwRead;
    char chBuf[4096];
    while (ReadFile(hRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead != 0) {}

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    return static_cast<int>(exitCode);
}
#endif

int XMPTools::writeXmpRating(const QString& exiftoolPath, const QString& sourceFile, const QString& targetFile, int rating) {
#ifdef _WIN32
    QString cmd = "\"" + exiftoolPath + "\" -overwrite_original -xmp:Rating=" +
                  QString::number(rating) + " \"" + targetFile + "\"";
    return executeCommandSilentW(cmd);
#else
    QString cmd = "\"" + exiftoolPath + "\" -overwrite_original -xmp:Rating=" +
                  QString::number(rating) + " \"" + targetFile + "\"";
    return std::system(cmd.toUtf8().constData());
#endif
}