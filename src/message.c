#include <sys/types.h>
#include <errno.h>
#include "libsignald.h"

const char *
signald_get_uuid_from_address(JsonObject *obj, const char *address_key)
{
    JsonObject *address = json_object_get_object_member(obj, address_key);
    if (address == NULL) {
        return NULL;
    } else {
        return (const char *)json_object_get_string_member(address, "uuid");
    }
}

static gboolean
signald_is_uuid(const gchar *identifier) {
    if (identifier) {
        return strlen(identifier) == 36;
    } else {
        return FALSE;
    }
}

static gboolean
signald_is_number(const gchar *identifier) {
    return identifier && identifier[0] == '+';
}

void
signald_set_recipient(SignaldAccount *sa, JsonObject *obj, gchar *recipient)
{
    g_return_if_fail(recipient);
    g_return_if_fail(obj);
    char * address_type = NULL;
    if (signald_is_number(recipient)) {
        address_type = "number";
    } else if (signald_is_uuid(recipient)) {
        address_type = "uuid";
    }
    g_return_if_fail(obj);
    JsonObject *address = json_object_new();
    // if contact was added manually and not yet migrated, the recipient might still be a number, not a UUID
    json_object_set_string_member(address, address_type, recipient);
    json_object_set_object_member(obj, "recipientAddress", address);
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

    if (json_object_has_member(msg->data, "quote")) {
        JsonObject *quote = json_object_get_object_member(msg->data, "quote");
        const char *text = json_object_get_string_member(quote, "text");
        gchar **lines = g_strsplit(text, "\n", 0);
        for (int i = 0; lines[i] != NULL; i++) {
            g_string_append_printf(*target, "> %s\n", lines[i]);
        }
        g_strfreev(lines);
    }

    if (json_object_has_member(msg->data, "reaction")) {
        JsonObject *reaction = json_object_get_object_member(msg->data, "reaction");
        const char *emoji = json_object_get_string_member(reaction, "emoji");
        const gboolean remove = json_object_get_boolean_member(reaction, "remove");
        const time_t targetSentTimestamp = json_object_get_int_member(reaction, "targetSentTimestamp") / 1000;
        struct tm *tm = localtime(&targetSentTimestamp);
        if (remove) {
            g_string_printf(*target, "removed their %s reaction.", emoji);
        } else {
            g_string_printf(*target, "reacted with %s (to message from %s).", emoji, purple_date_format_long(tm));
        }
    }

    // append actual message text
    g_string_append(*target, json_object_get_string_member(msg->data, "body"));

    return (*target)->len > 0; // message not empty
}

gboolean
signald_parse_message(SignaldAccount *sa, JsonObject *obj, SignaldMessage *msg)
{
    if (msg == NULL) {
        return FALSE;
    }

    JsonObject *syncMessage = json_object_get_object_member(obj, "sync_message");

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

        msg->sender_uuid = (char *)signald_get_uuid_from_address(sent, "destination");
        msg->data = json_object_get_object_member(sent, "message");
     } else {
        JsonObject *source = json_object_get_object_member(obj, "source");
        msg->sender_uuid = (char *)json_object_get_string_member(source, "uuid");
        msg->data = json_object_get_object_member(obj, "data_message");
     }

    if (msg->data == NULL) {
        return FALSE;
    }

    if (msg->sender_uuid == NULL) {
        msg->sender_uuid = SIGNALD_UNKNOWN_SOURCE_NUMBER;
    }

    if (json_object_has_member(msg->data, "groupV2")) {
        msg->type = SIGNALD_MESSAGE_TYPE_GROUPV2;
    } else {
        msg->type = SIGNALD_MESSAGE_TYPE_DIRECT;
    }

    return TRUE;
}

int
signald_send_message(SignaldAccount *sa, SignaldMessageType type, gchar *recipient, const char *message)
{
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_send_message…\n");
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "send");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    //json_object_set_string_member(data, "account", sa->uuid); // alternative to supplying the username, mutually exclusive

    if (type == SIGNALD_MESSAGE_TYPE_DIRECT) {
        signald_set_recipient(sa, data, recipient);
    } else {
        json_object_set_string_member(data, "recipientGroupId", recipient);
    }

    JsonArray *attachments = json_array_new();
    char *textonly = signald_detach_images(message, attachments);
    json_object_set_array_member(data, "attachments", attachments);
    char *plain = purple_unescape_html(textonly);
    g_free(textonly);
    json_object_set_string_member(data, "messageBody", plain);

    int ret = !purple_account_get_bool(sa->account, SIGNALD_OPTION_WAIT_SEND_ACKNOWLEDEMENT, FALSE);
    if (!signald_send_json(sa, data)) {
        ret = -errno;
    }
    json_object_unref(data);
    // TODO: check if json_object_set_string_member manages copies of the data it is given
    g_free(plain);

    if (ret == 0) {
        // free last message just in case it still lingers in memory
        g_free(sa->last_message);
        // store message for later echo
        sa->last_message = g_strdup(message);
        // store this as the currently active conversation
        sa->last_conversation = purple_find_conversation_with_account(PURPLE_CONV_TYPE_ANY, recipient, sa->account);
        if (sa->last_conversation == NULL) {
            // no appropriate conversation was found. maybe it is a group?
            PurpleConvChat *conv_chat = purple_conversations_find_chat_with_account(recipient, sa->account);
            if (conv_chat != NULL) {
                sa->last_conversation = conv_chat->conv;
            }
        }
    }
    return ret;
}

static void
signald_send_check_result(JsonArray* results, guint i, JsonNode* result_node, gpointer user_data) {
    int * devices_count_ptr = (int *)user_data;
    JsonObject * result = json_node_get_object(result_node);
    JsonObject * success = json_object_get_object_member(result, "success");
    if (success) {
        JsonArray * devices = json_object_get_array_member(success, "devices");
        if (devices) {
            *devices_count_ptr += json_array_get_length(devices);
        }
    }
}

void
signald_send_acknowledged(SignaldAccount *sa,  JsonObject *data) {
    time_t timestamp = json_object_get_int_member(data, "timestamp") / 1000;
    int devices_count = 0;
    JsonArray * results = json_object_get_array_member(data, "results");
    if (results) {
        if (json_array_get_length(results) == 0) {
            // when sending message to self, the results array is empty
            // TODO: check if recipient actually was sa->uuid
            devices_count = 1;
        } else {
            json_array_foreach_element(results, signald_send_check_result, &devices_count);
        }
    }
    if (sa->last_conversation && sa->uuid && sa->last_message) {
        if (devices_count > 0) {
            PurpleMessageFlags flags = PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;
            purple_conversation_write(sa->last_conversation, sa->uuid, sa->last_message, flags, timestamp);
            g_free(sa->last_message);
            sa->last_message = NULL;
        } else {
            // form purple_conv_present_error()
            purple_conversation_write(sa->last_conversation, NULL, "Message was not delivered to any devices.", PURPLE_MESSAGE_ERROR, time(NULL));
        }
    } else if (devices_count == 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "A message was not delivered to any devices.\n");
    }
}
