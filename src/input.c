#include "purple_compat.h"
#include "input.h"
#include "defines.h"
#include "structs.h"
#include "link.h"
#include "admin.h"
#include "groups.h"
#include "contacts.h"
#include "message.h"
#include "login.h"

static void
signald_handle_input(SignaldAccount *sa, JsonNode *root)
{
    JsonObject *obj = json_node_get_object(root);
    const gchar *type = json_object_get_string_member(obj, "type");
    purple_debug_info(SIGNALD_PLUGIN_ID, "received type: %s\n", type);

    // catch and display errors
    JsonObject *errobj = json_object_get_object_member(obj, "error");
    if (errobj != NULL) {
        const char *error_type = json_object_get_string_member(obj, "error_type");
        const char *error_message = json_object_get_string_member(errobj, "message");
        if (strstr(error_message, "AuthorizationFailedException")) {
            // TODO: rather check json array error.exceptions for "org.whispersystems.signalservice.api.push.exceptions.AuthorizationFailedException"
            signald_link_or_register(sa);
            return;
        } else if (purple_strequal(type, "subscribe")) {
            // error while subscribing
            signald_link_or_register(sa);
            return;
        } else {
            const char *message = json_object_get_string_member(errobj, "message");
            char *error_message = g_strdup_printf("%s occurred on %s: %s\n", error_type, type, message);
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, error_message);
            g_free(error_message);
            return;
        }
    }

    // no error, actions depending on type
    if (purple_strequal(type, "version")) {
        obj = json_object_get_object_member(obj, "data");
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));
        signald_request_accounts(sa); // Request information on accounts, including our own UUID.

    } else if (purple_strequal(type, "list_accounts")) {
        JsonObject *data = json_object_get_object_member(obj, "data");
        signald_parse_account_list(sa, json_object_get_array_member(data, "accounts"));

    } else if (purple_strequal(type, "subscribe")) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Subscribed!\n");
        // request a sync from other devices
        signald_request_sync(sa);

    } else if (purple_strequal(type, "unsubscribe")) {
        purple_connection_set_state(sa->pc, PURPLE_CONNECTION_DISCONNECTED);

    } else if (purple_strequal(type, "request_sync")) {
        // sync from other devices completed,
        // now pull contacts and groups
        signald_list_contacts(sa);
        signald_request_group_list(sa);

    } else if (purple_strequal(type, "list_contacts")) {
        signald_parse_contact_list(sa,
            json_object_get_array_member(json_object_get_object_member (obj, "data"),
            "profiles")
        );

    } else if (purple_strequal(type, "InternalError")) {
        const char * message = json_object_get_string_member(obj, "message");
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, message);

    } else if (purple_strequal(type, "get_profile")) {
        obj = json_object_get_object_member(obj, "data");
        signald_process_profile(sa, obj);

    } else if (purple_strequal(type, "get_group")) {
        obj = json_object_get_object_member(obj, "data");
        signald_process_groupV2_obj(sa, obj);

    } else if (purple_strequal(type, "list_groups")) {
        obj = json_object_get_object_member(obj, "data");
        signald_parse_groupV2_list(sa, json_object_get_array_member(obj, "groups"));

    } else if (purple_strequal(type, "IncomingMessage")) {
        obj = json_object_get_object_member(obj, "data");
        if (json_object_has_member(obj, "receipt_message")) {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Ignoring receipt.\n");
        } else if (json_object_has_member(obj, "typing_message")) {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Ignoring typing message.\n");
        } else {
            signald_process_incoming_message(sa, obj);
        }

    } else if (purple_strequal(type, "generate_linking_uri")) {
        signald_parse_linking_uri(sa, obj);

    } else if (purple_strequal (type, "finish_link")) {
        signald_process_finish_link(sa, obj);

    } else if (purple_strequal (type, "set_device_name")) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Device name set successfully.\n");

    } else if (purple_strequal(type, "send")) {
        JsonObject *data = json_object_get_object_member(obj, "data");
        signald_send_acknowledged(sa, data);

    } else if (purple_strequal(type, "WebSocketConnectionState")) {
        JsonObject *data = json_object_get_object_member(obj, "data");
        const gchar *state = json_object_get_string_member(data, "state");
        if  (purple_strequal(state, "CONNECTED") && sa->uuid) {
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);
        } else if  (purple_strequal(state, "CONNECTING") && sa->uuid) {
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTING);
        } else if  (purple_strequal(state, "DISCONNECTED") && sa->uuid) {
            // setting the connection state to DISCONNECTED invokes the destruction of the instance
            // we probably do not want that (signald might already be doing a reconnect)
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTING);
            //purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Disconnected.");
        }
    } else {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Ignored message of unknown type '%s'.\n", type);
    }
}

void
signald_parse_input(SignaldAccount *sa, const char * json)
{
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json, -1, NULL)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Error parsing input.");
        return;
    } else {
        JsonNode *root = json_parser_get_root(parser);
        if (root == NULL) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "root node is NULL.");
        } else {
            signald_handle_input(sa, root);
        }
        g_object_unref(parser);
    }
}
