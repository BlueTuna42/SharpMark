#ifndef DELETE_BLURRY_ACTION_H
#define DELETE_BLURRY_ACTION_H

#include <gtk/gtk.h>
#include <string>
#include <vector>

struct DeleteBlurryResult {
    bool confirmed = false;
    std::vector<std::string> trashedFiles;
};

DeleteBlurryResult delete_blurry_photos(GtkWindow* parent, const std::vector<std::string>& blurryFiles);

#endif
