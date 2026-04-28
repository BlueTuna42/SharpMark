#include "progress_summary.h"

#include <algorithm>
#include <cmath>
#include <string>

void set_progress_bar(GtkWidget* progressBar, int processedFiles, int totalFiles) {
    if (!progressBar) {
        return;
    }

    const double fraction = totalFiles > 0
        ? static_cast<double>(processedFiles) / totalFiles
        : 0.0;
    const int percent = static_cast<int>(std::lround(fraction * 100.0));
    const std::string text = totalFiles > 0
        ? std::to_string(processedFiles) + " / " + std::to_string(totalFiles) + " (" + std::to_string(percent) + "%)"
        : "0 / 0";

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), std::clamp(fraction, 0.0, 1.0));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progressBar), text.c_str());
}

gboolean on_summary_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    GUIContext* ctx = static_cast<GUIContext*>(data);
    if (!ctx) {
        return FALSE;
    }

    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);
    const int barHeight = std::min(8, height);
    const int barY = (height - barHeight) / 2;
    const int total = ctx->summarySharp + ctx->summaryBlurry;
    if (total <= 0) {
        cairo_set_source_rgb(cr, 0.74, 0.74, 0.74);
        cairo_rectangle(cr, 0, barY, width, barHeight);
        cairo_fill(cr);

        return FALSE;
    }

    const double sharpFraction = total > 0
        ? static_cast<double>(ctx->summarySharp) / total
        : 0.0;
    const int sharpWidth = static_cast<int>(std::lround(width * sharpFraction));

    cairo_set_source_rgb(cr, 0.18, 0.58, 0.24);
    cairo_rectangle(cr, 0, barY, sharpWidth, barHeight);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.82, 0.16, 0.16);
    cairo_rectangle(cr, sharpWidth, barY, width - sharpWidth, barHeight);
    cairo_fill(cr);

    return FALSE;
}

void update_summary_bar(GUIContext& ctx, int sharpFiles, int blurryFiles) {
    ctx.summarySharp = sharpFiles;
    ctx.summaryBlurry = blurryFiles;

    const std::string sharpText = "<span foreground=\"forestgreen\"><b>" + std::to_string(sharpFiles) + "</b></span>";
    const std::string blurryText = "<span foreground=\"red\"><b>" + std::to_string(blurryFiles) + "</b></span>";

    gtk_label_set_markup(GTK_LABEL(ctx.summary_sharp_label), sharpText.c_str());
    gtk_label_set_markup(GTK_LABEL(ctx.summary_blurry_label), blurryText.c_str());
    gtk_widget_queue_draw(ctx.summary_bar);
    gtk_widget_show(ctx.summary_sharp_label);
    gtk_widget_show(ctx.summary_bar);
    gtk_widget_show(ctx.summary_blurry_label);
    gtk_widget_show(ctx.summary_box);
}
