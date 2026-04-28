#include "XMP_tools.h"
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
    #define NULL_DEVICE "NUL"
#else
    #define NULL_DEVICE "/dev/null"
#endif

int XMPTools::writeXmpRating(const std::string& filename, int rating) {
    std::ostringstream cmd;
    cmd << "exiftool -overwrite_original -XMP-xmp:Rating=" << rating 
    << " \"" << filename << "\" > " << NULL_DEVICE << " 2>&1";
        
    return std::system(cmd.str().c_str());
}