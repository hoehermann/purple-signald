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

void
signald_handle_input(SignaldAccount *sa, const char * json)
{
    JsonParser *parser = json_parser_new();
    JsonNode *root;

    if (!json_parser_load_from_data(parser, json, -1, NULL)) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "Error parsing input: %s\n", json);
        return;
    }

    root = json_parser_get_root(parser);

    if (root != NULL) {
        JsonObject *obj = json_node_get_object(root);
        const gchar *type = json_object_get_string_member(obj, "type");
        purple_debug_info(SIGNALD_PLUGIN_ID, "received type: %s\n", type);

        // error handling
        // TODO: which messages use boolean error fields? which use objects?

        //gboolean is_error = json_object_get_boolean_member(obj, "error");
        //if (is_error) {
        //    purple_debug_error(SIGNALD_PLUGIN_ID, "%s\n", type);
            // TODO: which errors are "hard errors" (need reconnect?)
            //purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, type);
        //} else {

        // subscribe can have an error object
        JsonObject *errobj = json_object_get_object_member(obj, "error");
        if (errobj != NULL) {
            const char *error_type = json_object_get_string_member(obj, "error_type");
            const char *error_message = json_object_get_string_member(errobj, "message");
            if (strstr(error_message, "AuthorizationFailedException")) {
                // TODO: rather check json array error.exceptions for "org.whispersystems.signalservice.api.push.exceptions.AuthorizationFailedException"
                signald_link_or_register(sa);
                return;
            }
            if (purple_strequal(type, "subscribe") && !purple_strequal(error_type, "InternalError")) {
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
            // v1 ok
            obj = json_object_get_object_member(obj, "data");
            purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));

            signald_request_accounts(sa); // Request information on accounts, including our own UUID.

        } else if (purple_strequal(type, "list_accounts")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            signald_parse_account_list(sa, json_object_get_array_member(data, "accounts"));

        } else if (purple_strequal(type, "subscribe")) {
            // v1 ok
            purple_debug_info(SIGNALD_PLUGIN_ID, "Subscribed!\n");
            // request a sync; on repsonse, contacts and groups are requested
            signald_request_sync(sa);

        } else if (purple_strequal(type, "request_sync")) {
            signald_list_contacts(sa);
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "list_contacts")) {
            // TODO: check v1
            signald_parse_contact_list(sa,
                json_object_get_array_member(json_object_get_object_member (obj, "data"),
                "profiles"));

        } else if (purple_strequal(type, "InternalError") || purple_strequal(type, "InternalError\n")) {
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
            SignaldMessage msg;

            if (signald_parse_message(sa, json_object_get_object_member(obj, "data"), &msg)) {
                purple_debug_info(SIGNALD_PLUGIN_ID, "signald_parse_message returned type %d.\n", msg.type);
                switch(msg.type) {
                    case SIGNALD_MESSAGE_TYPE_DIRECT:
                        signald_process_direct_message(sa, &msg);
                        break;
                    case SIGNALD_MESSAGE_TYPE_GROUPV2:
                        signald_process_groupV2_message(sa, &msg);
                        break;
                }
            }
        } else if (purple_strequal(type, "generate_linking_uri")) {
            signald_parse_linking_uri(sa, obj);

        } else if (purple_strequal (type, "finish_link")) {
            signald_process_finish_link(sa, obj);

        } else if (purple_strequal (type, "set_device_name")) {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Device name set successfully.\n");

        } else if (purple_strequal(type, "group_created")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "group_updated")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "send")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            signald_send_acknowledged(sa, data);

        } else if (purple_strequal(type, "WebSocketConnectionState")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            const gchar *state = json_object_get_string_member(data, "state");
            if  (purple_strequal(state, "CONNECTED") && sa->uuid) {
                purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);
            }
            // TODO: reflect unexpected disconnection
            //purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Disconnected.");
        } else {
            purple_debug_error(SIGNALD_PLUGIN_ID, "Ignored message of unknown type '%s'.\n", type);
        }
    }

    g_object_unref(parser);
}
