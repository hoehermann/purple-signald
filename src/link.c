#include <sys/stat.h>
#include "defines.h"
#include <purple.h>
#include "structs.h"
#include "comms.h"
#include "login.h"
#include "qrcodegen.h" // TODO: better use libqrencode (available in Debian)
#include <json-glib/json-glib.h>
#include "purple-3/compat.h"

void
signald_set_device_name (SignaldAccount *sa)
{
    g_return_if_fail(sa->uuid != NULL);
    
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "set_device_name");
    json_object_set_string_member(data, "account", sa->uuid);
    const char * device_name = purple_account_get_string(sa->account, "device-name", SIGNALD_DEFAULT_DEVICENAME);
    json_object_set_string_member(data, "device_name", device_name);

    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

static void
signald_scan_qrcode_done(SignaldAccount *sa , PurpleRequestFields *fields)
{
    // Send finish link
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "finish_link");
    json_object_set_string_member(data, "session_id", sa->session_id);
    json_object_set_boolean_member(data, "overwrite", TRUE); // TODO: ask user before overwrting

    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

static void
signald_scan_qrcode_cancel(SignaldAccount *sa , PurpleRequestFields *fields)
{
    purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, "Linking was cancelled.");
}

void
signald_scan_qrcode(SignaldAccount *sa, gchar* qrimgdata, gsize qrimglen)
{
    // Dispalay it for scanning
    PurpleRequestFields* fields = purple_request_fields_new();
    PurpleRequestFieldGroup* group = purple_request_field_group_new(NULL);
    PurpleRequestField* field;

    purple_request_fields_add_group(fields, group);

    field = purple_request_field_image_new(
                "qr_code", "QR code",
                 qrimgdata, qrimglen);
    // TODO: find out why this is unusably tiny on pidgin3
    purple_request_field_group_add_field(group, field);

    purple_request_fields(
        sa->pc, "Signal Protocol", "Link to master device",
        "For linking this account to a Signal master device, "
        "please scan the QR code below. In the Signal App, "
        "go to \"Preferences\" and \"Linked devices\".", 
        fields,
        "Done", G_CALLBACK(signald_scan_qrcode_done), 
        "Cancel", G_CALLBACK(signald_scan_qrcode_cancel),
        purple_request_cpar_from_account(sa->account),
        sa);
}

void
signald_parse_linking_uri(SignaldAccount *sa, JsonObject *obj)
{
    // Linking uri is provided, create the qr-code
    JsonObject *data = json_object_get_object_member(obj, "data");
    const gchar *uri = json_object_get_string_member(data, "uri");
    const gchar *session_id = json_object_get_string_member(data, "session_id");
    sa->session_id = g_strdup(session_id);
    purple_debug_info(SIGNALD_PLUGIN_ID, "Link URI = '%s'\n", uri);
    purple_debug_info(SIGNALD_PLUGIN_ID, "Sesison ID = '%s'\n", session_id);

    enum qrcodegen_Ecc errCorLvl = qrcodegen_Ecc_LOW;
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(uri, tempBuffer, qrcode, errCorLvl, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
    if (ok) {
        int border = 4;
        int zoom = 4;
        int qrcodesize = qrcodegen_getSize(qrcode);
        int imgwidth = (border*2+qrcodesize)*zoom;
        // poor man's PBM encoder
        gchar *head = g_strdup_printf("P1 %d %d ", imgwidth, imgwidth);
        int headlen = strlen(head);
        gsize qrimglen = headlen+imgwidth*2*imgwidth*2;
        gchar qrimgdata[qrimglen];
        strncpy(qrimgdata, head, headlen+1);
        g_free(head);
        gchar *qrimgptr = qrimgdata+headlen;
        // from https://github.com/nayuki/QR-Code-generator/blob/master/c/qrcodegen-demo.c
        for (int y = 0; y/zoom < qrcodesize + border*2; y++) {
            for (int x = 0; x/zoom < qrcodesize + border*2; x++) {
                *qrimgptr++ = qrcodegen_getModule(qrcode, x/zoom - border, y/zoom - border) ? '1' : '0';
                *qrimgptr++ = ' ';
            }
        }
        signald_scan_qrcode(sa, qrimgdata, qrimglen);
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "qrcodegen failed.\n");
    }

}

void
signald_verify_ok_cb(SignaldAccount *sa, const char* input)
{
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "verify");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "code", input);
    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);

    // TODO: Is there an acknowledge on successful registration? If yes,
    //       subscribe afterwards or display an error otherwise
    // signald_subscribe(sa);
    purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Verification code was sent. Reconnect needed.");
}

void
signald_link_or_register(SignaldAccount *sa)
{
    // TODO: split the function into link and register
    const char *username = purple_account_get_username(sa->account);
    JsonObject *data = json_object_new();

    if (purple_account_get_bool(sa->account, "link", TRUE)) {
        // Link Pidgin to the master device.
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "generate_linking_uri");
        signald_send_json_or_display_error(sa, data);
    } else {
        // Register username (phone number) as new signal account, which
        // requires a registration. From the signald readme:
        // {"type": "register", "username": "+12024561414"}
        // TODO: test this for v1

        json_object_set_string_member(data, "type", "register");
        json_object_set_string_member(data, "username", username);
        signald_send_json_or_display_error(sa, data);

        // TODO: Test registering thoroughly
        purple_request_input (sa->pc, SIGNALD_DIALOG_TITLE, "Verify registration",
                              "Please enter the code that you have received for\n"
                              "verifying the registration",
                              "000-000", FALSE, FALSE, NULL,
                              "OK", G_CALLBACK (signald_verify_ok_cb),
                              "Cancel", NULL,
                              purple_request_cpar_from_account(sa->account), sa);
    }

    json_object_unref(data);
}

void
signald_process_account(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);

    const char *username = purple_account_get_username(sa->account);
    JsonObject *address = json_object_get_object_member(obj, "address");
    const char *uuid = json_object_get_string_member(address, "uuid");
    const char *number = json_object_get_string_member(address, "number");
    const char *account_id = json_object_get_string_member(obj, "account_id");
    if (purple_strequal(account_id, username) || purple_strequal(number, username) || purple_strequal(uuid, username)) {
        // this is the current account
        sa->account_exists = TRUE;

        sa->uuid = g_strdup(uuid);
        purple_debug_info(SIGNALD_PLUGIN_ID, "Account uuid: %s\n", sa->uuid);

        gboolean pending = json_object_has_member(obj, "pending") && json_object_get_boolean_member(obj, "pending");
        purple_debug_info(SIGNALD_PLUGIN_ID, "Account %s pending: %d\n", account_id, pending);
        if (pending) {
            // account is pending verification, try to link again
            signald_link_or_register(sa);
        } else {
            // account allegedly is ready for usage
            signald_subscribe(sa);
        }
    }
}

void
signald_parse_account_list(SignaldAccount *sa, JsonArray *data)
{
    sa->account_exists = FALSE;
    json_array_foreach_element(data, signald_process_account, sa); // lookup signald account

    // if Purple account does not exist in signald, link or register
    if (!sa->account_exists) {
        signald_link_or_register(sa);
    }
}

/*
 * Request information on accounts, including our own UUID.
 * This should be the first request after making a connection.
 */
void
signald_request_accounts(SignaldAccount *sa) {
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "list_accounts");
    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

void
signald_process_finish_link(SignaldAccount *sa, JsonObject *obj) {
    // sync account's UUID with the one reported by signal
    obj = json_object_get_object_member(obj, "data");
    if (obj) {
        obj = json_object_get_object_member(obj, "address");
        if (obj) {
            sa->uuid = g_strdup(json_object_get_string_member(obj, "uuid"));
        }
    }
    // publish device name
    signald_set_device_name(sa);
    signald_subscribe(sa);
}
