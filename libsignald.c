/*
 *   signald plugin for libpurple
 *   Copyright (C) 2016 Hermann Höhne
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GNUC__
#include <unistd.h>
#endif
#include <errno.h>
#include <sys/socket.h> // for recv
#include <sys/un.h> // for sockaddr_un
#include <sys/stat.h> // for chmod
#include <gmodule.h>

#ifdef ENABLE_NLS
// TODO: implement localisation
#else
#      define _(a) (a)
#      define N_(a) (a)
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#ifdef __clang__
#pragma GCC diagnostic ignored "-W#pragma-messages"
#endif

#include "json_compat.h"
#include "purple_compat.h"

#pragma GCC diagnostic pop

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#ifndef SIGNALD_PLUGIN_VERSION
#error Must set SIGNALD_PLUGIN_VERSION in Makefile
#endif
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"
#define SIGNAL_DEFAULT_GROUP "Signal"

#define SIGNALD_DIALOG_TITLE "Signal Protocol"
#define SIGNALD_DIALOG_LINK "Link to Signal App"

#define SIGNALD_TIME_OUT 10
#define SIGNALD_DEFAULT_SOCKET "/var/run/signald/signald.sock"
#define SIGNALD_DEFAULT_SOCKET_LOCAL "/tmp/signald.sock"
#define SIGNALD_START "signald -s " SIGNALD_DEFAULT_SOCKET_LOCAL " -d " SIGNALD_DATA_PATH " &"

#define SIGNALD_DATA_PATH "%s/signald"
#define SIGNALD_DATA_FILE SIGNALD_DATA_PATH "/data/%s"
#define SIGNALD_PID_FILE SIGNALD_DATA_PATH "/pid"

#define SIGNALD_TMP_QRFILE "/tmp/signald_link_purple_qrcode.png"
#define SIGNALD_PID_FILE_QR SIGNALD_DATA_PATH "/pidqr"
#define SIGNALD_QRCREATE_CMD "qrencode -s 6 -o " SIGNALD_TMP_QRFILE " '%s'"
#define SIGNALD_QR_MSG "echo Link by scanning QR with Signal App"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

#define SIGNALD_UNKNOWN_SOURCE_NUMBER "unknown"

#define SIGNALD_ERR_NONEXISTUSER "Attempted to connect to a non-existant user"
#define SIGNALD_ERR_AUTHFAILED   "Authorization failed"

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;

    int fd;
    guint watcher;

    // Maps signal group IDs to libpurple PurpleConversation objects that represent those chats.
    GHashTable *groups;
} SignaldAccount;

static int signald_usages = 0;

static void
signald_add_purple_buddy(SignaldAccount *sa, const char *username, const char *alias);

void
signald_request_group_list(SignaldAccount *sa);

void
signald_update_group(SignaldAccount *sa, const char *groupId, const char *groupName, JsonArray *members);

void
signald_quit_group(SignaldAccount *sa, const char *groupId);

static const char *
signald_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
    return "signal";
}

void
signald_save_pidfile (const char *pid_file_name)
{
    int pid = getpid ();
    FILE *pid_file = fopen (pid_file_name, "w");
    if (pid_file) {
        fprintf (pid_file, "%d\n", pid);
        fclose (pid_file);
    }
}

void
signald_kill_process (const char *pid_file_name)
{
    pid_t pid;
    FILE *pid_file = fopen (pid_file_name, "r");
    if (pid_file) {
        if (fscanf (pid_file, "%d\n", &pid)) {
            fclose (pid_file);
        } else {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Failed to read signald pid from file.");
        }
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Failed to access signald pidfile.");
    }
    kill(pid, SIGTERM);
    remove(pid_file_name);
}

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

int
signald_strequalprefix (const char *s1, const char *s2)
{
    int l1 = strlen (s1);
    int l2 = strlen (s2);

    return 0 == strncmp (s1, s2, l1 < l2 ? l1 : l2);
}
void
signald_assume_buddy_online(PurpleAccount *account, PurpleBuddy *buddy)
{
    if (purple_account_get_bool(account, "fake-online", TRUE)) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_online %s\n", buddy->name);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_ONLINE, NULL);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_MOBILE, NULL);
    }
}

void
signald_assume_all_buddies_online(SignaldAccount *sa)
{
    GSList *buddies = purple_find_buddies(sa->account, NULL);
    while (buddies != NULL) {
        signald_assume_buddy_online(sa->account, buddies->data);
        buddies = g_slist_delete_link(buddies, buddies);
    }
}

PurpleConversation *
signald_find_conversation(const char *username, PurpleAccount *account) {
    PurpleIMConversation *imconv = purple_conversations_find_im_with_account(username, account);
    if (imconv == NULL) {
        imconv = purple_im_conversation_new(account, username);
    }

    PurpleConversation *conv = PURPLE_CONVERSATION(imconv);
    if (conv == NULL) {
        imconv = purple_conversations_find_im_with_account(username, account);
        conv = PURPLE_CONVERSATION(imconv);
    }

    return conv;
}

void
signald_process_message(SignaldAccount *sa,
        const gchar *sender, const gchar *content, time_t timestamp, gboolean fromMe,
        const gchar *groupid_str, const gchar *groupname, GString *attachments)
{
    PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;

    if (attachments->len) {
        flags |= PURPLE_MESSAGE_IMAGES;
    }

    g_string_append(attachments, content);

    if (! attachments->len) {
      return;
    }

    PurpleConversation *conv = NULL;

    if (groupid_str) {
      conv = (PurpleConversation *)g_hash_table_lookup(sa->groups, groupid_str);
    } else {
      conv = signald_find_conversation(sender, sa->account);
    }

    if (fromMe) {
        // special handling of messages sent by self incoming from remote
        // copied from hoehermann/purple-gowhatsapp/libgowhatsapp.c
        flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;

        purple_conversation_write(conv, sender, attachments->str, flags, timestamp);
    } else if (groupid_str != NULL) {
        purple_serv_got_chat_in(sa->pc, purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)), sender, flags, attachments->str, timestamp);
    } else {
        purple_serv_got_im(sa->pc, sender, attachments->str, flags, timestamp);
    }
}

void
signald_parse_attachment(JsonObject *obj, GString *message)
{
    const char *type = json_object_get_string_member(obj, "contentType");
    const char *fn = json_object_get_string_member(obj, "storedFilename");
    if (purple_strequal(type, "image/jpeg") || purple_strequal(type, "image/png")) {
        // TODO: forward "access denied" error to UI
        PurpleStoredImage *img = purple_imgstore_new_from_file(fn);
        size_t size = purple_imgstore_get_size(img);
        int img_id = purple_imgstore_add_with_id(g_memdup(purple_imgstore_get_data(img), size), size, NULL);
        g_string_append_printf(message, "<IMG ID=\"%d\"/><br/>", img_id);
    } else {
        //TODO: Receive file using libpurple's file transfer API
        g_string_append_printf(message, "<a href=\"file://%s \">Attachment (type: %s)</a><br/>", fn, type);
    }
    purple_debug_info(SIGNALD_PLUGIN_ID, "Attachment: %s", message->str);
}

GString *
signald_prepare_attachments_message(JsonObject *obj) {
    JsonArray *attachments = json_object_get_array_member(obj, "attachments");
    guint len = json_array_get_length(attachments);
    GString *attachments_message = g_string_sized_new(len * 100); // Preallocate buffer. Exact size doesn't matter. It grows automatically if it is too small
    for (guint i=0; i < len; i++) {
        signald_parse_attachment(json_array_get_object_element(attachments, i), attachments_message);
    }
    return attachments_message;
}

void
signald_parse_message(SignaldAccount *sa, JsonObject *obj)
{
    // Signal's integer timestamps are in milliseconds
    // timestamp, timestampISO and dataMessage.timestamp seem to always be the same value (message sent time)
    // serverTimestamp is when the server received the message
    time_t timestamp = json_object_get_int_member(obj, "timestamp") / 1000;

    JsonObject *syncMessage = json_object_get_object_member(obj, "syncMessage");

    JsonObject *dataMessage = NULL;
    const gchar *source;

    if (syncMessage != NULL) {
        JsonObject *sent = json_object_get_object_member(syncMessage, "sent");

        if (sent == NULL) {
            return;
        }

        source = json_object_get_string_member(sent, "destination");
        dataMessage = json_object_get_object_member(sent, "message");
    } else {
        // gboolean isreceipt = json_object_get_boolean_member(obj, "isReceipt");
        // isReceipt() can be false even if message actually is a receipt
        // TODO: optioally display receipt in the conversation window
        // purple_conv_chat_write(to, username, msg, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));

        source = json_object_get_string_member(obj, "source");
        dataMessage = json_object_get_object_member(obj, "dataMessage");
    }

    if (dataMessage == NULL) {
        return;
    }

    if (source == NULL) {
        source = SIGNALD_UNKNOWN_SOURCE_NUMBER;
    }

    const gchar *message = json_object_get_string_member(dataMessage, "message");

    JsonObject *groupInfo = json_object_get_object_member(dataMessage, "groupInfo");
    const gchar *type = NULL;
    const gchar *groupid_str = NULL;
    const gchar *groupname = NULL;

    if (groupInfo) {
        type = json_object_get_string_member(groupInfo, "type");
        groupid_str = json_object_get_string_member(groupInfo, "groupId");
        groupname = json_object_get_string_member(groupInfo, "name");
    }

    if (purple_strequal(type, "UPDATE")) {
        signald_update_group(sa, groupid_str, groupname, json_object_get_array_member(groupInfo, "members"));
    } else if (purple_strequal(type, "QUIT")) {
        char *username = (char *)purple_account_get_username(sa->account);
        char *quit_source = (char *)json_object_get_string_member(obj, "source");

        if (purple_strequal(username, quit_source)) {
            signald_quit_group(sa, groupid_str);
        } else {
            signald_update_group(sa, groupid_str, groupname, json_object_get_array_member(groupInfo, "members"));
        }
    } else if ((groupInfo == NULL) || purple_strequal(type, "DELIVER")) {
        GString *attachments_message = signald_prepare_attachments_message(dataMessage);

        signald_process_message(sa, source, message, timestamp, (syncMessage != NULL), groupid_str, groupname, attachments_message);

        g_string_free(attachments_message, TRUE);
    }
}

void
signald_process_contact(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);
    const char *username = json_object_get_string_member(obj, "number");
    const char *alias = json_object_get_string_member(obj, "name");
    signald_add_purple_buddy(sa, username, alias);
}

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *data)
{
    json_array_foreach_element(data, signald_process_contact, sa);
}

/******************************
 *
 * Basic group handling logic.
 *
 ******************************/

int
signald_check_group_membership(JsonArray *members, char *username)
{
    for (GList *this_member = json_array_get_elements(members); this_member != NULL; this_member = this_member->next) {
        const char *member_name = json_node_get_string((JsonNode *)(this_member->data));

        if (purple_strequal(username, member_name)) {
            return 1;
        }
    }

    return 0;
}

void
signald_join_group(SignaldAccount *sa, const char *groupId, const char *groupName, JsonArray *members)
{
    PurpleConversation *conv = (PurpleConversation *)g_hash_table_lookup(sa->groups, groupId);

    if (conv != NULL) {
        // Belt and suspenders check to make sure we don't join the same group twice.

        return;
    }

    // For now we just set up the group IDs by numbering them as they arrive.
    // I don't know if these things need to be persistent across sessions.
    // If so, this is a bad idea, since the ordering may be non-deterministic.

    int id = g_str_hash(groupId);
    GList *users = NULL;
    GList *flags = NULL;

    for (int i = 0; i < json_array_get_length(members); i++) {
        char *user = (char *)json_node_get_string(json_array_get_element(members, i));

        users = g_list_append(users, user);
        flags = g_list_append(flags, GINT_TO_POINTER(PURPLE_CBFLAGS_NONE));
    }

    conv = serv_got_joined_chat(sa->pc, id, groupName);
    purple_chat_conversation_add_users(PURPLE_CONV_CHAT(conv), users, NULL, flags, FALSE);

    g_hash_table_insert(sa->groups, g_strdup(groupId), conv);

    g_list_free(users);
    g_list_free(flags);
}

void
signald_update_group(SignaldAccount *sa, const char *groupId, const char *groupName, JsonArray *members)
{
    PurpleConversation *conv = g_hash_table_lookup(sa->groups, groupId);

    // Look it see if we're a member of the group.

    char *username = (char *)purple_account_get_username(sa->account);
    int in_group = signald_check_group_membership(members, username);

    if ((conv == NULL) && (! in_group)) {
        // Chat that we neither know about nor we're in?  Ignore it.

        return;
    } else if ((conv == NULL) && in_group) {
        // Brand new chat and we're a member?  Join it!

        signald_join_group(sa, groupId, groupName, members);
        return;
    } else if ((conv != NULL) && ! in_group) {
        // Existing chat but we're *not* a member?  Quit it.

        signald_quit_group(sa, groupId);
        return;
    }

    // So conv != NULL and in_group, which means we are both in this group and we know about it,
    // so this is a true update.
    //
    // Next, search for users we know about that were removed from the member list (because they left),
    // and let's take them out of the chat.

    GList *current_users = purple_conv_chat_get_users(PURPLE_CONV_CHAT(conv));
    GList *remove_users = NULL;

    for (GList *this_user = current_users; this_user != NULL; this_user = this_user->next) {
        username = ((PurpleConvChatBuddy *)this_user->data)->name;

        if (! signald_check_group_membership(members, username)) {
            printf("going to remove %s from %s\n", username, groupName);

            remove_users = g_list_append(remove_users, g_strdup(username));
        }
    }

    purple_conv_chat_remove_users(PURPLE_CONV_CHAT(conv), remove_users, NULL);

    g_list_free_full(remove_users, g_free);

    // Search for users in the member list not in the user list and add them.

    GList *current_members = json_array_get_elements(members);

    for (GList *this_member = current_members; this_member != NULL; this_member = this_member->next) {
        const char *member_name = json_node_get_string((JsonNode *)(this_member->data));
        int found = 0;

        current_users = purple_conv_chat_get_users(PURPLE_CONV_CHAT(conv));

        for (GList *this_user = current_users; this_user != NULL; this_user = this_user->next) {
            const char *user_name = ((PurpleConvChatBuddy *)this_user->data)->name;

            if (purple_strequal(user_name, member_name)) {
                found = 1;
                break;
            }
        }

        if (! found) {
            purple_conv_chat_add_user(PURPLE_CONV_CHAT(conv), member_name, NULL, PURPLE_CBFLAGS_NONE, FALSE);
        }
    }
}

void
signald_quit_group(SignaldAccount *sa, const char *groupId)
{
    PurpleConversation *conv = (PurpleConversation *)g_hash_table_lookup(sa->groups, groupId);

    if (conv == NULL) {
        return;
    }

    int id = purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv));

    g_hash_table_remove(sa->groups, groupId);

    serv_got_chat_left(sa->pc, id);
}

void
signald_process_group(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);

    signald_update_group(sa,
                         json_object_get_string_member(obj, "groupId"),
                         json_object_get_string_member(obj, "name"),
                         json_object_get_array_member(obj, "members"));
}

void
signald_parse_group_list(SignaldAccount *sa, JsonArray *groups)
{
    json_array_foreach_element(groups, signald_process_group, sa);
}

gchar *
signald_find_groupid_for_conv_id(SignaldAccount *sa, int id)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iter, sa->groups);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (purple_conv_chat_get_id(PURPLE_CONV_CHAT((PurpleConversation *)value)) == id) {
            return (gchar *)key;
        }
    }

    return NULL;
}

/******************************/
/******************************/
/******************************/

gboolean
signald_send_str(SignaldAccount *sa, char *s)
{
    int l = strlen(s);
    int w = write(sa->fd, s, l);
    if (w != l) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "wrote %d, wanted %d, error is %s\n", w, l, strerror(errno));
        return 0;
    }
    return 1;
}

gboolean
signald_send_json(SignaldAccount *sa, JsonObject *data)
{
    gboolean success;
    char *json = json_object_to_string(data);
    purple_debug_info(SIGNALD_PLUGIN_ID, "Sending: %s\n", json);
    success = signald_send_str(sa, json);
    if (success) {
        success = signald_send_str(sa, "\n");
    }
    g_free(json);
    return success;
}

void
signald_subscribe (SignaldAccount *sa)
{
    // subscribe to the configured number
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "subscribe");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    if (!signald_send_json (sa, data)) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    // Load the contact list
    json_object_set_string_member(data, "type", "list_contacts");
    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request contacts."));
    }

    json_object_unref(data);

    signald_request_group_list(sa);
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
signald_parse_linking_successful (SignaldAccount *sa, JsonObject *obj)
{
        // Linking was successful
        purple_notify_close_with_handle (purple_notify_get_handle ());
        remove (SIGNALD_TMP_QRFILE);
        // FIXME: Sometimes, messages are not received by pidgin after
        //        linking to the main account and are only shown there.
        //        Is it robust to subscribe here?
        signald_subscribe (sa);
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

    gchar *pid_file = g_strdup_printf (SIGNALD_PID_FILE_QR, purple_user_dir ());
    signald_kill_process (pid_file);
    purple_notify_close_with_handle (purple_notify_get_handle ());
    g_free (pid_file);

    remove (SIGNALD_TMP_QRFILE);

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
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
        }
    } else {
        // Register username (phone number) as new signal account, which
        // requires a registration. From the signald readme:
        // {"type": "register", "username": "+12024561414"}

        json_object_set_string_member(data, "type", "register");
        json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
        if (!signald_send_json(sa, data)) {
            //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
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

        if (purple_strequal(type, "version")) {
            obj = json_object_get_object_member(obj, "data");
            purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));

        } else if (purple_strequal(type, "success")) {
            // TODO: mark message as delayed (maybe do not echo) until success is reported
            purple_debug_info(SIGNALD_PLUGIN_ID, "Success noticed.\n");

        } else if (purple_strequal(type, "subscribed")) {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Subscribed!\n");
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);
            signald_assume_all_buddies_online(sa);

        } else if (purple_strequal(type, "message")) {
            signald_parse_message(sa, json_object_get_object_member(obj, "data"));

        } else if (purple_strequal(type, "linking_uri")) {
            signald_parse_linking_uri(sa, obj);

        } else if (purple_strequal (type, "linking_successful")) {
            signald_parse_linking_successful(sa, obj);

        } else if (purple_strequal (type, "linking_error")) {
            signald_parse_linking_error(sa, obj);

        } else if (signald_strequalprefix(type, "linking_")) {
            gchar *text = g_strdup_printf("Unknown message related to linking:\n%s", type);
            purple_notify_warning (NULL, SIGNALD_DIALOG_TITLE, SIGNALD_DIALOG_LINK, text);
            g_free (text);

        } else if (purple_strequal(type, "contact_list")) {
            signald_parse_contact_list(sa, json_object_get_array_member(obj, "data"));

        } else if (purple_strequal(type, "group_list")) {
            obj = json_object_get_object_member(obj, "data");
            signald_parse_group_list(sa, json_object_get_array_member(obj, "groups"));

        } else if (purple_strequal(type, "group_created")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "group_updated")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "unexpected_error")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            const gchar *message = json_object_get_string_member(data, "message");
            // Analyze the error: Check for failed authorization or unknown user.
            // Do we have to link or register the account?
            // FIXME: This does not work reliably, i.e.,
            //          * there is a connection error without but no attempt to link or register
            //          * the account is enabled and the contacts are loaded but sending a message won't work
            if (message && *message) {
                  if ((signald_strequalprefix (message, SIGNALD_ERR_NONEXISTUSER))
                      || (signald_strequalprefix (message, SIGNALD_ERR_AUTHFAILED))                 ) {
                      signald_link_or_register (sa);
                  }
            } else {
                purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_OTHER_ERROR, _("signald reported an unexpected error. View the console output in debug mode for more information."));
            }

        } else {
            purple_debug_error(SIGNALD_PLUGIN_ID, "Ignored message of unknown type '%s'.\n", type);
        }
    }

    g_object_unref(parser);
}

void
signald_read_cb(gpointer data, gint source, PurpleInputCondition cond)
{
    SignaldAccount *sa = data;
    // this function essentially just reads bytes into a buffer until a newline is reached
    // using getline would be cool, but I do not want to find out what happens if I wrap this fd into a FILE* while the purple handle is connected to it
    const size_t BUFSIZE = 500000; // TODO: research actual maximum message size
    char buf[BUFSIZE];
    char *b = buf;
    gssize read = recv(sa->fd, b, 1, MSG_DONTWAIT);
    while (read > 0) {
        b += read;
        if(b[-1] == '\n') {
            *b = 0;
            purple_debug_info(SIGNALD_PLUGIN_ID, "got newline delimited message: %s", buf);
            signald_handle_input(sa, buf);
            // reset buffer
            *buf = 0;
            b = buf;
        }
        if (b-buf+1 == BUFSIZE) {
            purple_debug_error(SIGNALD_PLUGIN_ID, "message exceeded buffer size: %s\n", buf);
            b = buf;
            // NOTE: incomplete message may be passed to handler during next call
            return;
        }
        read = recv(sa->fd, b, 1, MSG_DONTWAIT);
    }
    if (read == 0) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Connection to signald lost."));
    }
    if (read < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            // assume the message is complete and was probably handled
        } else {
            // TODO: error out?
            purple_debug_error(SIGNALD_PLUGIN_ID, "recv error is %s\n",strerror(errno));
            return;
        }
    }
    if (*buf) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "left in buffer: %s\n", buf);
    }
}

void
signald_signald_start(PurpleAccount *account)
{
    // Controlled by plugin.
    // We need to start signald if it not already running

    purple_debug_info (SIGNALD_PLUGIN_ID, "signald handled by plugin\n");

    if (0 == signald_usages) {
        purple_debug_info (SIGNALD_PLUGIN_ID, "starting signald\n");

        // Start signald daemon as forked process for killing it when closing
        int pid = fork();
        if (pid == 0) {
            // The child, redirect it to signald

            // Save pid for later killing the daemon
            gchar *pid_file = g_strdup_printf(SIGNALD_PID_FILE, purple_user_dir());
            signald_save_pidfile (pid_file);
            g_free(pid_file);

            // Start the daemon
            gchar *data = g_strdup_printf(SIGNALD_DATA_PATH, purple_user_dir());
            const char *socket = purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET_LOCAL);
            execlp("signald", "signald", "-s", socket, "-d", data, (char *) NULL);
            g_free(data);
        }
    }

    signald_usages++;
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald used %d times\n", signald_usages);
}

void
signald_login(PurpleAccount *account)
{
    purple_debug_info(SIGNALD_PLUGIN_ID, "login\n");

    PurpleConnection *pc = purple_account_get_connection(account);

    // this protocol does not support anything special right now
    PurpleConnectionFlags pc_flags;
    pc_flags = purple_connection_get_flags(pc);
    pc_flags |= PURPLE_CONNECTION_NO_FONTSIZE;
    pc_flags |= PURPLE_CONNECTION_NO_BGCOLOR;
    pc_flags |= PURPLE_CONNECTION_ALLOW_CUSTOM_SMILEY;
    purple_connection_set_flags(pc, pc_flags);

    SignaldAccount *sa = g_new0(SignaldAccount, 1);
    purple_connection_set_protocol_data(pc, sa);
    sa->account = account;
    sa->pc = pc;

    // Check account settings whether signald is globally running
    // (controlled by the system or the user) or whether it should
    // be controlled by the plugin.
    if (purple_account_get_bool(sa->account, "handle_signald", FALSE)) {
        signald_signald_start(sa->account);
    }

    purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTING);
    // create a socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_debug_error(SIGNALD_PLUGIN_ID, "socket() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not create to socket."));
        return;
    }

    // connect our socket to signald socket
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET));

    // Try to connect but give signald some time (it was started in background)
    int try = 0;
    int err = -1;
    while ((err != 0) && (try <= SIGNALD_TIME_OUT))
    {
      err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
      try++;
      sleep (1);    // altogether wait SIGNALD_TIME_OUT seconds
    }

    if (err)
    {
      //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
      purple_debug_info(SIGNALD_PLUGIN_ID, "connect() error is %s\n", strerror(errno));
      purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not connect to socket."));
      return;
    }

    sa->fd = fd;
    sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sa);

    // Initialize the container where we'll store our group mappings
    sa->groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    signald_subscribe (sa);
}

static void
signald_close (PurpleConnection *pc)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    // unsubscribe to the configured number
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "unsubscribe");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
      purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    purple_input_remove(sa->watcher);

    sa->watcher = 0;
    close(sa->fd);
    sa->fd = 0;

    g_hash_table_destroy(sa->groups);

    g_free(sa);

    // Kill signald daemon and remove its pid file if this was the last
    // account using the daemon. There is no need to check the option for
    // controlling signald again, since usage count is only greater 0 if
    // controlled by the plugin.
    if (signald_usages) {
        signald_usages--;
        if (0 == signald_usages) {
            // This was the last instance, kill daemon and remove pid file
            gchar *pid_file = g_strdup_printf(SIGNALD_PID_FILE, purple_user_dir());
            signald_kill_process(pid_file);
            g_free(pid_file);
        }
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald used %d times after closing\n", signald_usages);
    }

}

static GList *
signald_status_types(PurpleAccount *account)
{
    GList *types = NULL;
    PurpleStatusType *status;

    status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, SIGNALD_STATUS_STR_ONLINE, _("Online"), TRUE, FALSE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, SIGNALD_STATUS_STR_OFFLINE, _("Offline"), TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_MOBILE, SIGNALD_STATUS_STR_MOBILE, NULL, FALSE, FALSE, TRUE);
    types = g_list_prepend(types, status);

    return types;
}

static int
signald_send_message(SignaldAccount *sa, JsonObject *data, const char *message)
{
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
    g_string_free(msg, TRUE);

    g_free(plain);

    if (!signald_send_json(sa, data)) {
        return -errno;
    } else {
        return 1;
    }
}

static int
signald_send_im(PurpleConnection *pc,
#if PURPLE_VERSION_CHECK(3, 0, 0)
                PurpleMessage *msg)
{
    const gchar *who = purple_message_get_recipient(msg);
    const gchar *message = purple_message_get_contents(msg);
#else
                const gchar *who, const gchar *message, PurpleMessageFlags flags)
{
#endif
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_send_im: flags: %x msg:%s\n", flags, message);

    if (purple_strequal(who, SIGNALD_UNKNOWN_SOURCE_NUMBER)) {
        return 0;
    }

    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "send");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "recipientNumber", who);

    return signald_send_message(sa, data, message);
}

/*************************
 *
 * Chat support functions
 *
 ************************/

void
signald_request_group_list(SignaldAccount *sa)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_groups");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request contacts."));
    }

    json_object_unref(data);
}

GList *
signald_chat_info(PurpleConnection *pc)
{
    GList *infos = NULL;

    struct proto_chat_entry *pce;

    pce = g_new0(struct proto_chat_entry, 1);
    pce->label = _("_Group Name:");
    pce->identifier = "name";
    pce->required = TRUE;
    infos = g_list_append(infos, pce);

    return infos;
}

GHashTable
*signald_chat_info_defaults(PurpleConnection *pc, const char *chat_name)
{
    GHashTable *defaults;

    defaults = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

    if (chat_name != NULL) {
        g_hash_table_insert(defaults, "name", g_strdup(chat_name));
    }

    return defaults;
}

void
signald_join_chat(PurpleConnection *pc, GHashTable *data)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    const char *name = g_hash_table_lookup(data, "name");

    JsonObject *message = json_object_new();

    json_object_set_string_member(message, "type", "update_group");
    json_object_set_string_member(message, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(message, "groupName", name);

    if (! signald_send_json(sa, message)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    // Once the above send completes we'll get a "group_created" event that'll
    // trigger the subsequent actions to make the channel available.

    json_object_unref(message);

    return;
}

void
signald_chat_leave(PurpleConnection *pc, int id)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    gchar *groupId = signald_find_groupid_for_conv_id(sa, id);

    if (groupId == NULL) {
        return;
    }

    JsonObject *message = json_object_new();

    json_object_set_string_member(message, "type", "leave_group");
    json_object_set_string_member(message, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(message, "recipientGroupId", groupId);

    if (! signald_send_json(sa, message)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    json_object_unref(message);

    signald_quit_group(sa, groupId);
}

void
signald_chat_invite(PurpleConnection *pc, int id, const char *message, const char *who)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    gchar *groupId = signald_find_groupid_for_conv_id(sa, id);

    if (groupId == NULL) {
        return;
    }

    JsonArray *members = json_array_new();

    json_array_add_string_element(members, who);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "update_group");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "recipientGroupId", groupId);
    json_object_set_array_member(data, "members", members);

    if (! signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    json_object_unref(data);
}

static int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags)
{
  SignaldAccount *sa = purple_connection_get_protocol_data(pc);
  gchar *groupId = signald_find_groupid_for_conv_id(sa, id);

  if (groupId == NULL) {
      return 0;
  }

  JsonObject *data = json_object_new();

  json_object_set_string_member(data, "type", "send");
  json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
  json_object_set_string_member(data, "recipientGroupId", groupId);

  int ret = signald_send_message(sa, data, message);

  if (ret) {
      purple_serv_got_chat_in(pc, id, sa->account->username, flags, message, time(NULL));
  }

  return ret;
}

/*************************/
/*************************/
/*************************/

static void
signald_add_purple_buddy(SignaldAccount *sa, const char *username, const char *alias)
{
    GSList *buddies;

    buddies = purple_find_buddies(sa->account, username);
    if (buddies) {
        //Already known => do nothing
        //TODO: Update alias
        g_slist_free(buddies);
        return;
    }
    //TODO: Remove old buddies: purple_blist_remove_buddy(b);
    //New buddy
    purple_debug_error(SIGNALD_PLUGIN_ID, "signald_add_purple_buddy(): Adding '%s' with alias '%s'\n", username, alias);

    PurpleGroup *g = purple_find_group(SIGNAL_DEFAULT_GROUP);
    if (!g) {
        g = purple_group_new(SIGNAL_DEFAULT_GROUP);
        purple_blist_add_group(g, NULL);
    }
    PurpleBuddy *b = purple_buddy_new(sa->account, username, alias);

    purple_blist_add_buddy(b, NULL, g, NULL);
    purple_blist_alias_buddy(b, alias);

    signald_assume_buddy_online(sa->account, b);
}

static void
signald_add_buddy(PurpleConnection *pc, PurpleBuddy *buddy, PurpleGroup *group
#if PURPLE_VERSION_CHECK(3, 0, 0)
                  ,
                  const char *message
#endif
                  )
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    signald_assume_buddy_online(sa->account, buddy);
    // does not actually do anything. buddy is added to pidgin's local list and is usable from there.
}

static GList *
signald_add_account_options(GList *account_options)
{
    PurpleAccountOption *option;

    option = purple_account_option_bool_new(
                _("Link to an existing account"),
                "link",
                TRUE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Daemon signald is controlled by pidgin, not globally or by the user"),
                "handle_signald",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                _("Socket of signald daemon when not controlled by pidgin"),
                "socket",
                SIGNALD_DEFAULT_SOCKET
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Display all contacts as online"),
                "fake-online",
                TRUE
                );
    account_options = g_list_append(account_options, option);

    return account_options;
}

static GList *
signald_actions(
#if !PURPLE_VERSION_CHECK(3, 0, 0)
  PurplePlugin *plugin, gpointer context
#else
  PurpleConnection *pc
#endif
  )
{
    GList *m = NULL;
    return m;
}

static gboolean
plugin_load(PurplePlugin *plugin, GError **error)
{
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin, GError **error)
{
    purple_signals_disconnect_by_handle(plugin);
	return TRUE;
}

/* Purple2 Plugin Load Functions */
#if !PURPLE_VERSION_CHECK(3, 0, 0)
static gboolean
libpurple2_plugin_load(PurplePlugin *plugin)
{
	return plugin_load(plugin, NULL);
}

static gboolean
libpurple2_plugin_unload(PurplePlugin *plugin)
{
	return plugin_unload(plugin, NULL);
}

static void
plugin_init(PurplePlugin *plugin)
{
	PurplePluginInfo *info;
	PurplePluginProtocolInfo *prpl_info = g_new0(PurplePluginProtocolInfo, 1);

	info = plugin->info;

	if (info == NULL) {
		plugin->info = info = g_new0(PurplePluginInfo, 1);
	}

	info->extra_info = prpl_info;

    prpl_info->options = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE;
    prpl_info->protocol_options = signald_add_account_options(prpl_info->protocol_options);

    /*
	prpl_info->get_account_text_table = discord_get_account_text_table;
	prpl_info->list_emblem = discord_list_emblem;
	prpl_info->status_text = discord_status_text;
	prpl_info->tooltip_text = discord_tooltip_text;
    */
    prpl_info->list_icon = signald_list_icon;
    /*
	prpl_info->set_status = discord_set_status;
	prpl_info->set_idle = discord_set_idle;
    */
    prpl_info->status_types = signald_status_types; // this actually needs to exist, else the protocol cannot be set to "online"
    prpl_info->chat_info = signald_chat_info;
	prpl_info->chat_info_defaults = signald_chat_info_defaults;
    prpl_info->login = signald_login;
    prpl_info->close = signald_close;
    prpl_info->send_im = signald_send_im;
    /*
	prpl_info->send_typing = discord_send_typing;
    */
    prpl_info->join_chat = signald_join_chat;
    /*
	prpl_info->get_chat_name = discord_get_chat_name;
	prpl_info->find_blist_chat = discord_find_chat;
    */
    prpl_info->chat_invite = signald_chat_invite;
    prpl_info->chat_leave = signald_chat_leave;
    prpl_info->chat_send = signald_send_chat;
    /*
	prpl_info->set_chat_topic = discord_chat_set_topic;
	prpl_info->get_cb_real_name = discord_get_real_name;
    */
    prpl_info->add_buddy = signald_add_buddy;
    /*
	prpl_info->remove_buddy = discord_buddy_remove;
	prpl_info->group_buddy = discord_fake_group_buddy;
	prpl_info->rename_group = discord_fake_group_rename;
	prpl_info->get_info = discord_get_info;
	prpl_info->add_deny = discord_block_user;
	prpl_info->rem_deny = discord_unblock_user;

	prpl_info->roomlist_get_list = discord_roomlist_get_list;
	prpl_info->roomlist_room_serialize = discord_roomlist_serialize;
    */
}

static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	/*	PURPLE_MAJOR_VERSION,
		PURPLE_MINOR_VERSION,
	*/
	2, 1,
	PURPLE_PLUGIN_PROTOCOL,			/* type */
	NULL,							/* ui_requirement */
	0,								/* flags */
	NULL,							/* dependencies */
	PURPLE_PRIORITY_DEFAULT,		/* priority */
	SIGNALD_PLUGIN_ID,				/* id */
    "signald",						/* name */
	SIGNALD_PLUGIN_VERSION,			/* version */
	"",								/* summary */
	"",								/* description */
    "Hermann Hoehne <hoehermann@gmx.de>", /* author */
	SIGNALD_PLUGIN_WEBSITE,			/* homepage */
	libpurple2_plugin_load,			/* load */
	libpurple2_plugin_unload,		/* unload */
	NULL,							/* destroy */
	NULL,							/* ui_info */
	NULL,							/* extra_info */
	NULL,							/* prefs_info */
    signald_actions,				/* actions */
	NULL,							/* padding */
	NULL,
	NULL,
	NULL
};

PURPLE_INIT_PLUGIN(signald, plugin_init, info);

#else
/* Purple 3 plugin load functions */
#error Purple 3 not supported.
#endif
