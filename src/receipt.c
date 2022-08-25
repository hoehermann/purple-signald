#include "receipt.h"
#include "structs.h"
#include "comms.h"
#include "defines.h"
#include "message.h"
#include <json-glib/json-glib.h>

void signald_mark_read(SignaldAccount * sa, gint64 timestamp_micro, const char *uuid) {
    g_return_if_fail(uuid != NULL);
    if (purple_account_get_bool(sa->account, SIGNALD_OPTION_MARK_READ, FALSE)) {
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "mark_read");
        json_object_set_string_member(data, "account", sa->uuid);
        signald_set_recipient(data, "to", uuid);
        JsonArray *timestamps = json_array_new();
        json_array_add_int_element(timestamps, timestamp_micro);
        json_object_set_array_member(data, "timestamps", timestamps);
        if (!signald_send_json(sa, data)) {
            purple_debug_error(SIGNALD_PLUGIN_ID, "Unable to send receipt for %ld.\n", timestamp_micro);
        }
        json_object_unref(data);
    }
}

void signald_mark_read_chat(SignaldAccount * sa, gint64 timestamp_micro, GHashTable *users) {
    GList * uuids = g_hash_table_get_keys(users);
    for (const GList *elem = uuids; elem != NULL; elem = elem->next) {
        signald_mark_read(sa, timestamp_micro, elem->data);
    }
    g_list_free(uuids);
}

void signald_process_receipt(SignaldAccount *sa, JsonObject *obj) {
    if (purple_account_get_bool(sa->account, SIGNALD_OPTION_DISPLAY_RECEIPTS, FALSE)) {
        // receipts carry no groupV2 information
        // source is always the reader
        JsonObject * source = json_object_get_object_member(obj, "source");
        const gchar * who = json_object_get_string_member(source, "uuid");
        PurpleConversation * conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, sa->account);
        if (conv) {
            // only display receipt if the conversation is currently open
            
            JsonObject * receipt_message = json_object_get_object_member(obj, "receipt_message");
            const gchar * type = json_object_get_string_member(receipt_message, "type");
            gint64 when = json_object_get_int_member(receipt_message, "when");
            time_t timestamp = when / 1000;

            // concatenate list of timestamps into one message
            GString *message = g_string_new(type);
            message = g_string_append(message, " receipt for message from ");
            JsonArray * timestamps = json_object_get_array_member(receipt_message, "timestamps");
            GList * timestamp_list = json_array_get_elements(timestamps);
            for (GList * elem = timestamp_list; elem != NULL ; elem = elem->next) {
                guint64 timestamp_micro = json_node_get_int(elem->data);
                time_t timestamp = timestamp_micro / 1000;
                struct tm *tm = localtime(&timestamp);
                message = g_string_append(message, purple_date_format_long(tm));
                if (elem->next) {
                    message = g_string_append(message, ", ");
                }
            }
            g_list_free(timestamp_list);
            
            PurpleMessageFlags flags = PURPLE_MESSAGE_NO_LOG;
            purple_conv_im_write(PURPLE_CONV_IM(conv), who, message->str, flags, timestamp);
            
            g_string_free(message, TRUE);
        }
    }
}