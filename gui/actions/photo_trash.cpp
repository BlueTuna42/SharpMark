#include "photo_trash.h"

bool confirm_and_trash_photo(GtkWindow* parent, const std::string& filename) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Move photo to trash?\n%s",
                                               filename.c_str());

    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        return false;
    }

    GFile *file = g_file_new_for_path(filename.c_str());
    GError *error = NULL;
    const gboolean trashed = g_file_trash(file, NULL, &error);
    g_object_unref(file);

    if (trashed) {
        return true;
    }

    GtkWidget *err_dialog = gtk_message_dialog_new(parent,
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "Failed to move file to trash.\n%s",
                                                   error ? error->message : "Unknown error");
    gtk_dialog_run(GTK_DIALOG(err_dialog));
    gtk_widget_destroy(err_dialog);
    if (error) {
        g_error_free(error);
    }
    return false;
}

bool trash_photo(GtkWindow* parent, const std::string& filename, std::string* errorMessage) {
    GFile *file = g_file_new_for_path(filename.c_str());
    GError *error = NULL;
    const gboolean trashed = g_file_trash(file, NULL, &error);
    g_object_unref(file);

    if (trashed) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = error ? error->message : "Unknown error";
    }

    if (error) {
        g_error_free(error);
    }
    return false;
}
