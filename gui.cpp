#include "gui.h"
#include <gtk/gtk.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdio> // Required for std::remove

struct ResultData {
    std::string filename;
    bool isBlurry;
};

struct GUIContext {
    GtkWidget *window = nullptr;
    GtkWidget *tree_view = nullptr;
    GtkListStore *list_store = nullptr;
    GtkWidget *button_select = nullptr;
    
    std::mutex mtx;
    std::condition_variable cv;
    std::string selectedDir = "";
    bool dirSelected = false;
    
    std::thread gtkThread;
};

static GUIContext* g_ctx = nullptr;

// Context to handle the individual image viewer window
struct ImageContext {
    std::string filename;
    GtkTreeRowReference *row_ref;
    GtkWidget *viewer_window;
};

// --- Image Viewer Callbacks ---

static void on_viewer_destroy(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    if (ctx->row_ref) {
        gtk_tree_row_reference_free(ctx->row_ref);
    }
    delete ctx;
}

static void on_delete_clicked(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    
    // Create confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->viewer_window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Are you sure you want to move this photo to the trash?\n%s",
                                               ctx->filename.c_str());
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        GFile *file = g_file_new_for_path(ctx->filename.c_str());
        GError *error = NULL;

        // Move file to the system trash instead of permanent deletion
        if (g_file_trash(file, NULL, &error)) {
            // Remove the item from the list store
            if (gtk_tree_row_reference_valid(ctx->row_ref)) {
                GtkTreePath* path = gtk_tree_row_reference_get_path(ctx->row_ref);
                GtkTreeIter iter;
                if (gtk_tree_model_get_iter(GTK_TREE_MODEL(g_ctx->list_store), &iter, path)) {
                    gtk_list_store_remove(g_ctx->list_store, &iter);
                }
                gtk_tree_path_free(path);
            }
            // Close the viewer window
            gtk_widget_destroy(ctx->viewer_window);
        } else {
            // Failed to move to trash
            GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->viewer_window),
                                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                                           GTK_MESSAGE_ERROR,
                                                           GTK_BUTTONS_OK,
                                                           "Failed to move file to trash.\n%s",
                                                           error->message);
            gtk_dialog_run(GTK_DIALOG(err_dialog));
            gtk_widget_destroy(err_dialog);
            g_error_free(error);
        }
        g_object_unref(file);
    }
}

// Opens the full-size viewer when a row is double-clicked
static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(g_ctx->list_store), &iter, path)) return;

    gchar *filename = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(g_ctx->list_store), &iter, 1, &filename, -1);
    
    if (!filename) return;

    ImageContext* ctx = new ImageContext();
    ctx->filename = filename;
    ctx->row_ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(g_ctx->list_store), path);
    g_free(filename);

    // Create the viewer window
    ctx->viewer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), "Image Viewer");
    gtk_window_set_default_size(GTK_WINDOW(ctx->viewer_window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(ctx->viewer_window), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_transient_for(GTK_WINDOW(ctx->viewer_window), GTK_WINDOW(g_ctx->window));

    // Ensure memory is freed when window is closed
    g_signal_connect(ctx->viewer_window, "destroy", G_CALLBACK(on_viewer_destroy), ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(ctx->viewer_window), vbox);

    // Load full image, scaled to a maximum reasonable size to prevent massive windows
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_size(ctx->filename.c_str(), 1024, 768, &error);
    if (error) {
        g_error_free(error);
    }
    
    GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
    if (pixbuf) g_object_unref(pixbuf);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), image);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    // Delete Button
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Photo");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(vbox), btn_delete, FALSE, FALSE, 5);

    gtk_widget_show_all(ctx->viewer_window);
}

// --- Main GUI Callbacks ---

static gboolean on_file_processed(gpointer data) {
    ResultData* res = static_cast<ResultData*>(data);
    GtkTreeIter iter;
    gtk_list_store_append(g_ctx->list_store, &iter);
    
    const char* color = res->isBlurry ? "red" : "forestgreen";
    
    // Load image thumbnail (100x100), keeping aspect ratio. 
    // Returns NULL if the file format is not supported by GTK out-of-the-box (e.g. some RAWs), which is safe.
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(res->filename.c_str(), 100, 100, TRUE, &error);
    
    if (error) {
        g_error_free(error);
    }

    // Insert data into 3 columns: 0 (Image), 1 (Filename), 2 (Color)
    gtk_list_store_set(g_ctx->list_store, &iter, 
                       0, pixbuf, 
                       1, res->filename.c_str(), 
                       2, color, 
                       -1);
    
    // Free the pixbuf object as the list_store now holds a reference to it
    if (pixbuf) {
        g_object_unref(pixbuf);
    }
    
    // Autoscroll to the newly added row
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(g_ctx->list_store), &iter);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(g_ctx->tree_view), path, NULL, FALSE, 0.0, 0.0);
    gtk_tree_path_free(path);

    delete res;
    return G_SOURCE_REMOVE;
}

static gboolean on_scan_finished(gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_ctx->window), GTK_DIALOG_DESTROY_WITH_PARENT, 
                                               GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Analysis complete! Double-click a file to preview/delete.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return G_SOURCE_REMOVE;
}

static void on_window_destroy(GtkWidget* widget, gpointer data) {
    if (!g_ctx->dirSelected) {
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        g_ctx->dirSelected = true;
        g_ctx->cv.notify_all();
    }
    gtk_main_quit();
}

static void on_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Directory", GTK_WINDOW(g_ctx->window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Select", GTK_RESPONSE_ACCEPT, NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_set_sensitive(g_ctx->button_select, FALSE);
        
        std::string title = std::string("Focus Checker - Analysis: ") + filename;
        gtk_window_set_title(GTK_WINDOW(g_ctx->window), title.c_str());
        
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        g_ctx->selectedDir = filename;
        g_ctx->dirSelected = true;
        g_ctx->cv.notify_all();
        
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void GtkThreadLoop() {
    int argc = 0; 
    char **argv = nullptr;
    gtk_init(&argc, &argv);
    
    g_ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_ctx->window), "Focus Checker");
    gtk_window_set_default_size(GTK_WINDOW(g_ctx->window), 800, 600);
    g_signal_connect(g_ctx->window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(g_ctx->window), vbox);

    g_ctx->button_select = gtk_button_new_with_label("Select directory for analysis");
    g_signal_connect(g_ctx->button_select, "clicked", G_CALLBACK(on_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->button_select, FALSE, FALSE, 5);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // Changed ListStore to 3 columns: Image (Pixbuf), Text (String), Color (String)
    g_ctx->list_store = gtk_list_store_new(3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);
    g_ctx->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_ctx->list_store));
    
    // Attach double-click signal to open viewer
    g_signal_connect(g_ctx->tree_view, "row-activated", G_CALLBACK(on_row_activated), NULL);
    
    // Column 1: Image Preview
    GtkCellRenderer *renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
    GtkTreeViewColumn *column_pixbuf = gtk_tree_view_column_new_with_attributes("Preview", renderer_pixbuf, "pixbuf", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(g_ctx->tree_view), column_pixbuf);

    // Column 2: Filename
    GtkCellRenderer *renderer_text = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column_text = gtk_tree_view_column_new_with_attributes("Processed files (Double-click to open)", renderer_text, "text", 1, "foreground", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(g_ctx->tree_view), column_text);

    gtk_container_add(GTK_CONTAINER(scrolled_window), g_ctx->tree_view);

    gtk_widget_show_all(g_ctx->window);
    gtk_main();
}

VisualGUI::VisualGUI() {
    g_ctx = new GUIContext();
    g_ctx->gtkThread = std::thread(GtkThreadLoop);
}

VisualGUI::~VisualGUI() {
    delete g_ctx;
}

std::string VisualGUI::SelectDirectory() {
    if (!g_ctx) return "";
    std::unique_lock<std::mutex> lock(g_ctx->mtx);
    g_ctx->cv.wait(lock, []{ return g_ctx->dirSelected; });
    return g_ctx->selectedDir;
}

void VisualGUI::AddResult(const std::string& filename, bool isBlurry) {
    if (!g_ctx || !g_ctx->window) return;
    ResultData* res = new ResultData{filename, isBlurry};
    g_idle_add(on_file_processed, res); 
}

void VisualGUI::ShowFinished() {
    if (!g_ctx || !g_ctx->window) return;
    g_idle_add(on_scan_finished, nullptr);
    
    if (g_ctx->gtkThread.joinable()) {
        g_ctx->gtkThread.join();
    }
}