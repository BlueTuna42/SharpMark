#ifndef SCAN_H
#define SCAN_H

#include <vector>
#include <QString>

class Scanner {
public:
    static std::vector<QString> scanFiles(const QString& path);
};

#endif