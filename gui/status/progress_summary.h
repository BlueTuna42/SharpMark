#ifndef PROGRESS_SUMMARY_H
#define PROGRESS_SUMMARY_H

#include "../gui_context.h"

#include <gtk/gtk.h>

void set_progress_bar(GtkWidget* progressBar, int processedFiles, int totalFiles);
gboolean on_summary_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
void update_summary_bar(GUIContext& ctx, int sharpFiles, int blurryFiles);

#endif
