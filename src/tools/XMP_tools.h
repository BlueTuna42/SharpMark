#ifndef XMPTOOLS_H
#define XMPTOOLS_H

#include <QString>

namespace XMPTools {
    int writeXmpRating(const QString& exiftoolPath, const QString& sourceFile, const QString& targetFile, int rating);
    int  writeXmpColorLabel(const QString& exiftoolPath, const QString& targetFile, const QString& label);
    QString readXmpColorLabel(const QString& filePath);
}

#endif