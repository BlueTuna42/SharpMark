#ifndef XMPTOOLS_H
#define XMPTOOLS_H

#include <string>

class XMPTools {
public:
    static int writeXmpRating(const std::string& filename, int rating);
};

#endif