#include <sys/stat.h>

#include "libsignald.h"


void
signald_scan_qrcode_done (SignaldAccount *sa , PurpleRequestFields *fields)
{
  // Nothing to do here
}

void
signald_scan_qrcode (SignaldAccount *sa)
{
    // Read QR code png file
    gchar* qrimgdata;
    gsize qrimglen;

    if (g_file_get_contents (SIGNALD_TMP_QRFILE, &qrimgdata, &qrimglen, NULL)) {

        // Dispalay it for scanning
        PurpleRequestFields* fields = purple_request_fields_new();
        PurpleRequestFieldGroup* group = purple_request_field_group_new(NULL);
        PurpleRequestField* field;

        purple_request_fields_add_group(fields, group);

        field = purple_request_field_image_new(
                    "qr_code", _("QR code"),
                     qrimgdata, qrimglen);
        purple_request_field_group_add_field(group, field);

        purple_request_fields(
            sa->pc, _("Signal Protocol"), _("Link to master device"),
            _("For linking this account to a Signal master device, "
              "please scan the  QR code below. In the Signal App, "
              "go to \"Preferences\" and \"Linked devices\"."), fields,
            _("Done"), G_CALLBACK(signald_scan_qrcode_done), _("Close"), NULL,
            sa->account, purple_account_get_username(sa->account), NULL, sa);

        g_free(qrimgdata);
    }
}

void
signald_parse_linking_uri (SignaldAccount *sa, JsonObject *obj)
{
    // Linking uri is provided, create the qr-code
    JsonObject *data = json_object_get_object_member(obj, "data");
    const gchar *uri = json_object_get_string_member(data, "uri");
    purple_debug_info (SIGNALD_PLUGIN_ID, "LINK URI = '%s'\n", uri);

    remove (SIGNALD_TMP_QRFILE);  // remove any old files

    // Start the system utility for creating the qr code
    // TODO: It would be better to do this be means of some libs
    //       instead of calling an external program via system () here
    gchar *qr_command = g_strdup_printf (SIGNALD_QRCREATE_CMD, uri);
    int ok = system (qr_command);

    struct stat file_stat;
    if ((ok < 0) || (stat (SIGNALD_TMP_QRFILE, &file_stat) < 0)) {
        gchar *text = g_strdup_printf ("QR code creation failed:\n%s",
                                        qr_command);
        purple_notify_error (NULL, SIGNALD_DIALOG_TITLE, SIGNALD_DIALOG_LINK, text);
        g_free (text);
        return;
    } else {
        // Display the QR code for scanning
        signald_scan_qrcode (sa);
    }
    g_free (qr_command);
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
    signald_subscribe(sa);
}

void
signald_link_or_register (SignaldAccount *sa)
{
    // TODO: split the function into link and register
    const char *username = purple_account_get_username(sa->account);
    JsonObject *data = json_object_new();

    if (purple_account_get_bool(sa->account, "link", TRUE)) {
        // Link Pidgin to the master device. This fails, if the user is already
        // known. Therefore, remove the related user data from signald configuration
        gchar *user_file = g_strdup_printf (SIGNALD_DATA_FILE,
                                            purple_user_dir (), username);
        remove (user_file);
        g_free (user_file);

        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "link");
        if (!signald_send_json(sa, data)) {
            //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write link message."));
        }
    } else {
        // Register username (phone number) as new signal account, which
        // requires a registration. From the signald readme:
        // {"type": "register", "username": "+12024561414"}

        json_object_set_string_member(data, "type", "register");
        json_object_set_string_member(data, "username", username);
        if (!signald_send_json(sa, data)) {
            //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
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

    const char *username = json_object_get_string_member(obj, "username");
    if (purple_strequal (username, purple_account_get_username(sa->account))) {
        // The current account
        gboolean registered = json_object_get_boolean_member (obj, "registered");
        purple_debug_info (SIGNALD_PLUGIN_ID,
                           "Account %s registered: %d\n", username, registered);
        if (registered)
            signald_subscribe (sa); // Subscribe when account is registered
        else
            signald_link_or_register (sa);  // Link or register if not
    }
}

void
signald_parse_account_list(SignaldAccount *sa, JsonArray *data)
{
    if (! json_array_get_length (data))
      signald_link_or_register (sa);

    json_array_foreach_element(data, signald_process_account, sa);
}
