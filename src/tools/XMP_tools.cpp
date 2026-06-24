#include "XMP_tools.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <fstream>

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

int XMPTools::writeXmpColorLabel(const QString& exiftoolPath, const QString& targetFile, const QString& label) {
    // label is one of: "Red","Yellow","Green","Blue","Purple","" (empty = clear)
    QString escaped = label;
    escaped.replace("\"", "\\\"");
#ifdef _WIN32
    QString cmd = "\"" + exiftoolPath + "\" -overwrite_original \"-xmp:Label=" +
                  escaped + "\" \"" + targetFile + "\"";
    return executeCommandSilentW(cmd);
#else
    QString cmd = "\"" + exiftoolPath + "\" -overwrite_original \"-xmp:Label=" +
                  escaped + "\" \"" + targetFile + "\"";
    return std::system(cmd.toUtf8().constData());
#endif
}

QString XMPTools::readXmpColorLabel(const QString& filePath) {
    // Fast native scan — no process spawn.
    // Looks for xmp:Label="Red" or <xmp:Label>Red</xmp:Label> within the first 1 MB.
#ifdef _WIN32
    std::ifstream file(filePath.toStdWString(), std::ios::binary);
#else
    std::ifstream file(filePath.toUtf8().constData(), std::ios::binary);
#endif
    if (!file.is_open()) return QString();

    const size_t bufSize = 1024 * 1024;
    std::string buf;
    buf.resize(bufSize);
    file.read(&buf[0], bufSize);
    buf.resize(file.gcount());

    // Patterns to search for (attribute and element forms)
    static const std::vector<std::pair<std::string, std::string>> patterns = {
        {"xmp:Label=\"", "\""},
        {"<xmp:Label>",  "</xmp:Label>"},
        {"xmp:Label='" , "'"},
    };

    for (const auto& [open, close] : patterns) {
        size_t pos = buf.find(open);
        if (pos == std::string::npos) continue;
        size_t start = pos + open.size();
        size_t end   = buf.find(close, start);
        if (end == std::string::npos || end - start > 32) continue;
        std::string val = buf.substr(start, end - start);
        // Trim whitespace
        while (!val.empty() && (val.front() == ' ' || val.front() == '\n' || val.front() == '\r')) val.erase(val.begin());
        while (!val.empty() && (val.back()  == ' ' || val.back()  == '\n' || val.back()  == '\r')) val.pop_back();
        if (!val.empty()) return QString::fromStdString(val);
    }
    return QString();
}