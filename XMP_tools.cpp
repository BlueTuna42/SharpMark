#include "XMP_tools.h"
#include <cstdlib>
#include <sstream>

int XMPTools::writeXmpRating(const std::string& filename, int rating) {
    std::ostringstream cmd;
    cmd << "exiftool -overwrite_original -XMP-xmp:Rating=" << rating 
        << " \"" << filename << "\" > /dev/null 2>&1";
        
    return std::system(cmd.str().c_str());
}