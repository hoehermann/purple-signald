#include <sys/types.h>
#include <errno.h>
#include <json-glib/json-glib.h>
#include "message.h"
#include "defines.h"
#include "attachments.h"
#include "comms.h"
#include "groups.h"
#include "purple_compat.h"
#include "receipt.h"

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
signald_set_recipient(JsonObject *obj, const gchar *key, const gchar *recipient)
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
    json_object_set_object_member(obj, key, address);
}

gboolean
signald_format_message(SignaldAccount *sa, JsonObject *data, GString **target, gboolean *has_attachment)
{
    // handle attachments, creating appropriate message content (always allocates *target)
    *target = signald_prepare_attachments_message(sa, data);

    if (json_object_has_member(data, "sticker")) {
        JsonObject *sticker = json_object_get_object_member(data, "sticker");
        JsonObject *attachment = json_object_get_object_member(sticker, "attachment");
        signald_parse_attachment(sa, attachment, *target);
    }

    if ((*target)->len > 0) {
        *has_attachment = TRUE;
    } else {
        *has_attachment = FALSE;
    }

    if (json_object_has_member(data, "quote")) {
        JsonObject *quote = json_object_get_object_member(data, "quote");
        JsonObject *author = json_object_get_object_member(quote, "author");
        const char *uuid = json_object_get_string_member(author, "uuid");
        PurpleBuddy *buddy = purple_find_buddy(sa->account, uuid);
        const char *alias = purple_buddy_get_alias(buddy);
        if (alias && alias[0]) {
            g_string_append_printf(*target, "%s wrote:\n", alias);
        }
        const char *text = json_object_get_string_member(quote, "text");
        gchar **lines = g_strsplit(text, "\n", 0);
        for (int i = 0; lines[i] != NULL; i++) {
            g_string_append_printf(*target, "> %s\n", lines[i]);
        }
        g_strfreev(lines);
    }

    if (json_object_has_member(data, "reaction")) {
        JsonObject *reaction = json_object_get_object_member(data, "reaction");
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
    
    if (json_object_has_member(data, "groupV2")) {
        JsonObject *groupV2 = json_object_get_object_member(data, "groupV2");
        if (json_object_has_member(groupV2, "group_change")) {
            g_string_append(*target, "made changes to this group (settings, permissions, members). This plug-in cannot show the details.");
        }
    }

    // append actual message text
    g_string_append(*target, json_object_get_string_member(data, "body"));

    return (*target)->len > 0; // message not empty
}

void
signald_process_message(SignaldAccount *sa, JsonObject *obj)
{
    // timestamp and data_message.timestamp seem to always be the same value (message sent time)
    // server_receiver_timestamp is when the server received the message
    // server_deliver_timestamp is when the server delivered the message
    gint64 timestamp = json_object_get_int_member(obj, "timestamp");

    const gchar * sender_uuid = NULL;
    JsonObject *message_data = NULL;
    JsonObject * sent = NULL;
    
    // source is always the author of the message
    JsonObject * source = json_object_get_object_member(obj, "source");
    sender_uuid = json_object_get_string_member(source, "uuid");
    
    if (json_object_has_member(obj, "sync_message")) {
        JsonObject * sync_message = json_object_get_object_member(obj, "sync_message");
        if (json_object_has_member(sync_message, "sent")) {
            sent = json_object_get_object_member(sync_message, "sent");
            if (json_object_has_member(sent, "destination")) {
                // for synced messages, purple does not need the author,
                // but rather the conversation defined by recipient
                // this does it for direc messages, chats are handled below
                sender_uuid = signald_get_uuid_from_address(sent, "destination");
            }
            message_data = json_object_get_object_member(sent, "message");
        }
    } else if (json_object_has_member(obj, "data_message")) {
        message_data = json_object_get_object_member(obj, "data_message");
    }
    
    if (message_data == NULL) {
        purple_debug_warning(SIGNALD_PLUGIN_ID, "Ignoring message without usable payload.\n");
    } else {
        const gchar *groupId = NULL;
        if (json_object_has_member(message_data, "groupV2")) {
            JsonObject *groupInfo = json_object_get_object_member(message_data, "groupV2");
            groupId = json_object_get_string_member(groupInfo, "id");
        }
        signald_display_message(sa, sender_uuid, groupId, timestamp, sent != NULL, message_data);
    }
}

int
signald_send_message(SignaldAccount *sa, const gchar *who, gboolean is_chat, const char *message)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "send");
    json_object_set_string_member(data, "account", sa->uuid);

    if (is_chat) {
        json_object_set_string_member(data, "recipientGroupId", who);
    } else {
        signald_set_recipient(data, "recipientAddress", who);
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

    // wait for signald to acknowledge the message has been sent
    // for displaying the outgoing message later, it is stored locally
    if (ret == 0) {
        // free last message just in case it still lingers in memory
        g_free(sa->last_message);
        // store message for later echo
        sa->last_message = g_strdup(message);
        // store this as the currently active conversation
        sa->last_conversation = purple_find_conversation_with_account(PURPLE_CONV_TYPE_ANY, who, sa->account);
        if (sa->last_conversation == NULL) {
            // no appropriate conversation was found. maybe it is a group?
            PurpleConvChat *conv_chat = purple_conversations_find_chat_with_account(who, sa->account);
            if (conv_chat != NULL) {
                sa->last_conversation = conv_chat->conv;
            }
        }
    }
    return ret;
}

struct SignaldSendResult {
  SignaldAccount *sa;
  int devices_count;
};

static void
signald_send_check_result(JsonArray* results, guint i, JsonNode* result_node, gpointer user_data) {
    struct SignaldSendResult * sr = (struct SignaldSendResult *)user_data;
    JsonObject * result = json_node_get_object(result_node);
    JsonObject * success = json_object_get_object_member(result, "success");
    if (success) {
        JsonArray * devices = json_object_get_array_member(success, "devices");
        if (devices) {
            sr->devices_count += json_array_get_length(devices);
        }
    }
    
    const gchar * failure = NULL;
    // NOTE: These failures might actually be orthogonal. This only regards the first one.
    if (json_object_has_member(result, "identityFailure")) {
        failure = "identityFailure";
    } else if (json_object_get_boolean_member(result, "networkFailure")) {
        failure = "networkFailure";
    } else if (json_object_has_member(result, "proof_required_failure")) {
        failure = "proof_required_failure";
    } else if (json_object_get_boolean_member(result, "unregisteredFailure")) {
        failure = "unregisteredFailure";
    }
    if (failure) {
        JsonObject * address = json_object_get_object_member(result, "address");
        const gchar * number = json_object_get_string_member(address, "number");
        const gchar * uuid = json_object_get_string_member(address, "uuid");
        gchar * errmsg = g_strdup_printf("Message was not delivered to %s (%s) due to %s.", number, uuid, failure);
        purple_conversation_write(sr->sa->last_conversation, NULL, errmsg, PURPLE_MESSAGE_ERROR, time(NULL));
        g_free(errmsg);
    }
}

void
signald_send_acknowledged(SignaldAccount *sa,  JsonObject *data) {
    time_t timestamp = json_object_get_int_member(data, "timestamp") / 1000;
    struct SignaldSendResult sr;
    sr.sa = sa;
    sr.devices_count = 0;
    JsonArray * results = json_object_get_array_member(data, "results");
    if (results) {
        if (json_array_get_length(results) == 0) {
            // when sending message to self, the results array is empty
            // TODO: check if recipient actually was sa->uuid
            sr.devices_count = 1;
        } else {
            json_array_foreach_element(results, signald_send_check_result, &sr);
        }
    }
    if (sa->last_conversation && sa->uuid && sa->last_message) {
        if (sr.devices_count > 0) {
            PurpleMessageFlags flags = PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;
            purple_conversation_write(sa->last_conversation, sa->uuid, sa->last_message, flags, timestamp);
            g_free(sa->last_message);
            sa->last_message = NULL;
        } else {
            // form purple_conv_present_error()
            purple_conversation_write(sa->last_conversation, NULL, "Message was not delivered to any devices.", PURPLE_MESSAGE_ERROR, time(NULL));
        }
    } else if (sr.devices_count == 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "A message was not delivered to any devices.\n");
    }
}

void
signald_display_message(SignaldAccount *sa, const char *who, const char *groupId, gint64 timestamp_micro, gboolean is_sync_message, JsonObject *message_data)
{
    // Signal's integer timestamps are in microseconds, but purple time_t in milliseconds.
    time_t timestamp_milli = timestamp_micro / 1000;
    PurpleMessageFlags flags = 0;
    GString *content = NULL;
    gboolean has_attachment = FALSE;
    if (signald_format_message(sa, message_data, &content, &has_attachment)) {
        if (has_attachment) {
            flags |= PURPLE_MESSAGE_IMAGES;
        }
        if (is_sync_message) {
            flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;
        } else {
            flags |= PURPLE_MESSAGE_RECV;
        }
        if (groupId) {
            PurpleConversation * conv = signald_enter_group_chat(sa->pc, groupId, NULL);
            purple_conv_chat_write(PURPLE_CONV_CHAT(conv), who, content->str, flags, timestamp_milli);
            // TODO: use serv_got_chat_in for more traditonal behaviour
            // though it compares who against chat->nick and sets the SEND/RECV flags itself
            signald_mark_read_chat(sa, timestamp_micro, PURPLE_CONV_CHAT(conv)->users);
        } else {
            if (flags & PURPLE_MESSAGE_RECV) {
                // incoming message
                purple_serv_got_im(sa->pc, who, content->str, flags, timestamp_milli);
            } else {
                // synced message (sent by ourselves via other device)
                PurpleConversation * conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, sa->account);
                if (conv == NULL) {
                    conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, sa->account, who);
                }
                purple_conv_im_write(PURPLE_CONV_IM(conv), who, content->str, flags, timestamp_milli);
            }
            signald_mark_read(sa, timestamp_micro, who);
        }
    } else {
        purple_debug_warning(SIGNALD_PLUGIN_ID, "signald_format_message returned false.\n");
    }
    g_string_free(content, TRUE);
}

int
signald_send_im(PurpleConnection *pc, const gchar *who, const gchar *message, PurpleMessageFlags flags)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    return signald_send_message(sa, who, FALSE, message);
}

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    PurpleConversation *conv = purple_find_chat(pc, id);
    if (conv != NULL) {
        gchar *groupId = (gchar *)purple_conversation_get_data(conv, "name");
        if (groupId != NULL) {
            int ret = signald_send_message(sa, groupId, TRUE, message);
            if (ret > 0) {
                // immediate local echo (ret == 0 indicates delayed local echo)
                purple_conversation_write(conv, sa->uuid, message, flags, time(NULL));
            }
            return ret;
        }
        return -6; // a negative value to indicate failure. chose ENXIO "no such address"
    }
    return -6; // a negative value to indicate failure. chose ENXIO "no such address"
}
