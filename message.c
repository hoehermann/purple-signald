#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <gio/gio.h>

#include "libsignald.h"

#ifdef SUPPORT_EXTERNAL_ATTACHMENTS
#include <magic.h>

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

gchar *
signald_write_external_attachment(SignaldAccount *sa, const char *filename)
{
    // We're going to ignore the supplied mimetype and use libmagic to figure it out
    // ourselves.  This is both more secure and less error prone, since we're not
    // trusting the sender.

    const char *path;
    const char *baseurl;
    gchar *url = NULL;

    if (signald_get_external_attachment_settings(sa, &path, &baseurl) != 0) {
        return NULL;
    }

    magic_t cookie = magic_open(MAGIC_EXTENSION);

    if ((cookie == NULL) || (magic_load(cookie, NULL) != 0)) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Error initializing libmagic (errno: %d)", magic_errno(cookie));

        return NULL;
    }

    const gchar *extensions = magic_file(cookie, filename);

    if (extensions == NULL) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Error getting extension for '%s': %d", filename, magic_errno(cookie));

        return NULL;
    }

    gchar **extension_arr = g_strsplit(extensions, "/", 2);

    if (extension_arr[0] != NULL) {
        GFile *source = g_file_new_for_path(filename);
        char *basename = g_file_get_basename(source);

        gchar *ext = extension_arr[0];
        gchar *destpath = g_strconcat(path, "/", basename, ".", ext, NULL);

        GFile *destination = g_file_new_for_path(destpath);
        GError *error;

        purple_debug_error(SIGNALD_PLUGIN_ID, "Copying attachment from '%s' to '%s'", filename, destpath);

        if (g_file_copy(source,
                        destination,
                        G_FILE_COPY_NONE,
                        NULL /* cancellable */,
                        NULL /* progress cb */,
                        NULL /* progress cb data */,
                        &error)) {

            url = g_strconcat(baseurl, "/", basename, ".", ext, NULL);
        } else {
            purple_debug_error(SIGNALD_PLUGIN_ID, "Error saving attachment to '%s': %s", destpath, error->message);

            g_error_free(error);
        }

        g_object_unref(source);
        g_object_unref(destination);

        g_free(destpath);
    } else {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Couldn't determine mimetype for file: '%s'", filename);
    }

    g_strfreev(extension_arr);
    magic_close(cookie);

    return url;
}

#endif

void
signald_parse_attachment(SignaldAccount *sa, JsonObject *obj, GString *message)
{
    const char *type = json_object_get_string_member(obj, "contentType");
    const char *fn = json_object_get_string_member(obj, "storedFilename");

#ifdef SUPPORT_EXTERNAL_ATTACHMENTS

    if (purple_account_get_bool(sa->account, SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS, FALSE)) {
        gchar *url = signald_write_external_attachment(sa, fn);

        if (url != NULL) {
            g_string_append_printf(message, "<a href=\"%s\">Attachment (type %s): %s</a><br/>", url, type, url);
            g_free(url);
        }

        return;
    }

#endif

    if (purple_strequal(type, "image/jpeg") || purple_strequal(type, "image/png")) {
        // TODO: forward "access denied" error to UI
        PurpleStoredImage *img = purple_imgstore_new_from_file(fn);
        size_t size = purple_imgstore_get_size(img);
        int img_id = purple_imgstore_add_with_id(g_memdup(purple_imgstore_get_data(img), size), size, NULL);

        g_string_append_printf(message, "<IMG ID=\"%d\"/><br/>", img_id);
        g_string_append_printf(message, "<a href=\"file://%s \">Image (type: %s)</a><br/>", fn, type);
    } else {
        //TODO: Receive file using libpurple's file transfer API
        g_string_append_printf(message, "<a href=\"file://%s \">Attachment (type: %s)</a><br/>", fn, type);
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

gboolean
signald_format_message(SignaldAccount *sa, SignaldMessage *msg, GString **target, gboolean *has_attachment)
{
    // handle attachments, creating appropriate message content (always allocates *target)
    *target = signald_prepare_attachments_message(sa, msg->data);

    if ((*target)->len > 0) {
        *has_attachment = TRUE;
    } else {
        *has_attachment = FALSE;
    }

    // append actual message text
    g_string_append(*target, json_object_get_string_member(msg->data, "message"));

    return (*target)->len > 0; // message not empty
}

gboolean
signald_parse_message(SignaldAccount *sa, JsonObject *obj, SignaldMessage *msg)
{
    if (msg == NULL) {
        return FALSE;
    }

    JsonObject *syncMessage = json_object_get_object_member(obj, "syncMessage");

    // Signal's integer timestamps are in milliseconds
    // timestamp, timestampISO and dataMessage.timestamp seem to always be the same value (message sent time)
    // serverTimestamp is when the server received the message

    msg->envelope = obj;
    msg->timestamp = json_object_get_int_member(obj, "timestamp") / 1000;
    msg->is_sync_message = (syncMessage != NULL);

    if (syncMessage != NULL) {
        JsonObject *sent = json_object_get_object_member(syncMessage, "sent");

        if (sent == NULL) {
            return FALSE;
        }

        msg->conversation_name = (gchar *)json_object_get_string_member(sent, "destination");
        msg->data = json_object_get_object_member(sent, "message");
    } else {
        msg->conversation_name = (gchar *)json_object_get_string_member(obj, "source");
        msg->data = json_object_get_object_member(obj, "dataMessage");
    }

    if (msg->data == NULL) {
        return FALSE;
    }

    if (msg->conversation_name == NULL) {
        msg->conversation_name = SIGNALD_UNKNOWN_SOURCE_NUMBER;
    }

    if (json_object_has_member(msg->data, "groupInfo")) {
        msg->type = SIGNALD_MESSAGE_TYPE_GROUP;
    } else {
        msg->type = SIGNALD_MESSAGE_TYPE_DIRECT;
    }

    return TRUE;
}

int
signald_send_message(SignaldAccount *sa, SignaldMessageType type, gchar *recipient, const char *message)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "send");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (type == SIGNALD_MESSAGE_TYPE_DIRECT) {
        json_object_set_string_member(data, "recipientNumber", recipient);
    } else if (type == SIGNALD_MESSAGE_TYPE_GROUP) {
        json_object_set_string_member(data, "recipientGroupId", recipient);
    } else {
        return -1;
    }

    // Search for embedded images and attach them to the message. Remove the <img> tags.
    JsonArray *attachments = json_array_new();
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

    json_object_set_array_member(data, "attachments", attachments);

    char *plain = purple_unescape_html(msg->str);

    json_object_set_string_member(data, "messageBody", plain);

    // TODO: check if json_object_set_string_member manages copies of the data it is given (else these would be read from free'd memory)

    int ret = 1;

    if (!signald_send_json(sa, data)) {
        ret = -errno;
    }

    g_string_free(msg, TRUE);
    g_free(plain);
    json_object_unref(data);

    return ret;
}
