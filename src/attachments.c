#include <sys/stat.h>
#include <MegaMimes.h>
#include "defines.h"
#include "structs.h"
#include "attachments.h"
#include <json-glib/json-glib.h>

#if !(GLIB_CHECK_VERSION(2, 67, 3))
#define g_memdup2 g_memdup
#endif

#if __has_include("gdk-pixbuf/gdk-pixbuf.h")
#include <gdk-pixbuf/gdk-pixbuf.h>
static int
pixbuf_format_mimetype_comparator(GdkPixbufFormat *format, const char *type) {
    int cmp = 1;
    gchar **mime_types = gdk_pixbuf_format_get_mime_types(format);
    for (gchar **mime_type = mime_types; mime_type != NULL && *mime_type != NULL && cmp != 0; mime_type++) {
        cmp = g_strcmp0(type, *mime_type);
    }
    g_strfreev(mime_types);
    return cmp;
}

static gboolean
is_loadable_image_mimetype(const char *mimetype) {
    // check if mimetype is among the formats supported by pixbuf
    GSList *pixbuf_formats = gdk_pixbuf_get_formats();
    GSList *pixbuf_format = g_slist_find_custom(pixbuf_formats, mimetype, (GCompareFunc)pixbuf_format_mimetype_comparator);
    g_slist_free(pixbuf_formats);
    return pixbuf_format != NULL;
}
#else
static gboolean
is_loadable_image_mimetype(const char *mimetype) {
    // blindly assume frontend can handle jpeg and png
    return purple_strequal(mimetype, "image/jpeg") || purple_strequal(mimetype, "image/png");
}
#endif

int
signald_get_external_attachment_settings(SignaldAccount *sa, const char **path, const char **url)
{
    *path = purple_account_get_string(sa->account, SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR, "");
    *url = purple_account_get_string(sa->account, SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL, "");

    if (strlen(*path) == 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "External attachments configured but no attachment path set.");

        return -1;
    }

    GFile *f = g_file_new_for_path(*path);
    GFileType type = g_file_query_file_type(f, G_FILE_QUERY_INFO_NONE, NULL);

    g_object_unref(f);

    if (type != G_FILE_TYPE_DIRECTORY) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "External attachments path is not a valid directory: '%s'", *path);

        return -1;
    }

    if (strlen(*url) == 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "External attachments configured but no attachment url set.");

        return -1;
    }

    return 0;
}

void
signald_parse_attachment(SignaldAccount *sa, JsonObject *obj, GString *message)
{
    const char *type = json_object_get_string_member(obj, "contentType");
    const char *fn = json_object_get_string_member(obj, "storedFilename");

    if (purple_account_get_bool(sa->account, SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS, FALSE)) {
        gchar *url = signald_write_external_attachment(sa, fn, type);

        if (url != NULL) {
            g_string_append_printf(message, "<a href=\"%s\">Attachment (type %s): %s</a><br/>", url, type, url);
            g_free(url);
        } else {
            g_string_append_printf(message, "An error occurred processing an attachment. Enable debug logging for more information.");
        }

        return;
    }

    if (is_loadable_image_mimetype(type)) {
        PurpleStoredImage *img = purple_imgstore_new_from_file(fn); // TODO: forward "access denied" error to UI
        size_t size = purple_imgstore_get_size(img);
        int img_id = purple_imgstore_add_with_id(g_memdup2(purple_imgstore_get_data(img), size), size, NULL);

        g_string_append_printf(message, "<IMG ID=\"%d\"/><br/>", img_id);
        g_string_append_printf(message, "<a href=\"file://%s\">Image (type: %s)</a><br/>", fn, type);
    } else {
        //TODO: Receive file using libpurple's file transfer API
        g_string_append_printf(message, "<a href=\"file://%s\">Attachment (type: %s)</a><br/>", fn, type);
    }

    purple_debug_info(SIGNALD_PLUGIN_ID, "Attachment: %s", message->str);
}

GString *
signald_prepare_attachments_message(SignaldAccount *sa, JsonObject *obj) {
    JsonArray *attachments = json_object_get_array_member(obj, "attachments");
    guint len = json_array_get_length(attachments);
    GString *attachments_message = g_string_sized_new(len * 100); // Preallocate buffer. Exact size doesn't matter. It grows automatically if it is too small

    for (guint i=0; i < len; i++) {
        signald_parse_attachment(sa, json_array_get_object_element(attachments, i), attachments_message);
    }

    return attachments_message;
}

// Search for embedded images and save them to files.
// Remove the <img> tags.
char *
signald_detach_images(const char *message, JsonArray *attachments) {
    GString *msg = g_string_new(""); // this shall hold the actual message body (without the <img> tags)
    GData *attribs;
    const char *start, *end, *last;

    last = message;

    /* for each valid IMG tag... */
    while (last && *last && purple_markup_find_tag("img", last, &start, &end, &attribs))
    {
        PurpleStoredImage *image = NULL;
        const char *id;

        if (start - last) {
            g_string_append_len(msg, last, start - last);
        }

        id = g_datalist_get_data(&attribs, "id");

        /* ... if it refers to a valid purple image ... */
        if (id && (image = purple_imgstore_find_by_id(atoi(id)))) {
            unsigned long size = purple_imgstore_get_size(image);
            gconstpointer imgdata = purple_imgstore_get_data(image);
            gchar *tmp_fn = NULL;
            GError *error = NULL;
            //TODO: This is not very secure. But attachment handling should be reworked in signald to allow sending them in the same stream as the message
            //Signal requires the filename to end with a known image extension. However it does not care if the extension matches the image format.
            //contentType is ignored completely.
            //https://gitlab.com/thefinn93/signald/issues/11

            gint file = g_file_open_tmp("XXXXXX.png", &tmp_fn, &error);
            if (file == -1) {
                purple_debug_error(SIGNALD_PLUGIN_ID, "Error: %s\n", error->message);
                // TODO: show this error to the user
            } else {
                close(file); // will be re-opened by g_file_set_contents
                error = NULL;
                if (!g_file_set_contents(tmp_fn, imgdata, size, &error)) {
                    purple_debug_error(SIGNALD_PLUGIN_ID, "Error: %s\n", error->message);
                } else {
                    chmod(tmp_fn, 0644);
                    JsonObject *attachment = json_object_new();
                    json_object_set_string_member(attachment, "filename", tmp_fn);
//                    json_object_set_string_member(attachment, "caption", "Caption");
//                    json_object_set_string_member(attachment, "contentType", "image/png");
//                    json_object_set_int_member(attachment, "width", 150);
//                    json_object_set_int_member(attachment, "height", 150);
                    json_array_add_object_element(attachments, attachment);
                }
                g_free(tmp_fn);
                //TODO: Check for memory leaks
                //TODO: Delete file when response from signald is received
            }
        }
        /* If the tag is invalid, skip it, thus no else here */

        g_datalist_clear(&attribs);

        /* continue from the end of the tag */
        last = end + 1;
    }

    /* append any remaining message data */
    if (last && *last) {
        g_string_append(msg, last);
    }

    return g_string_free(msg, FALSE);
}

gchar *
signald_write_external_attachment(SignaldAccount *sa, const char *filename, const char *mimetype_remote)
{
    const char *path;
    const char *baseurl;
    gchar *url = NULL;

    if (signald_get_external_attachment_settings(sa, &path, &baseurl) != 0) {
        return NULL;
    }

    GFile *f = g_file_new_for_path(filename);
    GFileType type = g_file_query_file_type(f, G_FILE_QUERY_INFO_NONE, NULL);

    g_object_unref(f);

    if (type == G_FILE_TYPE_UNKNOWN) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Error accessing file (permission issue?)");

        return NULL;
    } else if (type != G_FILE_TYPE_REGULAR) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "File is not a regular file... that's odd.");

        return NULL;
    }

    GFile *source = g_file_new_for_path(filename);
    char *basename = g_file_get_basename(source); // TODO: stickers are using "hash/id" – do not simply use the basename or stickers will get overwritten

    gchar * ext = "unknown";
    char ** extensions = (char **)getMegaMimeExtensions(mimetype_remote);
    if (extensions && extensions[0]) {
        ext = extensions[0]+2;
    } else {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Sender supplied mime-type %s. No extensions are known for this mime-type.", mimetype_remote);
    }
    gchar *destpath = g_strconcat(path, "/", basename, ".", ext, NULL);

    GFile *destination = g_file_new_for_path(destpath);

    purple_debug_info(SIGNALD_PLUGIN_ID, "Copying attachment from '%s' to '%s'...\n", filename, destpath);

    GError *gerror = NULL;
    if (g_file_copy(source,
                    destination,
                    G_FILE_COPY_NONE,
                    NULL /* cancellable */,
                    NULL /* progress cb */,
                    NULL /* progress cb data */,
                    &gerror)) {

        url = g_strconcat(baseurl, "/", basename, ".", ext, NULL);
    } else {
        // TODO: print this in conversation window
        purple_debug_error(SIGNALD_PLUGIN_ID, "Error saving attachment to '%s': %s\n", destpath, gerror->message);

        g_error_free(gerror);
    }

    g_object_unref(source);
    g_object_unref(destination);

    g_free(destpath);

    freeMegaStringArray(extensions);

    return url;
}
