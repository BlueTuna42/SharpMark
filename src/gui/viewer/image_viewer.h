#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "../results/result_store.h"
#include "../gui.h"

#include <functional>
#include <gtk/gtk.h>
#include <string>

struct ImageViewerCallbacks {
    std::function<int(const std::string&)> visibleIndexForFilename;
    std::function<const ResultData*(int)> visibleAt;
    std::function<bool(const std::string&, GtkWindow*)> deleteByFilename;
    std::function<void(int)> selectVisibleRow;
    std::function<void(const std::string&, bool)> trainAI; 
    std::function<double(const std::string&)> getAestheticScore;
};

void open_image_viewer(GtkWindow* parent, const ResultData& result, int rawMode, const ImageViewerCallbacks& callbacks);

#endif