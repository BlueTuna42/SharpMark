#include "delete_blurry_action.h"

#include "photo_trash.h"

DeleteBlurryResult delete_blurry_photos(GtkWindow* parent, const std::vector<std::string>& blurryFiles) {
    DeleteBlurryResult result;

    if (blurryFiles.empty()) {
        return result;
    }

    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Move blurry photos to trash?\n%d file(s) will be moved.",
                                               static_cast<int>(blurryFiles.size()));
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        return result;
    }

    result.confirmed = true;
    int failedCount = 0;
    std::string firstError;
    for (const std::string& filename : blurryFiles) {
        std::string errorMessage;
        if (trash_photo(parent, filename, &errorMessage)) {
            result.trashedFiles.push_back(filename);
        } else {
            ++failedCount;
            if (firstError.empty()) {
                firstError = errorMessage;
            }
        }
    }

    if (failedCount > 0) {
        GtkWidget *err_dialog = gtk_message_dialog_new(parent,
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       "Failed to move %d blurry photo(s) to trash.\n%s",
                                                       failedCount,
                                                       firstError.empty() ? "Unknown error" : firstError.c_str());
        gtk_dialog_run(GTK_DIALOG(err_dialog));
        gtk_widget_destroy(err_dialog);
    }

    return result;
}
