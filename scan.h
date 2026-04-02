#ifndef SCAN_H
#define SCAN_H

#include <vector>
#include <string>

class Scanner {
public:
    static std::vector<std::string> scanBmpFiles(const std::string& path);
};

#endif