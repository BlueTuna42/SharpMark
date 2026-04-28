#ifndef PHOTO_TRASH_H
#define PHOTO_TRASH_H

#include <gtk/gtk.h>
#include <string>

bool confirm_and_trash_photo(GtkWindow* parent, const std::string& filename);
bool trash_photo(GtkWindow* parent, const std::string& filename, std::string* errorMessage = nullptr);

#endif
