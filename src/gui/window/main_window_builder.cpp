#include "main_window_builder.h"

static GtkWidget* create_main_window(GUIContext& ctx, const MainWindowCallbacks& callbacks) {
    ctx.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx.window), "SharpMark");
    gtk_window_set_default_size(GTK_WINDOW(ctx.window), 800, 600);
    g_signal_connect(ctx.window, "destroy", callbacks.windowDestroy, NULL);

    // add icon
    gtk_window_set_default_icon_name("icon");
    gtk_window_set_icon_name(GTK_WINDOW(ctx.window), "icon");
    GError* error = nullptr;
    /*
    if (!gtk_window_set_default_icon_from_file("../assets/icons/512x512/icon.png", &error)) {
        g_warning("Failed to load local icon: %s", error->message);
        g_clear_error(&error);
    }
    */

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(ctx.window), vbox);
    return vbox;
}

static void build_top_bar(GUIContext& ctx, GtkWidget* vbox, const MainWindowCallbacks& callbacks) {
    ctx.top_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), ctx.top_button_box, FALSE, FALSE, 5);

    ctx.button_select = gtk_button_new_with_label("Select folder for analysis");
    gtk_widget_set_hexpand(ctx.button_select, TRUE);
    g_signal_connect(ctx.button_select, "clicked", callbacks.selectClicked, NULL);
    g_signal_connect(ctx.button_select, "key-press-event", callbacks.selectKeyPress, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.top_button_box), ctx.button_select, TRUE, TRUE, 0);

    ctx.button_recheck = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(ctx.button_recheck, "Recheck the same folder");
    gtk_widget_set_no_show_all(ctx.button_recheck, TRUE);
    gtk_widget_set_sensitive(ctx.button_recheck, FALSE);
    g_signal_connect(ctx.button_recheck, "clicked", callbacks.recheckClicked, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.top_button_box), ctx.button_recheck, FALSE, FALSE, 0);
    gtk_widget_hide(ctx.button_recheck);

    ctx.button_settings = gtk_button_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(ctx.button_settings, "Settings");
    g_signal_connect(ctx.button_settings, "clicked", callbacks.settingsClicked, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.top_button_box), ctx.button_settings, FALSE, FALSE, 0);
}

static void build_progress_bar(GUIContext& ctx, GtkWidget* vbox) {
    ctx.progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ctx.progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx.progress_bar), "0 / 0");
    gtk_box_pack_start(GTK_BOX(vbox), ctx.progress_bar, FALSE, FALSE, 5);
    gtk_widget_set_no_show_all(ctx.progress_bar, TRUE);
    gtk_widget_hide(ctx.progress_bar);
}

static void add_sort_options(GtkWidget* combo) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Default");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sharp First");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Blurry First");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Sharp Only");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Blurry Only");
}

static void build_directory_bar(GUIContext& ctx, GtkWidget* vbox, const MainWindowCallbacks& callbacks) {
    ctx.directory_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_no_show_all(ctx.directory_box, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), ctx.directory_box, FALSE, FALSE, 0);

    ctx.folder_label = gtk_label_new(NULL);
    gtk_widget_set_halign(ctx.folder_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(ctx.folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(ctx.folder_label, TRUE);
    gtk_widget_set_no_show_all(ctx.folder_label, TRUE);
    gtk_box_pack_start(GTK_BOX(ctx.directory_box), ctx.folder_label, TRUE, TRUE, 0);

    ctx.sort_combo = gtk_combo_box_text_new();
    add_sort_options(ctx.sort_combo);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx.sort_combo), static_cast<gint>(ctx.sortMode));
    gtk_widget_set_no_show_all(ctx.sort_combo, TRUE);
    g_signal_connect(ctx.sort_combo, "changed", callbacks.sortChanged, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.directory_box), ctx.sort_combo, FALSE, FALSE, 0);

    ctx.view_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ctx.view_mode_combo), "List");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ctx.view_mode_combo), "Grid");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx.view_mode_combo), static_cast<gint>(ctx.viewMode));
    gtk_widget_set_no_show_all(ctx.view_mode_combo, TRUE);
    g_signal_connect(ctx.view_mode_combo, "changed", callbacks.viewModeChanged, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.directory_box), ctx.view_mode_combo, FALSE, FALSE, 0);

    ctx.button_delete_blurry = gtk_button_new_with_label("Delete blurry");
    gtk_widget_set_sensitive(ctx.button_delete_blurry, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx.button_delete_blurry), "delete-button");
    g_signal_connect(ctx.button_delete_blurry, "clicked", callbacks.deleteBlurryClicked, NULL);
    gtk_box_pack_start(GTK_BOX(ctx.directory_box), ctx.button_delete_blurry, FALSE, FALSE, 0);

    gtk_widget_hide(ctx.directory_box);
}

static void update_selection_css(GtkSettings *settings, GParamSpec *pspec, gpointer user_data) {
    GtkCssProvider *provider = GTK_CSS_PROVIDER(user_data);
    gboolean prefer_dark = FALSE;
    gchar *theme_name = nullptr;
    
    g_object_get(settings, 
                 "gtk-application-prefer-dark-theme", &prefer_dark, 
                 "gtk-theme-name", &theme_name, 
                 NULL);
    
    bool is_dark = prefer_dark;
    if (theme_name) {
        std::string tName(theme_name);
        if (tName.length() >= 5) {
            std::string suffix = tName.substr(tName.length() - 5);
            if (suffix == "-dark" || suffix == "-Dark") {
                is_dark = true;
            }
        }
        g_free(theme_name);
    }

    const char* css = is_dark ? 
        "#results-flow-box flowboxchild:selected,\n"
        "#results-list-box row:selected {\n"
        "  background-color: #f4f4f4;\n" 
        "  color: #000000;\n"            
        "  border-radius: 6px;\n"        
        "}\n" :
        "#results-flow-box flowboxchild:selected,\n"
        "#results-list-box row:selected {\n"
        "  background-color: #d0d0d0;\n" 
        "  color: #000000;\n"            
        "  border-radius: 6px;\n"        
        "}\n";
        
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
}

static void build_results_area(GUIContext& ctx, GtkWidget* vbox, const MainWindowCallbacks& callbacks) {
    ctx.list_overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(vbox), ctx.list_overlay, TRUE, TRUE, 0);

    ctx.list_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ctx.list_scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    
    gtk_container_add(GTK_CONTAINER(ctx.list_overlay), ctx.list_scrolled_window);
    gtk_widget_add_events(ctx.list_scrolled_window, GDK_SCROLL_MASK);
    g_signal_connect(ctx.list_scrolled_window, "scroll-event", callbacks.listScrollEvent, NULL);
    GtkWidget *view_stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ctx.list_scrolled_window), view_stack);

    ctx.list_box = gtk_list_box_new();
    gtk_widget_set_name(ctx.list_box, "results-list-box");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx.list_box), GTK_SELECTION_MULTIPLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ctx.list_box), FALSE);
    g_signal_connect(ctx.list_box, "row-activated", callbacks.resultRowActivated, NULL);
    g_signal_connect(ctx.list_box, "key-press-event", callbacks.resultListKeyPress, NULL);
    gtk_box_pack_start(GTK_BOX(view_stack), ctx.list_box, TRUE, TRUE, 0);

    ctx.flow_box = gtk_flow_box_new();
    gtk_widget_set_name(ctx.flow_box, "results-flow-box");
    gtk_widget_set_valign(ctx.flow_box, GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ctx.flow_box), 20);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ctx.flow_box), GTK_SELECTION_MULTIPLE);
    gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(ctx.flow_box), FALSE);
    g_signal_connect(ctx.flow_box, "child-activated", callbacks.flowBoxChildActivated, NULL);
    gtk_box_pack_start(GTK_BOX(view_stack), ctx.flow_box, TRUE, TRUE, 0);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                                              
    GtkSettings *settings = gtk_settings_get_default();
    update_selection_css(settings, NULL, provider); // Apply initial CSS based on current settings
    
    // Connect signals to listen for theme changes
    g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(update_selection_css), provider);
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(update_selection_css), provider);
    
    g_object_unref(provider);

    gtk_widget_set_no_show_all(ctx.flow_box, TRUE);

    ctx.empty_results_label = gtk_label_new("No photos found in this folder");
    gtk_widget_set_halign(ctx.empty_results_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ctx.empty_results_label, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(ctx.empty_results_label, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(ctx.list_overlay), ctx.empty_results_label);
    gtk_widget_hide(ctx.empty_results_label);
}

static void build_summary_area(GUIContext& ctx, GtkWidget* vbox, const MainWindowCallbacks& callbacks) {
    ctx.summary_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_no_show_all(ctx.summary_box, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), ctx.summary_box, FALSE, FALSE, 5);

    ctx.summary_sharp_label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(ctx.summary_sharp_label), 5);
    gtk_box_pack_start(GTK_BOX(ctx.summary_box), ctx.summary_sharp_label, FALSE, FALSE, 0);

    ctx.summary_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(ctx.summary_bar, -1, 8);
    g_signal_connect(ctx.summary_bar, "draw", callbacks.summaryDraw, &ctx);
    gtk_box_pack_start(GTK_BOX(ctx.summary_box), ctx.summary_bar, TRUE, TRUE, 0);

    ctx.summary_blurry_label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(ctx.summary_blurry_label), 5);
    gtk_box_pack_start(GTK_BOX(ctx.summary_box), ctx.summary_blurry_label, FALSE, FALSE, 0);
    gtk_widget_hide(ctx.summary_box);
}

void build_main_window(GUIContext& ctx, const MainWindowCallbacks& callbacks) {
    GtkWidget *vbox = create_main_window(ctx, callbacks);
    build_top_bar(ctx, vbox, callbacks);
    build_progress_bar(ctx, vbox);
    build_directory_bar(ctx, vbox, callbacks);
    build_results_area(ctx, vbox, callbacks);
    build_summary_area(ctx, vbox, callbacks);
}
