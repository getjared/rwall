#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#define WALLPAPER_DIR "/home/jared/wallpapers"
#define SUPPORTED_EXTENSIONS "jpg,png,jpeg,bmp,svg"
#define THUMBNAIL_SIZE 128
#define CACHE_DIR_NAME ".cache/rwall"
#define BACKGROUND_CACHE_DIR_NAME "backgrounds"

typedef struct {
    gchar *filepath;
    GtkWidget *image;
    GtkWidget *event_box;
} ImageData;

GList* get_image_files(const gchar *directory) {
    GList *image_files = NULL;
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (!dir) {
        g_printerr("Failed to open directory: %s\n", directory);
        return NULL;
    }

    const gchar *filename;
    while ((filename = g_dir_read_name(dir)) != NULL) {
        gchar *lower_filename = g_utf8_strdown(filename, -1);
        if (g_str_has_suffix(lower_filename, ".jpg") ||
            g_str_has_suffix(lower_filename, ".png") ||
            g_str_has_suffix(lower_filename, ".jpeg") ||
            g_str_has_suffix(lower_filename, ".bmp") ||
            g_str_has_suffix(lower_filename, ".svg")) {
            gchar *filepath = g_build_filename(directory, filename, NULL);
            image_files = g_list_append(image_files, filepath);
        }
        g_free(lower_filename);
    }

    g_dir_close(dir);
    return image_files;
}

GString* compute_md5(const gchar *str) {
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
    g_checksum_update(checksum, (const guchar*)str, strlen(str));
    gchar *checksum_str = g_strdup(g_checksum_get_string(checksum));
    GString *result = g_string_new(checksum_str);
    g_free(checksum_str);
    g_checksum_free(checksum);
    return result;
}

gchar* get_cache_dir() {
    const gchar *home = g_get_home_dir();
    gchar *cache_dir = g_build_filename(home, CACHE_DIR_NAME, NULL);
    if (!g_file_test(cache_dir, G_FILE_TEST_IS_DIR)) {
        if (g_mkdir_with_parents(cache_dir, 0755) != 0) {
            g_printerr("Failed to create cache directory: %s\n", cache_dir);
            g_free(cache_dir);
            return NULL;
        }
    }
    return cache_dir;
}

gchar* get_background_cache_dir(const gchar *cache_dir) {
    gchar *background_cache_dir = g_build_filename(cache_dir, BACKGROUND_CACHE_DIR_NAME, NULL);
    if (!g_file_test(background_cache_dir, G_FILE_TEST_IS_DIR)) {
        if (g_mkdir_with_parents(background_cache_dir, 0755) != 0) {
            g_printerr("Failed to create background cache directory: %s\n", background_cache_dir);
            g_free(background_cache_dir);
            return NULL;
        }
    }
    return background_cache_dir;
}

gchar* get_thumbnail_path(const gchar *filepath, const gchar *cache_dir) {
    GString *checksum = compute_md5(filepath);
    gchar *thumbnail_filename = g_strdup_printf("%s.png", checksum->str);
    GString *thumbnail_path = g_string_new(cache_dir);
    g_string_append_printf(thumbnail_path, "/%s", thumbnail_filename);
    g_free(thumbnail_filename);
    g_string_free(checksum, TRUE);
    gchar *result = g_string_free(thumbnail_path, FALSE);
    return result;
}

gboolean is_thumbnail_up_to_date(const gchar *image_path, const gchar *thumbnail_path) {
    struct stat image_stat, thumb_stat;
    if (stat(image_path, &image_stat) != 0) {
        return FALSE;
    }
    if (stat(thumbnail_path, &thumb_stat) != 0) {
        return FALSE;
    }
    return thumb_stat.st_mtime >= image_stat.st_mtime;
}

GdkPixbuf* create_and_cache_thumbnail(const gchar *image_path, const gchar *thumbnail_path) {
    GError *error = NULL;
    GdkPixbuf *thumbnail = gdk_pixbuf_new_from_file_at_scale(image_path, THUMBNAIL_SIZE, THUMBNAIL_SIZE, TRUE, &error);
    if (!thumbnail) {
        g_printerr("Error loading and scaling image %s: %s\n", image_path, error->message);
        g_error_free(error);
        return NULL;
    }

    if (!gdk_pixbuf_save(thumbnail, thumbnail_path, "png", &error, NULL)) {
        g_printerr("Error saving thumbnail %s: %s\n", thumbnail_path, error->message);
        g_error_free(error);
        g_object_unref(thumbnail);
        return NULL;
    }

    return thumbnail;
}

GdkPixbuf* get_thumbnail(const gchar *image_path, const gchar *cache_dir) {
    gchar *thumbnail_path = get_thumbnail_path(image_path, cache_dir);
    if (!thumbnail_path) {
        return NULL;
    }

    GdkPixbuf *thumbnail = NULL;
    if (is_thumbnail_up_to_date(image_path, thumbnail_path)) {
        thumbnail = gdk_pixbuf_new_from_file(thumbnail_path, NULL);
        if (!thumbnail) {
            g_printerr("Failed to load cached thumbnail: %s\n", thumbnail_path);
            thumbnail = create_and_cache_thumbnail(image_path, thumbnail_path);
        }
    } else {
        thumbnail = create_and_cache_thumbnail(image_path, thumbnail_path);
    }

    g_free(thumbnail_path);
    return thumbnail;
}

gchar* fetch_random_wallpaper_url() {
    const gchar *api_url = "https://wallhaven.cc/api/v1/search?sorting=random&categories=111&purity=100&atleast=1920x1080&resolutions=1920x1080,2560x1440,3840x2160";

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("GET", api_url);
    soup_session_send_message(session, msg);

    if (msg->status_code != SOUP_STATUS_OK) {
        g_printerr("Failed to fetch wallpaper data: %s\n", msg->reason_phrase);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    const gchar *response = msg->response_body->data;
    gsize length = msg->response_body->length;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    if (!json_parser_load_from_data(parser, response, length, &error)) {
        g_printerr("Failed to parse JSON: %s\n", error->message);
        g_error_free(error);
        g_object_unref(parser);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_printerr("Invalid JSON structure.\n");
        g_object_unref(parser);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    JsonObject *root_obj = json_node_get_object(root);
    JsonArray *data_array = json_object_get_array_member(root_obj, "data");
    if (json_array_get_length(data_array) == 0) {
        g_printerr("No wallpapers found in the response.\n");
        g_object_unref(parser);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    JsonObject *wallpaper = json_array_get_object_element(data_array, 0);
    const gchar *path = json_object_get_string_member(wallpaper, "path");
    if (!path) {
        g_printerr("No 'path' field found in wallpaper data.\n");
        g_object_unref(parser);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    gchar *wallpaper_url = g_strdup(path);

    g_object_unref(parser);
    g_object_unref(msg);
    g_object_unref(session);

    return wallpaper_url;
}

gchar* download_wallpaper(const gchar *url, const gchar *cache_dir) {
    GString *checksum = compute_md5(url);
    gchar *wallpaper_filename = g_strdup_printf("%s.jpg", checksum->str);
    GString *wallpaper_path = g_string_new(cache_dir);
    g_string_append_printf(wallpaper_path, "/%s", wallpaper_filename);
    g_free(wallpaper_filename);
    g_string_free(checksum, TRUE);
    gchar *result = g_string_free(wallpaper_path, FALSE);

    if (g_file_test(result, G_FILE_TEST_EXISTS)) {
        return result;
    }

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("GET", url);
    soup_session_send_message(session, msg);

    if (msg->status_code != SOUP_STATUS_OK) {
        g_printerr("Failed to download wallpaper: %s\n", msg->reason_phrase);
        g_object_unref(msg);
        g_object_unref(session);
        g_free(result);
        return NULL;
    }

    const gchar *response = msg->response_body->data;
    gsize length = msg->response_body->length;

    GError *error = NULL;
    if (!g_file_set_contents(result, response, length, &error)) {
        g_printerr("Failed to save wallpaper to cache: %s\n", error->message);
        g_error_free(error);
        g_object_unref(msg);
        g_object_unref(session);
        g_free(result);
        return NULL;
    }

    g_object_unref(msg);
    g_object_unref(session);

    return result;
}

gboolean set_wallpaper(const gchar *filepath) {
    if (!filepath) {
        g_printerr("Invalid filepath provided to set_wallpaper.\n");
        return FALSE;
    }

    gchar *command = g_strdup_printf("feh --bg-scale \"%s\"", filepath);
    if (!command) {
        g_printerr("Failed to construct command for feh.\n");
        return FALSE;
    }

    int ret = system(command);
    g_free(command);

    if (ret != 0) {
        g_printerr("Failed to set wallpaper using feh.\n");
        return FALSE;
    }

    g_print("Wallpaper set successfully.\n");
    return TRUE;
}

gboolean on_image_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;

    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    ImageData *img_data = (ImageData*)data;
    g_print("Selected wallpaper: %s\n", img_data->filepath);

    if (img_data && img_data->filepath) {
        gboolean success = set_wallpaper(img_data->filepath);
        if (!success) {
            g_printerr("Failed to set wallpaper.\n");
        }
    } else {
        g_printerr("Invalid image data.\n");
    }

    g_free(img_data);
    gtk_main_quit();

    return TRUE;
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    (void)data;

    if (event->keyval == GDK_KEY_q || event->keyval == GDK_KEY_Q) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

void add_css(GtkWidget *window, gboolean rounded, gboolean transparent) {
    GtkCssProvider *provider = gtk_css_provider_new();
    GString *css = g_string_new(
        "window {"
        "   border: none;"
        "}"
        "scrollbar slider {"
        "   background-color: rgba(255, 255, 255, 0.3);"
        "   min-width: 8px;"
        "   border-radius: 4px;"
        "}"
        "scrollbar trough {"
        "   background-color: rgba(0, 0, 0, 0.0);"
        "}"
        ".event-box:hover {"
        "   background-color: rgba(255, 255, 255, 0.2);"
        "}"
    );

    if (transparent) {
        g_string_append(css,
            "window {"
            "   background-color: rgba(0, 0, 0, 0.0);"
            "}"
        );
    } else {
        g_string_append(css,
            "window {"
            "   background-color: rgba(0, 0, 0, 1.0);"
            "}"
        );
    }

    if (rounded) {
        g_string_append(css,
            ".rounded-container {"
            "   background-color: rgba(0, 0, 0, 0.7);"
            "   border-radius: 15px;"
            "   padding: 10px;"
            "}"
            ".event-box {"
            "   border-radius: 10px;"
            "}"
        );
    } else {
        g_string_append(css,
            ".container {"
            "   background-color: rgba(0, 0, 0, 0.7);"
            "   padding: 10px;"
            "}"
        );
    }

    gtk_css_provider_load_from_data(provider, css->str, -1, NULL);
    g_string_free(css, TRUE);

    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
}

int main(int argc, char *argv[]) {
    gboolean rounded = FALSE;
    gboolean transparent = FALSE;
    gboolean background_flag = FALSE;
    gboolean no_window_flag = FALSE;

    for(int i=1; i<argc; i++) {
        if(g_strcmp0(argv[i], "--r") == 0) {
            rounded = TRUE;
        }
        if(g_strcmp0(argv[i], "--t") == 0) {
            transparent = TRUE;
        }
        if(g_strcmp0(argv[i], "--b") == 0) {
            background_flag = TRUE;
        }
        if(g_strcmp0(argv[i], "--n") == 0) {
            no_window_flag = TRUE;
        }
    }

    if(background_flag) {
        gchar *wallpaper_url = fetch_random_wallpaper_url();
        if (!wallpaper_url) {
            g_printerr("Failed to fetch a random wallpaper.\n");
            return 1;
        }

        gchar *cache_dir = get_cache_dir();
        if (!cache_dir) {
            g_printerr("Failed to access cache directory.\n");
            g_free(wallpaper_url);
            return 1;
        }

        gchar *background_cache_dir = get_background_cache_dir(cache_dir);
        if (!background_cache_dir) {
            g_printerr("Failed to access background cache directory.\n");
            g_free(wallpaper_url);
            g_free(cache_dir);
            return 1;
        }

        gchar *wallpaper_path = download_wallpaper(wallpaper_url, background_cache_dir);
        g_free(wallpaper_url);

        if (!wallpaper_path) {
            g_printerr("Failed to download wallpaper.\n");
            g_free(background_cache_dir);
            g_free(cache_dir);
            return 1;
        }

        gboolean set_success = set_wallpaper(wallpaper_path);
        g_free(wallpaper_path);
        g_free(background_cache_dir);
        g_free(cache_dir);

        if (!set_success) {
            g_printerr("Failed to set wallpaper.\n");
            return 1;
        }

        return 0;
    }

    gtk_init(&argc, &argv);

    if(no_window_flag) {
        gtk_main();
        return 0;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Wallpaper Selector");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 200);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_UTILITY);

    if (transparent) {
        gtk_widget_set_app_paintable(window, TRUE);
        GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(window));
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (visual != NULL) {
            gtk_widget_set_visual(window, visual);
        }
    }

    add_css(window, rounded, transparent);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    if (rounded) {
        gtk_style_context_add_class(gtk_widget_get_style_context(container), "rounded-container");
    } else {
        gtk_style_context_add_class(gtk_widget_get_style_context(container), "container");
    }
    gtk_container_add(GTK_CONTAINER(scrolled_window), container);

    GList *images = get_image_files(WALLPAPER_DIR);
    if (!images) {
        g_printerr("No images found in directory: %s\n", WALLPAPER_DIR);
        gtk_widget_destroy(window);
        return 1;
    }

    gchar *cache_dir = get_cache_dir();
    if (!cache_dir) {
        g_printerr("Failed to set up cache directory.\n");
        gtk_widget_destroy(window);
        return 1;
    }

    for (GList *l = images; l != NULL; l = l->next) {
        gchar *filepath = (gchar*)l->data;
        GdkPixbuf *thumbnail = get_thumbnail(filepath, cache_dir);
        if (!thumbnail) {
            g_free(filepath);
            continue;
        }

        GtkWidget *image = gtk_image_new_from_pixbuf(thumbnail);
        g_object_unref(thumbnail);

        GtkWidget *event_box = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(event_box), image);
        gtk_style_context_add_class(gtk_widget_get_style_context(event_box), "event-box");

        ImageData *img_data = g_new(ImageData, 1);
        img_data->filepath = filepath;
        img_data->image = image;
        img_data->event_box = event_box;

        g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_image_clicked), img_data);

        gtk_box_pack_start(GTK_BOX(container), event_box, FALSE, FALSE, 5);
    }

    g_free(cache_dir);

    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    GList *current = images;
    while (current != NULL) {
        gchar *filepath = (gchar*)current->data;
        g_free(filepath);
        current = current->next;
    }
    g_list_free(images);

    return 0;
}
