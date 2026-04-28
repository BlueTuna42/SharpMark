#include "app_style.h"

#include <gtk/gtk.h>

void install_app_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "button.delete-button, #delete-photo-button {"
        "  background-image: none;"
        "  background-color: #8b1e3f;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.delete-button:hover, #delete-photo-button:hover {"
        "  background-image: none;"
        "  background-color: #9f274c;"
        "}"
        "button.delete-button:active, #delete-photo-button:active {"
        "  background-image: none;"
        "  background-color: #6f1832;"
        "}"
        "button.delete-button:disabled, #delete-photo-button:disabled {"
        "  background-image: none;"
        "  background-color: #4a4a4a;"
        "  color: #b8b8b8;"
        "  border-color: #3a3a3a;"
        "}"
        "button.delete-button label, #delete-photo-button label {"
        "  color: white;"
        "}"
        "button.delete-button:disabled label, #delete-photo-button:disabled label {"
        "  color: #b8b8b8;"
        "}"
        "button.subtle-delete-button {"
        "  background-image: none;"
        "  background-color: transparent;"
        "  color: #8b1e3f;"
        "  border-color: #c8cdd2;"
        "}"
        "button.subtle-delete-button:hover {"
        "  background-image: none;"
        "  background-color: #8b1e3f;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.subtle-delete-button:active {"
        "  background-image: none;"
        "  background-color: #6f1832;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.subtle-delete-button label {"
        "  color: #8b1e3f;"
        "}"
        "button.subtle-delete-button:hover label, button.subtle-delete-button:active label {"
        "  color: white;"
        "}"
        "list row:selected, list row:selected:focus {"
        "  background-image: none;"
        "  background-color: #eceff1;"
        "  color: inherit;"
        "}"
        "list row:focus {"
        "  outline-color: #9aa0a6;"
        "  outline-style: solid;"
        "  outline-width: 1px;"
        "  outline-offset: -1px;"
        "}"
        "progressbar text {"
        "  color: #f2f2f2;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}",
        -1,
        NULL);

    GdkScreen* screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(screen,
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_USER);
    }
    g_object_unref(provider);
}
