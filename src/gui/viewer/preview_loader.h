#ifndef PREVIEW_LOADER_H
#define PREVIEW_LOADER_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string>

 GdkPixbuf* load_preview_pixbuf(const std::string& filename, int maxWidth, int maxHeight, int rawMode = 0);
GdkPixbuf* add_status_border(GdkPixbuf* pixbuf, bool isBlurry, int borderWidth = 4);

#endif