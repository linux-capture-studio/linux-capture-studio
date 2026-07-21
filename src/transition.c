#include <gtk/gtk.h>

typedef struct {
    GtkApplication *application;
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *spinner;
    GtkWidget *hint_label;
    gchar *status_path;
    guint missing_polls;
} TransitionWindow;

static gboolean
refresh_status(gpointer user_data)
{
    TransitionWindow *state = user_data;
    gchar *contents = NULL;

    if (!g_file_get_contents(state->status_path, &contents, NULL, NULL)) {
        state->missing_polls++;
        if (state->missing_polls >= 3) {
            g_application_quit(G_APPLICATION(state->application));
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    state->missing_polls = 0;
    g_strstrip(contents);
    if (contents[0] != '\0') {
        gtk_label_set_text(GTK_LABEL(state->status_label), contents);
        if (g_str_has_prefix(contents, "ERROR:")) {
            gtk_spinner_stop(GTK_SPINNER(state->spinner));
            gtk_label_set_text(
                GTK_LABEL(state->hint_label),
                "You may close this window, reconnect the capture card, and try again."
            );
        }
    }
    g_free(contents);
    return G_SOURCE_CONTINUE;
}

static void
on_activate(GtkApplication *application, gpointer user_data)
{
    TransitionWindow *state = user_data;
    GtkWidget *box;
    GtkWidget *spinner;
    GtkWidget *title;
    GtkWidget *hint;
    GtkCssProvider *provider;
    const gchar *css =
        "window { background: #111318; }"
        ".wait-root { padding: 34px; }"
        ".wait-title { color: #ffffff; font-size: 26px; font-weight: 700; }"
        ".wait-status { color: #e4e7ec; font-size: 17px; }"
        ".wait-hint { color: #9aa3b2; font-size: 13px; }"
        "spinner { color: #ffffff; min-width: 52px; min-height: 52px; }";

    state->application = application;
    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Linux Capture Studio — Please wait");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 720, 300);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(state->window), TRUE);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(box, "wait-root");
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    spinner = gtk_spinner_new();
    state->spinner = spinner;
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), spinner);

    title = gtk_label_new("Please wait…");
    gtk_widget_add_css_class(title, "wait-title");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    state->status_label = gtk_label_new("Switching the capture card to native 4K60.");
    gtk_widget_add_css_class(state->status_label, "wait-status");
    gtk_label_set_wrap(GTK_LABEL(state->status_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(state->status_label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), state->status_label);

    hint = gtk_label_new("The capture card may disappear briefly. Linux Capture Studio will reopen automatically.");
    state->hint_label = hint;
    gtk_widget_add_css_class(hint, "wait-hint");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), hint);

    gtk_window_set_child(GTK_WINDOW(state->window), box);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(state->window),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    refresh_status(state);
    g_timeout_add(250, refresh_status, state);
    gtk_window_present(GTK_WINDOW(state->window));
}

int
main(int argc, char **argv)
{
    TransitionWindow state = {0};
    GtkApplication *application;
    int result;

    if (argc != 2 || argv[1] == NULL || argv[1][0] == '\0') {
        g_printerr("Usage: linux-capture-studio-wait STATUS_FILE\n");
        return 2;
    }

    state.status_path = g_strdup(argv[1]);
    application = gtk_application_new(
        "io.github.linuxcapturestudio.LinuxCaptureStudio.Wait",
        G_APPLICATION_NON_UNIQUE
    );
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), &state);
    {
        char *application_argv[] = { argv[0], NULL };
        result = g_application_run(G_APPLICATION(application), 1, application_argv);
    }
    g_object_unref(application);
    g_free(state.status_path);
    return result;
}
