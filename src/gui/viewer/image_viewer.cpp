#include "image_viewer.h"

#include "../utils/path_utils.h"
#include "preview_loader.h"

#include <algorithm>
#include <cmath>
#include <gdk/gdkkeysyms.h>
#include <utility>

struct ImageContext {
    std::string filename;
    bool isBlurry = false;
    GtkWidget *viewer_window = nullptr;
    GtkWidget *image = nullptr;
    GtkWidget *previous_button = nullptr;
    GtkWidget *next_button = nullptr;
    ImageViewerCallbacks callbacks;
};

struct ViewerBounds {
    int windowWidth;
    int windowHeight;
    int imageWidth;
    int imageHeight;
};

static void update_viewer_navigation_state(ImageContext* ctx);

static ViewerBounds get_viewer_bounds(GtkWindow* parent) {
    GdkRectangle workarea{0, 0, 1024, 768};

    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkMonitor* monitor = nullptr;
        if (parent) {
            GdkWindow* parentWindow = gtk_widget_get_window(GTK_WIDGET(parent));
            if (parentWindow) {
                monitor = gdk_display_get_monitor_at_window(display, parentWindow);
            }
        }

        if (!monitor) {
            monitor = gdk_display_get_primary_monitor(display);
        }

        if (monitor) {
            gdk_monitor_get_workarea(monitor, &workarea);
        }
    }

    const int windowWidth = std::max(640, static_cast<int>(std::lround(workarea.width * 0.90)));
    const int windowHeight = std::max(480, static_cast<int>(std::lround(workarea.height * 0.86)));

    return {
        windowWidth,
        windowHeight,
        std::max(320, windowWidth - 48),
        std::max(240, windowHeight - 120)
    };
}

static ViewerBounds get_viewer_bounds(ImageContext* ctx) {
    GtkWindow* parent = nullptr;
    if (ctx && ctx->viewer_window) {
        parent = gtk_window_get_transient_for(GTK_WINDOW(ctx->viewer_window));
    }
    return get_viewer_bounds(parent);
}

static void update_viewer_image(ImageContext* ctx) {
    const ViewerBounds bounds = get_viewer_bounds(ctx);
    GdkPixbuf *pixbuf = add_status_border(load_preview_pixbuf(ctx->filename, bounds.imageWidth, bounds.imageHeight),
                                          ctx->isBlurry);
    gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->image), pixbuf);
    if (pixbuf) {
        g_object_unref(pixbuf);
    }

    const std::string displayName = path_filename(ctx->filename);
    const std::string statusText = ctx->isBlurry ? "Blurry" : "Sharp";
    const std::string title = displayName + " - " + statusText;

    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), title.c_str());
    gtk_window_resize(GTK_WINDOW(ctx->viewer_window), bounds.windowWidth, bounds.windowHeight);
    update_viewer_navigation_state(ctx);
}

static bool move_viewer_to_adjacent(ImageContext* ctx, bool forward) {
    if (!ctx) {
        return false;
    }

    const int currentIndex = ctx->callbacks.visibleIndexForFilename
        ? ctx->callbacks.visibleIndexForFilename(ctx->filename)
        : -1;
    if (currentIndex < 0) {
        return false;
    }

    const int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    const ResultData* target = ctx->callbacks.visibleAt
        ? ctx->callbacks.visibleAt(targetIndex)
        : nullptr;
    if (!target) {
        return false;
    }

    ctx->filename = target->filename;
    ctx->isBlurry = target->isBlurry;
    update_viewer_image(ctx);

    if (ctx->callbacks.selectVisibleRow) {
        ctx->callbacks.selectVisibleRow(targetIndex);
    }

    return true;
}

static void update_viewer_navigation_state(ImageContext* ctx) {
    if (!ctx) {
        return;
    }

    const int currentIndex = ctx->callbacks.visibleIndexForFilename
        ? ctx->callbacks.visibleIndexForFilename(ctx->filename)
        : -1;
    const gboolean hasPrevious = currentIndex > 0;
    const gboolean hasNext = ctx->callbacks.visibleAt
        ? ctx->callbacks.visibleAt(currentIndex + 1) != nullptr
        : FALSE;
    gtk_widget_set_sensitive(ctx->previous_button, hasPrevious);
    gtk_widget_set_sensitive(ctx->next_button, hasNext);
}

static void on_previous_clicked(GtkWidget* widget, gpointer data) {
    move_viewer_to_adjacent(static_cast<ImageContext*>(data), false);
}

static void on_next_clicked(GtkWidget* widget, gpointer data) {
    move_viewer_to_adjacent(static_cast<ImageContext*>(data), true);
}

static void on_viewer_destroy(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    delete ctx;
}

static void on_delete_clicked(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (!ctx || !ctx->callbacks.deleteByFilename) {
        return;
    }

    if (ctx->callbacks.deleteByFilename(ctx->filename, GTK_WINDOW(ctx->viewer_window))) {
        gtk_widget_destroy(ctx->viewer_window);
    }
}

static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(ctx->viewer_window);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Page_Up) {
        return move_viewer_to_adjacent(ctx, false) ? TRUE : FALSE;
    }

    if (event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_space) {
        return move_viewer_to_adjacent(ctx, true) ? TRUE : FALSE;
    }

    if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
        on_delete_clicked(widget, data);
        return TRUE;
    }

    return FALSE;
}

void open_image_viewer(GtkWindow* parent, const ResultData& result, ImageViewerCallbacks callbacks) {
    ImageContext* ctx = new ImageContext();
    ctx->filename = result.filename;
    ctx->isBlurry = result.isBlurry;
    ctx->callbacks = std::move(callbacks);

    ctx->viewer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), path_filename(ctx->filename).c_str());
    const ViewerBounds bounds = get_viewer_bounds(parent);
    gtk_window_set_default_size(GTK_WINDOW(ctx->viewer_window), bounds.windowWidth, bounds.windowHeight);
    gtk_window_set_position(GTK_WINDOW(ctx->viewer_window), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_transient_for(GTK_WINDOW(ctx->viewer_window), parent);

    g_signal_connect(ctx->viewer_window, "destroy", G_CALLBACK(on_viewer_destroy), ctx);
    g_signal_connect(ctx->viewer_window, "key-press-event", G_CALLBACK(on_viewer_key_press), ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(ctx->viewer_window), vbox);

    ctx->image = gtk_image_new();

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), ctx->image);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    ctx->previous_button = gtk_button_new_with_label("Previous");
    g_signal_connect(ctx->previous_button, "clicked", G_CALLBACK(on_previous_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->previous_button, TRUE, TRUE, 0);

    ctx->next_button = gtk_button_new_with_label("Next");
    g_signal_connect(ctx->next_button, "clicked", G_CALLBACK(on_next_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->next_button, TRUE, TRUE, 0);

    GtkWidget *btn_delete = gtk_button_new_with_label("Delete");
    gtk_widget_set_name(btn_delete, "delete-photo-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_delete), "delete-button");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), btn_delete, TRUE, TRUE, 0);

    update_viewer_image(ctx);

    gtk_widget_show_all(ctx->viewer_window);
}
