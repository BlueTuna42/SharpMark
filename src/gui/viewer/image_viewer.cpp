#include "image_viewer.h"

#include "../utils/path_utils.h"
#include "preview_loader.h"
#include "../../gui/gui.h"
#include "../../img_tools/laplacian.h"
#include "../../processors/aesthetic_scorer.h"
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <libraw/libraw.h>
#include <fstream>
#include <sstream>
#include <iomanip>

static constexpr double kMinZoomLevel = 0.01;
static constexpr double kMaxZoomLevel = 2.0;

// Laplacian to Pixbuf Rendering
// Converts a raw float Laplacian matrix into an 8-bit visible image
static GdkPixbuf* render_laplacian_to_pixbuf(const GrayscaleImage& lapImg) {
    int w = lapImg.width;
    int h = lapImg.height;
    
    GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    if (!pixbuf) return nullptr;
    
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    
    float maxVal = 0.001f;
    int totalPixels = w * h;
    for (int i = 0; i < totalPixels; ++i) {
        float val = std::abs(lapImg.data[i]);
        if (val > maxVal) maxVal = val;
    }
    
    // Threshold set low to ensure faint focus edges are brightly visible
    float viewThreshold = maxVal * 0.15f; 
    
    for (int y = 0; y < h; ++y) {
        guchar* row = pixels + y * rowstride;
        int imgOffset = y * w;
        for (int x = 0; x < w; ++x) {
            float val = std::abs(lapImg.data[imgOffset + x]);
            
            float normalized = (val / viewThreshold) * 255.0f;
            if (normalized > 255.0f) normalized = 255.0f;
            
            guchar pixelVal = static_cast<guchar>(normalized);
            row[x * 3 + 0] = pixelVal;
            row[x * 3 + 1] = pixelVal;
            row[x * 3 + 2] = pixelVal;
        }
    }
    return pixbuf;
}

// Attempts to find and load the .rawlap cache file for the given image
static std::filesystem::path laplacian_cache_file_path(const std::string& originalFilepath) {
#ifdef _WIN32
    std::filesystem::path origPath = std::filesystem::u8path(originalFilepath);
#else
    std::filesystem::path origPath(originalFilepath);
#endif
    return origPath.parent_path() / ".laplacian_cache" / (origPath.filename().string() + ".rawlap");
}

static bool has_laplacian_cache(const std::string& originalFilepath) {
    std::error_code ec;
    return std::filesystem::is_regular_file(laplacian_cache_file_path(originalFilepath), ec);
}

static GdkPixbuf* load_laplacian_pixbuf(const std::string& originalFilepath) {
    std::filesystem::path cacheFile = laplacian_cache_file_path(originalFilepath);

    if (std::filesystem::exists(cacheFile)) {
        auto lapImg = LaplacianProcessor::loadLaplacian(cacheFile.string());
        if (lapImg) {
            return render_laplacian_to_pixbuf(*lapImg);
        }
    }
    return nullptr;
}

// Viewer Implementation
struct ImageContext {
    std::string filename;
    bool isBlurry = false;
    GtkWidget *viewer_window = nullptr;
    GtkWidget *drawing_area = nullptr; 
    GtkWidget *scrolled = nullptr; 
    GtkWidget *previous_button = nullptr;
    GtkWidget *next_button = nullptr;
    GtkWidget *laplacian_toggle = nullptr;
    GtkWidget *sidebar_revealer = nullptr; 
    GtkWidget *sidebar_box = nullptr;      
    GtkWidget *histogram_area = nullptr;   
    GtkWidget *info_label = nullptr;       
    GtkWidget *sidebar_toggle_btn = nullptr; 
    ImageViewerCallbacks callbacks;

    GdkPixbuf *original_pixbuf = nullptr; 
    double zoom_level = 1.0;
    
    bool show_laplacian = false;
    bool updating_laplacian_toggle = false;

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

    int original_w = 0;
    int original_h = 0; 

    int rawMode = 0;
};

static void update_viewer_navigation_state(ImageContext* ctx);
static void load_current_image(ImageContext* ctx);
static void reset_zoom_to_fit(ImageContext* ctx);
static void apply_zoom_sync(ImageContext* ctx);

static void train_ai_from_viewer(ImageContext* ctx, bool isGoodPhoto) {
    if (!ctx) return;
    
    if (ctx->callbacks.trainAI) {
        ctx->callbacks.trainAI(ctx->filename, isGoodPhoto);
        g_print("AI training signal sent! (%s)\n", isGoodPhoto ? "Good" : "Bad");
    }
}

static void update_laplacian_toggle_state(ImageContext* ctx) {
    if (!ctx->laplacian_toggle) {
        return;
    }

    const bool hasCache = has_laplacian_cache(ctx->filename);
    gtk_widget_set_sensitive(ctx->laplacian_toggle, hasCache);
    gtk_widget_set_tooltip_text(ctx->laplacian_toggle,
                                hasCache ? "Show cached Laplacian edge map"
                                         : "Laplacian cache is not available for this photo");

    if (!hasCache && ctx->show_laplacian) {
        ctx->show_laplacian = false;
        ctx->updating_laplacian_toggle = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->laplacian_toggle), FALSE);
        ctx->updating_laplacian_toggle = false;
    }
}

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

static void set_zoom_pivot_to_view_center(ImageContext* ctx) {
    GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));

    GtkAllocation allocation;
    gtk_widget_get_allocation(ctx->scrolled, &allocation);

    ctx->mouse_view_x = allocation.width / 2.0;
    ctx->mouse_view_y = allocation.height / 2.0;
    ctx->pivot_orig_x = (gtk_adjustment_get_value(hadj) + ctx->mouse_view_x) / ctx->zoom_level;
    ctx->pivot_orig_y = (gtk_adjustment_get_value(vadj) + ctx->mouse_view_y) / ctx->zoom_level;
}

static void zoom_viewer(ImageContext* ctx, double factor) {
    if (!ctx->original_pixbuf) {
        return;
    }

    set_zoom_pivot_to_view_center(ctx);
    ctx->zoom_level = std::clamp(ctx->zoom_level * factor, kMinZoomLevel, kMaxZoomLevel);
    apply_zoom_sync(ctx);
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

        ctx->zoom_level = std::clamp(ctx->zoom_level, kMinZoomLevel, kMaxZoomLevel);
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
static std::string extract_image_info(const std::string& filepath) {
    std::ostringstream ss;
    
    // Try to load varience from cahce
    double variance = -1.0;
    std::filesystem::path p(filepath);
    std::filesystem::path cacheFile = p.parent_path() / ".laplacian_cache" / "state.csv";
    if (std::filesystem::exists(cacheFile)) {
        std::ifstream in(cacheFile);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream iss(line);
            std::string path, blurry, varStr;
             if (std::getline(iss, path, ',') && std::getline(iss, blurry, ',')) {
                if (path == filepath) {
                    if (std::getline(iss, varStr) && !varStr.empty()) {
                        variance = std::stod(varStr);
                    }
                    break;
                }
            }
        }
    }

    // Read EXIF using LibRaw
    libraw_data_t *lr = libraw_init(0);
    if (lr) {
        int open_result = 0;
#ifdef _WIN32
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wfilename = converter.from_bytes(filepath);
        open_result = libraw_open_wfile(lr, wfilename.c_str());
#else
        open_result = libraw_open_file(lr, filepath.c_str());
#endif
        if (open_result == LIBRAW_SUCCESS) {
            ss << "<b>Camera:</b> " << lr->idata.make << " " << lr->idata.model << "\n";
            ss << "<b>Lens:</b> " << lr->lens.Lens << "\n";
            ss << "<b>Date:</b> " << std::put_time(std::localtime(&lr->other.timestamp), "%Y-%m-%d %H:%M:%S") << "\n\n";
            ss << "<b>Aperture:</b> f/" << lr->other.aperture << "\n";
            ss << "<b>Shutter:</b> 1/" << (int)(1.0f / lr->other.shutter) << "s\n";
            ss << "<b>ISO:</b> " << lr->other.iso_speed << "\n";
            ss << "<b>Focal Length:</b> " << lr->other.focal_len << "mm\n";
        }
        libraw_close(lr);
    }

    if (variance >= 0.0) {
        ss << "\n<b>Laplacian Variance:</b> " << std::fixed << std::setprecision(2) << variance;
    }

    return ss.str();
}

static void load_current_image(ImageContext* ctx) {
    if (ctx->original_pixbuf) {
        g_object_unref(ctx->original_pixbuf);
        ctx->original_pixbuf = nullptr;
    }

    update_laplacian_toggle_state(ctx);

    // Try loading Laplacian map if toggled
    if (ctx->show_laplacian) {
        ctx->original_pixbuf = load_laplacian_pixbuf(ctx->filename);
        if (!ctx->original_pixbuf) {
            // Uncheck the toggle if the technical map file is missing
            ctx->updating_laplacian_toggle = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->laplacian_toggle), FALSE);
            ctx->updating_laplacian_toggle = false;
            ctx->show_laplacian = false;
            update_laplacian_toggle_state(ctx);
        }
    }

    // Default to the original image preview
    if (!ctx->show_laplacian) {
        ctx->original_pixbuf = load_preview_pixbuf(ctx->filename, 8192, 8192, ctx->rawMode); 
    }

    int original_w = 0;
    int original_h = 0;

    if (ctx->original_pixbuf) {
        original_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
        original_h = gdk_pixbuf_get_height(ctx->original_pixbuf);
        
        int dynamicBorder = std::max(8, std::min(original_w, original_h) / 100);

        ctx->original_pixbuf = add_status_border(ctx->original_pixbuf, ctx->isBlurry, dynamicBorder);

        reset_zoom_to_fit(ctx);
        apply_zoom_sync(ctx);
    } else {
        gtk_widget_queue_draw(ctx->drawing_area);
    }

    // Append technical tag to window title if Laplacian mode is active
    std::string title = path_filename(ctx->filename) + (ctx->isBlurry ? " (Blurry)" : " (Sharp)");
    if (ctx->show_laplacian) title += " [LAPLACIAN CACHE]";
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), title.c_str());
    update_viewer_navigation_state(ctx);

    if (ctx->info_label) {
        std::string filename_only = std::filesystem::path(ctx->filename).filename().string();
        std::string res_text = (ctx->original_w > 0) ? (std::to_string(ctx->original_w) + " x " + std::to_string(ctx->original_h)) : "Unknown";
        
        std::string ai_text = "N/A";
        if (ctx->callbacks.getAestheticScore) {
            double score = ctx->callbacks.getAestheticScore(ctx->filename);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.3f", score);
            ai_text = buf;
        }

        std::string info_text = 
            "<b>File:</b> " + filename_only + "\n"
            "<b>Status:</b> " + (ctx->isBlurry ? "<span foreground='red'>Blurry</span>" : "<span foreground='green'>Sharp</span>") + "\n"
            "<b>Resolution:</b> " + res_text + "\n"
            "<b>AI Score:</b> <span foreground='#00BFFF'>" + ai_text + "</span>\n\n"
            + extract_image_info(ctx->filename);

        gtk_label_set_markup(GTK_LABEL(ctx->info_label), info_text.c_str());
    }
}

static void on_previous_clicked(GtkButton* button, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    if (currentIndex > 0) {
        const ResultData* prevResult = ctx->callbacks.visibleAt(currentIndex - 1);
        if (prevResult) {
            ctx->filename = prevResult->filename;
            ctx->isBlurry = prevResult->isBlurry;
            ctx->original_w = prevResult->width;
            ctx->original_h = prevResult->height;
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
            ctx->original_w = nextResult->width;
            ctx->original_h = nextResult->height;
            load_current_image(ctx);
            if (ctx->callbacks.selectVisibleRow) ctx->callbacks.selectVisibleRow(currentIndex + 1);
        }
    }
}

static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if ((event->state & GDK_CONTROL_MASK) &&
        (event->keyval == GDK_KEY_plus ||
         event->keyval == GDK_KEY_equal ||
         event->keyval == GDK_KEY_KP_Add)) {
        zoom_viewer(ctx, 1.2);
        return TRUE;
    } else if ((event->state & GDK_CONTROL_MASK) &&
               (event->keyval == GDK_KEY_minus ||
                event->keyval == GDK_KEY_KP_Subtract)) {
        zoom_viewer(ctx, 1.0 / 1.2);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Escape) {
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
                    ctx->original_w = newResult->width;
                    ctx->original_h = newResult->height;
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

static gboolean on_histogram_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (!ctx->original_pixbuf) return FALSE;

    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_paint(cr);

    int w = gdk_pixbuf_get_width(ctx->original_pixbuf);
    int h = gdk_pixbuf_get_height(ctx->original_pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(ctx->original_pixbuf);
    guchar* pixels = gdk_pixbuf_get_pixels(ctx->original_pixbuf);

    int hist_r[256] = {0}, hist_g[256] = {0}, hist_b[256] = {0}, hist_l[256] = {0};
    int max_count = 0;

    // Boarder margin
    int margin = 50; 
    
    // 4 step for better performance
    for (int y = margin; y < h - margin; y += 4) {
        guchar* row = pixels + y * rowstride;
        for (int x = margin; x < w - margin; x += 4) {
            guchar r = row[x * 3];
            guchar g = row[x * 3 + 1];
            guchar b = row[x * 3 + 2];
            int luma = (r * 299 + g * 587 + b * 114) / 1000;
            
            hist_r[r]++;
            hist_g[g]++;
            hist_b[b]++;
            hist_l[luma]++;
            
            if (hist_l[luma] > max_count) max_count = hist_l[luma];
        }
    }

    if (max_count == 0) return FALSE;

    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);

    auto draw_channel = [&](int* hist, double r, double g, double b, double alpha) {
        cairo_set_source_rgba(cr, r, g, b, alpha);
        cairo_move_to(cr, 0, height);
        for (int i = 0; i < 256; ++i) {
            double x = (i / 255.0) * width;       
            double val = std::pow((double)hist[i] / max_count, 0.5); 
            double y = height - (val * height * 0.95);
            cairo_line_to(cr, x, y);
        }
        cairo_line_to(cr, width, height);
        cairo_close_path(cr);
        cairo_fill(cr);
    };

    draw_channel(hist_r, 1.0, 0.0, 0.0, 0.6);
    draw_channel(hist_g, 0.0, 1.0, 0.0, 0.6);
    draw_channel(hist_b, 0.0, 0.0, 1.0, 0.6);
    
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    draw_channel(hist_l, 1.0, 1.0, 1.0, 0.25);

    return FALSE;
}

void open_image_viewer(GtkWindow* parent, const ResultData& result, int rawMode, const ImageViewerCallbacks& callbacks) {
    ImageContext* ctx = new ImageContext();
    ctx->filename = result.filename;
    ctx->isBlurry = result.isBlurry;
    ctx->original_w = result.width;
    ctx->original_h = result.height;
    ctx->rawMode = rawMode;
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
    GtkWidget *hbox_main = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_main, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(hbox_main), ctx->scrolled, TRUE, TRUE, 0);

    GtkWidget *toggle_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(toggle_vbox, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(hbox_main), toggle_vbox, FALSE, FALSE, 0);

    ctx->sidebar_toggle_btn = gtk_toggle_button_new_with_label("▶");
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->sidebar_toggle_btn), "flat");
    gtk_widget_set_size_request(ctx->sidebar_toggle_btn, 24, 60);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->sidebar_toggle_btn), TRUE);
    gtk_box_pack_start(GTK_BOX(toggle_vbox), ctx->sidebar_toggle_btn, FALSE, FALSE, 0);

    ctx->sidebar_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(ctx->sidebar_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(ctx->sidebar_revealer), 250);
    gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->sidebar_revealer), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox_main), ctx->sidebar_revealer, FALSE, FALSE, 0);

    ctx->sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(ctx->sidebar_box, 300, -1);
    gtk_container_set_border_width(GTK_CONTAINER(ctx->sidebar_box), 15);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->sidebar_box), "background"); 
    gtk_container_add(GTK_CONTAINER(ctx->sidebar_revealer), ctx->sidebar_box);

    GtkWidget *sidebar_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(sidebar_title), "<b>Metadata & Analysis</b>");
    gtk_widget_set_halign(sidebar_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(ctx->sidebar_box), sidebar_title, FALSE, FALSE, 0);

    ctx->histogram_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(ctx->histogram_area, -1, 150);
    g_signal_connect(ctx->histogram_area, "draw", G_CALLBACK(on_histogram_draw), ctx);
    gtk_box_pack_start(GTK_BOX(ctx->sidebar_box), ctx->histogram_area, FALSE, FALSE, 10);

    ctx->info_label = gtk_label_new("");
    gtk_label_set_use_markup(GTK_LABEL(ctx->info_label), TRUE);
    gtk_widget_set_halign(ctx->info_label, GTK_ALIGN_START);
    gtk_widget_set_valign(ctx->info_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(ctx->info_label), TRUE);
    gtk_box_pack_start(GTK_BOX(ctx->sidebar_box), ctx->info_label, TRUE, TRUE, 0);
    
    g_signal_connect(ctx->sidebar_toggle_btn, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        ImageContext* ctx = static_cast<ImageContext*>(data);
        bool active = gtk_toggle_button_get_active(btn);
        gtk_button_set_label(GTK_BUTTON(btn), active ? "▶" : "◀");
        gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->sidebar_revealer), active);
    }), ctx);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    ctx->previous_button = gtk_button_new_with_label("Previous");
    g_signal_connect(ctx->previous_button, "clicked", G_CALLBACK(on_previous_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->previous_button, TRUE, TRUE, 0);

    GtkWidget* ai_good_btn = gtk_button_new_with_label("👍 Good");
    gtk_box_pack_start(GTK_BOX(button_box), ai_good_btn, FALSE, FALSE, 5);
    g_signal_connect(ai_good_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        ImageContext* ictx = static_cast<ImageContext*>(data);
        train_ai_from_viewer(ictx, true);
    }), ctx);

    GtkWidget* ai_bad_btn = gtk_button_new_with_label("👎 Bad");
    gtk_box_pack_start(GTK_BOX(button_box), ai_bad_btn, FALSE, FALSE, 5);
    g_signal_connect(ai_bad_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        ImageContext* ictx = static_cast<ImageContext*>(data);
        train_ai_from_viewer(ictx, false);
    }), ctx);
   /* 
    ctx->laplacian_toggle = gtk_toggle_button_new_with_label("Show Laplacian Edge Map");
    gtk_box_pack_start(GTK_BOX(button_box), ctx->laplacian_toggle, TRUE, TRUE, 0);
    g_signal_connect(ctx->laplacian_toggle, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        ImageContext* ctx = static_cast<ImageContext*>(data);
        if (ctx->updating_laplacian_toggle) {
            return;
        }
        ctx->show_laplacian = gtk_toggle_button_get_active(btn);
        load_current_image(ctx); // Triggers visual mode switch
    }), ctx);
    */
    ctx->next_button = gtk_button_new_with_label("Next");
    g_signal_connect(ctx->next_button, "clicked", G_CALLBACK(on_next_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->next_button, TRUE, TRUE, 0);

    load_current_image(ctx);
    gtk_widget_show_all(ctx->viewer_window);
}
