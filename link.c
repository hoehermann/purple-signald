#define _DEFAULT_SOURCE // for gethostname in unistd.h
#include <unistd.h>
#include <sys/stat.h>

#include "libsignald.h"
#include "qrcodegen.h"

static char device_name[HOST_NAME_MAX+1];

void
signald_set_device_name (SignaldAccount *sa)
{
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "set_device_name");
    json_object_set_string_member(data, "account", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "device_name", device_name);

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write finish_link message."));
    }
    json_object_unref(data);
}

void
signald_scan_qrcode_done (SignaldAccount *sa , PurpleRequestFields *fields)
{
    // Send finish link
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "finish_link");
    json_object_set_string_member(data, "session_id", sa->session_id);
    
    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write finish_link message."));
    }
    json_object_unref(data);
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
                "qr_code", _("QR code"),
                 qrimgdata, qrimglen);
    purple_request_field_group_add_field(group, field);

    gchar *msg = g_strdup_printf(_("Link to master device as \"%s\""), device_name);

    purple_request_fields(
        sa->pc, _("Signal Protocol"), msg,
        _("For linking this account to a Signal master device, "
          "please scan the QR code below. In the Signal App, "
          "go to \"Preferences\" and \"Linked devices\"."), fields,
        _("Done"), G_CALLBACK(signald_scan_qrcode_done), _("Close"), NULL,
        sa->account, purple_account_get_username(sa->account), NULL, sa);

    g_free(msg);
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
signald_parse_linking_successful (void)
{
    // Linking was successful
    purple_debug_info (SIGNALD_PLUGIN_ID, "Linking successful\n");
    purple_notify_close_with_handle (purple_notify_get_handle ());
    remove (SIGNALD_TMP_QRFILE);
}

void
signald_parse_linking_error (SignaldAccount *sa, JsonObject *obj)
{
    // Error: Linking was not successful
    JsonObject *data = json_object_get_object_member(obj, "data");
    const gchar *msg = json_object_get_string_member(data, "message");
    gchar *text = g_strdup_printf ("Linking not successful!\n%s", msg);
    purple_notify_error (NULL, SIGNALD_DIALOG_TITLE, SIGNALD_DIALOG_LINK, text);
    g_free (text);

    json_object_unref(data);
}

void
signald_verify_ok_cb (SignaldAccount *sa, const char* input)
{
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "verify");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "code", input);
    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write verification message."));
    }
    json_object_unref(data);

    // TODO: Is there an acknowledge on successful registration? If yes,
    //       subscribe afterwards or display an error otherwise
    // signald_subscribe(sa);
    purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Verification code was sent. Reconnect needed."));
}

void
signald_link_or_register(SignaldAccount *sa)
{
    // TODO: split the function into link and register
    const char *username = purple_account_get_username(sa->account);
    JsonObject *data = json_object_new();

    if (purple_account_get_bool(sa->account, "link", TRUE)) {
        // Link Pidgin to the master device. This fails, if the user is already
        // known. Therefore, remove the related user data from signald configuration
        gchar *user_file = g_strdup_printf(SIGNALD_DATA_FILE, purple_user_dir(), username);
        remove(user_file);
        g_free(user_file);

        // Get desired device name
        if (gethostname(device_name, HOST_NAME_MAX)) {
            strcpy(device_name, SIGNALD_DEFAULT_DEVICENAME);
        }
        strcpy(device_name, purple_account_get_string(sa->account, "device-name", device_name));

        // Send the link request
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "generate_linking_uri");

        if (!signald_send_json(sa, data)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write link message."));
        }
    } else {
        // Register username (phone number) as new signal account, which
        // requires a registration. From the signald readme:
        // {"type": "register", "username": "+12024561414"}
        // TODO: test this for v1

        json_object_set_string_member(data, "type", "register");
        json_object_set_string_member(data, "username", username);
        if (!signald_send_json(sa, data)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write registration message."));
        }

        // TODO: Test registering thoroughly
        purple_request_input (sa->pc, SIGNALD_DIALOG_TITLE, "Verify registration",
                              "Please enter the code that you have received for\n"
                              "verifying the registration",
                              "000-000", FALSE, FALSE, NULL,
                              "OK", G_CALLBACK (signald_verify_ok_cb),
                              "Cancel", NULL,
                              sa->account, username, NULL, sa);
    }

    json_object_unref(data);
}

void
signald_process_account(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);
        
    const char *username = json_object_get_string_member(obj, "account_id");
    purple_debug_info(SIGNALD_PLUGIN_ID, "Account = Username <=> %s = %s\n", username, purple_account_get_username(sa->account));
    if (purple_strequal(username, purple_account_get_username(sa->account))) {
        // this is the current account
        sa->account_exists = TRUE;
        obj = json_object_get_object_member(obj, "address");
        sa->uuid = g_strdup(json_object_get_string_member(obj, "uuid"));
        purple_debug_info(SIGNALD_PLUGIN_ID, "Account uuid: %s\n", sa->uuid);
        gboolean pending = json_object_get_boolean_member (obj, "pending");
        purple_debug_info(SIGNALD_PLUGIN_ID, "Account %s registered: %d\n", username, !pending);
        if (!pending) {
            signald_subscribe(sa); // Subscribe if account is registered
        } else {
            signald_link_or_register(sa);  // Link or register if not
        }
    }
}

void
signald_parse_account_list(SignaldAccount *sa, JsonArray *data)
{
    sa->account_exists = FALSE;
    json_array_foreach_element(data, signald_process_account, sa); // lookup signald account by Purple username (number)
    
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
    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write list account message."));
    }
    json_object_unref(data);
}
