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
    GtkWidget *drawing_area = nullptr; 
    GtkWidget *scrolled = nullptr; 
    GtkWidget *previous_button = nullptr;
    GtkWidget *next_button = nullptr;
    ImageViewerCallbacks callbacks;

    GdkPixbuf *original_pixbuf = nullptr; 
    double zoom_level = 1.0;
    
    // Panning state
    bool is_dragging = false;
    double drag_last_x = 0.0;
    double drag_last_y = 0.0;

    // Zoom pivot state
    double pivot_orig_x = 0.0; 
    double pivot_orig_y = 0.0; 
    double mouse_view_x = 0.0; 
    double mouse_view_y = 0.0; 

    // Window states
    bool initial_fit_done = false;
    bool is_fullscreen = false;
};

static void update_viewer_navigation_state(ImageContext* ctx);
static void load_current_image(ImageContext* ctx);
static void reset_zoom_to_fit(ImageContext* ctx);
static void apply_zoom_sync(ImageContext* ctx);

static gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (!ctx->original_pixbuf) {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_paint(cr);
        return FALSE;
    }

    cairo_save(cr);
    cairo_scale(cr, ctx->zoom_level, ctx->zoom_level);
    gdk_cairo_set_source_pixbuf(cr, ctx->original_pixbuf, 0, 0);
    
    cairo_pattern_set_filter(cairo_get_source(cr), 
        ctx->zoom_level >= 1.0 ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);

    cairo_paint(cr);
    cairo_restore(cr);

    return TRUE;
}

static void apply_zoom_sync(ImageContext* ctx) {
    if (!ctx->original_pixbuf) return;

    int orig_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
    int orig_h = gdk_pixbuf_get_height(ctx->original_pixbuf);

    int new_w = std::max(1, static_cast<int>(std::round(orig_w * ctx->zoom_level)));
    int new_h = std::max(1, static_cast<int>(std::round(orig_h * ctx->zoom_level)));
    
    gtk_widget_set_size_request(ctx->drawing_area, new_w, new_h);
    gtk_widget_queue_draw(ctx->drawing_area);

    GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));

    double new_hadj_val = (ctx->pivot_orig_x * ctx->zoom_level) - ctx->mouse_view_x;
    double new_vadj_val = (ctx->pivot_orig_y * ctx->zoom_level) - ctx->mouse_view_y;

    gtk_adjustment_set_value(hadj, new_hadj_val);
    gtk_adjustment_set_value(vadj, new_vadj_val);
}

static void reset_zoom_to_fit(ImageContext* ctx) {
    if (!ctx->original_pixbuf) return;

    int orig_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
    int orig_h = gdk_pixbuf_get_height(ctx->original_pixbuf);

    int view_w = 800;
    int view_h = 600;

    // Get actual allocation of the scrolled view
    if (ctx->scrolled) {
        GtkAllocation alloc;
        gtk_widget_get_allocation(ctx->scrolled, &alloc);
        if (alloc.width > 1 && alloc.height > 1) {
            view_w = alloc.width;
            view_h = alloc.height;
        } else if (ctx->viewer_window) {
            gtk_window_get_size(GTK_WINDOW(ctx->viewer_window), &view_w, &view_h);
            view_h -= 50; // Approximate offset for bottom buttons
        }
    }

    double zoom_w = (double)(view_w) / orig_w;
    double zoom_h = (double)(view_h) / orig_h;
    
    // Prevent zooming past 100% on initial load
    ctx->zoom_level = std::min({1.0, zoom_w, zoom_h});
    
    ctx->pivot_orig_x = orig_w / 2.0;
    ctx->pivot_orig_y = orig_h / 2.0;
    ctx->mouse_view_x = (orig_w * ctx->zoom_level) / 2.0;
    ctx->mouse_view_y = (orig_h * ctx->zoom_level) / 2.0;
}

static void on_scrolled_size_allocate(GtkWidget* widget, GtkAllocation* allocation, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    // Fit image to real dimensions on first window render
    if (!ctx->initial_fit_done && allocation->width > 1 && allocation->height > 1) {
        ctx->initial_fit_done = true;
        reset_zoom_to_fit(ctx);
        apply_zoom_sync(ctx);
    }
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (event->button == 1) {
        if (event->type == GDK_2BUTTON_PRESS) {
            reset_zoom_to_fit(ctx);
            apply_zoom_sync(ctx);
            return TRUE;
        }
        ctx->is_dragging = true;
        ctx->drag_last_x = event->x_root;
        ctx->drag_last_y = event->y_root;
        
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) {
            GdkCursor *cursor = gdk_cursor_new_from_name(gdk_window_get_display(window), "grabbing");
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (event->button == 1) {
        ctx->is_dragging = false;
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) gdk_window_set_cursor(window, NULL);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (ctx->is_dragging) {
        double dx = event->x_root - ctx->drag_last_x;
        double dy = event->y_root - ctx->drag_last_y;

        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));

        gtk_adjustment_set_value(hadj, gtk_adjustment_get_value(hadj) - dx);
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_value(vadj) - dy);

        ctx->drag_last_x = event->x_root;
        ctx->drag_last_y = event->y_root;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_viewer_scroll(GtkWidget* widget, GdkEventScroll* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (event->state & GDK_CONTROL_MASK) {
        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));

        ctx->pivot_orig_x = event->x / ctx->zoom_level;
        ctx->pivot_orig_y = event->y / ctx->zoom_level;
        
        ctx->mouse_view_x = event->x - gtk_adjustment_get_value(hadj);
        ctx->mouse_view_y = event->y - gtk_adjustment_get_value(vadj);

        double zoom_factor = 1.2; 
        if (event->direction == GDK_SCROLL_UP) ctx->zoom_level *= zoom_factor;
        else if (event->direction == GDK_SCROLL_DOWN) ctx->zoom_level /= zoom_factor;
        else if (event->direction == GDK_SCROLL_SMOOTH) {
            if (event->delta_y < 0) ctx->zoom_level *= zoom_factor;
            else if (event->delta_y > 0) ctx->zoom_level /= zoom_factor;
        }

        ctx->zoom_level = std::clamp(ctx->zoom_level, 0.01, 50.0);
        apply_zoom_sync(ctx);
        return TRUE; 
    }
    return FALSE; 
}

static void on_viewer_destroy(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (ctx->original_pixbuf) g_object_unref(ctx->original_pixbuf);
    delete ctx;
}

static void load_current_image(ImageContext* ctx) {
    if (ctx->original_pixbuf) {
        g_object_unref(ctx->original_pixbuf);
        ctx->original_pixbuf = nullptr;
    }

    ctx->original_pixbuf = load_preview_pixbuf(ctx->filename, 8192, 8192);

    if (ctx->original_pixbuf) {
        int w = gdk_pixbuf_get_width(ctx->original_pixbuf);
        int h = gdk_pixbuf_get_height(ctx->original_pixbuf);
        int dynamicBorder = std::max(8, std::min(w, h) / 100);
        
        ctx->original_pixbuf = add_status_border(ctx->original_pixbuf, ctx->isBlurry, dynamicBorder);

        reset_zoom_to_fit(ctx);
        apply_zoom_sync(ctx);
    } else {
        gtk_widget_queue_draw(ctx->drawing_area);
    }

    std::string title = path_filename(ctx->filename) + (ctx->isBlurry ? " (Blurry)" : " (Sharp)");
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), title.c_str());
    update_viewer_navigation_state(ctx);
}

static void on_previous_clicked(GtkButton* button, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    if (currentIndex > 0) {
        const ResultData* prevResult = ctx->callbacks.visibleAt(currentIndex - 1);
        if (prevResult) {
            ctx->filename = prevResult->filename;
            ctx->isBlurry = prevResult->isBlurry;
            load_current_image(ctx);
            if (ctx->callbacks.selectVisibleRow) ctx->callbacks.selectVisibleRow(currentIndex - 1);
        }
    }
}

static void on_next_clicked(GtkButton* button, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    if (currentIndex >= 0) {
        const ResultData* nextResult = ctx->callbacks.visibleAt(currentIndex + 1);
        if (nextResult) {
            ctx->filename = nextResult->filename;
            ctx->isBlurry = nextResult->isBlurry;
            load_current_image(ctx);
            if (ctx->callbacks.selectVisibleRow) ctx->callbacks.selectVisibleRow(currentIndex + 1);
        }
    }
}

static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(ctx->viewer_window);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Left) {
        if (gtk_widget_get_sensitive(ctx->previous_button)) on_previous_clicked(NULL, ctx);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Right) {
        if (gtk_widget_get_sensitive(ctx->next_button)) on_next_clicked(NULL, ctx);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
        if (ctx->callbacks.deleteByFilename) {
            int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
            
            if (ctx->callbacks.deleteByFilename(ctx->filename, GTK_WINDOW(ctx->viewer_window))) {
                const ResultData* newResult = ctx->callbacks.visibleAt(currentIndex);
                if (!newResult && currentIndex > 0) {
                    newResult = ctx->callbacks.visibleAt(currentIndex - 1); 
                }
                
                if (newResult) {
                    ctx->filename = newResult->filename;
                    ctx->isBlurry = newResult->isBlurry;
                    load_current_image(ctx);
                    if (ctx->callbacks.selectVisibleRow) {
                        ctx->callbacks.selectVisibleRow(ctx->callbacks.visibleIndexForFilename(ctx->filename));
                    }
                } else {
                    gtk_widget_destroy(ctx->viewer_window);
                }
            }
        }
        return TRUE;
    } else if (event->keyval == GDK_KEY_F11) {
        if (ctx->is_fullscreen) {
            gtk_window_unfullscreen(GTK_WINDOW(ctx->viewer_window));
            ctx->is_fullscreen = false;
        } else {
            gtk_window_fullscreen(GTK_WINDOW(ctx->viewer_window));
            ctx->is_fullscreen = true;
        }
        return TRUE;
    }
    return FALSE;
}

static void update_viewer_navigation_state(ImageContext* ctx) {
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    bool hasPrev = currentIndex > 0;
    bool hasNext = currentIndex >= 0 && ctx->callbacks.visibleAt(currentIndex + 1) != nullptr;
    gtk_widget_set_sensitive(ctx->previous_button, hasPrev);
    gtk_widget_set_sensitive(ctx->next_button, hasNext);
}

void open_image_viewer(GtkWindow* parent, const ResultData& result, ImageViewerCallbacks callbacks) {
    ImageContext* ctx = new ImageContext();
    ctx->filename = result.filename;
    ctx->isBlurry = result.isBlurry;
    ctx->callbacks = callbacks;

    ctx->viewer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(ctx->viewer_window), parent);
    // Allow window to be resized
    gtk_window_set_resizable(GTK_WINDOW(ctx->viewer_window), TRUE);

    // Explicitly enable window decorations (title bar with buttons)
    gtk_window_set_decorated(GTK_WINDOW(ctx->viewer_window), TRUE);

    // Set default size, this size will be used when the window is un-maximized
    gtk_window_set_default_size(GTK_WINDOW(ctx->viewer_window), 1024, 768);
    
    // Maximize window by default
    gtk_window_maximize(GTK_WINDOW(ctx->viewer_window));

    g_signal_connect(ctx->viewer_window, "destroy", G_CALLBACK(on_viewer_destroy), ctx);
    g_signal_connect(ctx->viewer_window, "key-press-event", G_CALLBACK(on_viewer_key_press), ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx->viewer_window), vbox);

    ctx->drawing_area = gtk_drawing_area_new();
    g_signal_connect(ctx->drawing_area, "draw", G_CALLBACK(on_draw), ctx);

    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), ctx->drawing_area);
    
    gtk_widget_set_hexpand(event_box, TRUE);
    gtk_widget_set_vexpand(event_box, TRUE);
    gtk_widget_set_halign(event_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(event_box, GTK_ALIGN_CENTER);

    gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);

    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_button_press), ctx);
    g_signal_connect(event_box, "button-release-event", G_CALLBACK(on_button_release), ctx);
    g_signal_connect(event_box, "motion-notify-event", G_CALLBACK(on_motion_notify), ctx);
    g_signal_connect(event_box, "scroll-event", G_CALLBACK(on_viewer_scroll), ctx);

    ctx->scrolled = gtk_scrolled_window_new(NULL, NULL);
    GtkStyleContext *scrolled_style = gtk_widget_get_style_context(ctx->scrolled);
    gtk_style_context_add_class(scrolled_style, "view");

    g_signal_connect(ctx->scrolled, "size-allocate", G_CALLBACK(on_scrolled_size_allocate), ctx);

    gtk_container_add(GTK_CONTAINER(ctx->scrolled), event_box);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->scrolled, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    ctx->previous_button = gtk_button_new_with_label("Previous");
    g_signal_connect(ctx->previous_button, "clicked", G_CALLBACK(on_previous_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->previous_button, TRUE, TRUE, 0);

    ctx->next_button = gtk_button_new_with_label("Next");
    g_signal_connect(ctx->next_button, "clicked", G_CALLBACK(on_next_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->next_button, TRUE, TRUE, 0);

    load_current_image(ctx);
    gtk_widget_show_all(ctx->viewer_window);
}