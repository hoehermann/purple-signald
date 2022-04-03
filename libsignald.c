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

#define _DEFAULT_SOURCE // for gethostname in unistd.h
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h> // for recv
#include <sys/un.h> // for sockaddr_un
#include <gmodule.h>

#include "libsignald.h"

static int signald_usages = 0;

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

int
signald_strequalprefix (const char *s1, const char *s2)
{
    int l1 = strlen (s1);
    int l2 = strlen (s2);

    return 0 == strncmp (s1, s2, l1 < l2 ? l1 : l2);
}

void
signald_subscribe (SignaldAccount *sa)
{
    // subscribe to the configured number
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "subscribe");
    // FIXME: subscribe with uuid as "account" does not work (account not found)
    json_object_set_string_member(data, "account", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    json_object_unref(data);
}

void
signald_request_sync(SignaldAccount *sa)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "request_sync");
    json_object_set_string_member(data, "account", sa->uuid);
    json_object_set_boolean_member(data, "contacts", TRUE);
    json_object_set_boolean_member(data, "groups", TRUE);
    json_object_set_boolean_member(data, "configuration", FALSE);
    json_object_set_boolean_member(data, "blocked", FALSE);

    if (!signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request contact sync."));
    }

    json_object_unref(data);
}

void
signald_list_contacts(SignaldAccount *sa)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_contacts");
    json_object_set_string_member(data, "account", sa->uuid);
    // TODO: v1

    if (!signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request contact list."));
    }

    json_object_unref(data);

    signald_assume_all_buddies_online(sa);
}

void
signald_handle_unexpected_error(SignaldAccount *sa, JsonObject *obj)
{
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
        purple_debug_info(SIGNALD_PLUGIN_ID, "received type: %s\n", type);

        // error handling
        JsonObject *errobj = json_object_get_object_member(obj, "error");
        if (errobj != NULL) {
            purple_debug_error(SIGNALD_PLUGIN_ID, "%s ERROR: %s\n",
                                   type,
                                   json_object_get_string_member(obj, "error_type"));
            return;
        }

        // no error, actions depending on type
        if (purple_strequal(type, "version")) {
            // v1 ok
            obj = json_object_get_object_member(obj, "data");
            purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));

        } else if (purple_strequal(type, "subscribe")) {
            // v1 ok
            purple_debug_info(SIGNALD_PLUGIN_ID, "Subscribed!\n");
            // request a sync; on repsonse, contacts and groups are requested
            signald_request_sync(sa);

        } else if (purple_strequal(type, "request_sync")) {
            signald_list_contacts(sa);

        } else if (purple_strequal(type, "list_contacts")) {
            // TODO: check v1
            signald_parse_contact_list(sa,
                json_object_get_array_member(json_object_get_object_member (obj, "data"),
                "profiles"));

            if (! sa->groups_updated) {
                signald_request_group_list(sa);
            }

        } else if (purple_strequal(type, "get_group")) {
            obj = json_object_get_object_member(obj, "data");
            signald_process_groupV2_obj(sa, obj);

        } else if (purple_strequal(type, "list_groups")) {
            obj = json_object_get_object_member(obj, "data");
            signald_parse_groupV2_list(sa, json_object_get_array_member(obj, "groups"));
            if (! sa->groups_updated) {
                sa->groups_updated = TRUE;
            }

        } else if (purple_strequal(type, "IncomingMessage")) {
            SignaldMessage msg;

            if (signald_parse_message(sa, json_object_get_object_member(obj, "data"), &msg)) {
                switch(msg.type) {
                    case SIGNALD_MESSAGE_TYPE_DIRECT:
                        signald_process_direct_message(sa, &msg);
                        break;
                    case SIGNALD_MESSAGE_TYPE_GROUP:
                        purple_debug_warning(SIGNALD_PLUGIN_ID, "Ignoring obsolete v1 group message");
                        break;
                    case SIGNALD_MESSAGE_TYPE_GROUPV2:
                        signald_process_groupV2_message(sa, &msg);
                        break;
                }
            }
        } else if (purple_strequal(type, "generate_linking_uri")) {
            signald_parse_linking_uri(sa, obj);

        } else if (purple_strequal (type, "finish_link")) {
            signald_parse_linking_successful();
            // FIXME: Sometimes, messages are not received by pidgin after
            //        linking to the main account and are only shown there.
            //        Is it robust to subscribe here?
            signald_subscribe (sa);
            signald_set_device_name(sa);

        } else if (purple_strequal (type, "linking_error")) {
            signald_parse_linking_error(sa, obj);

            purple_notify_close_with_handle (purple_notify_get_handle ());
            remove (SIGNALD_TMP_QRFILE);

        } else if (signald_strequalprefix(type, "linking_")) {
            gchar *text = g_strdup_printf("Unknown message related to linking:\n%s", type);
            purple_notify_warning (NULL, SIGNALD_DIALOG_TITLE, SIGNALD_DIALOG_LINK, text);
            g_free (text);

        } else if (purple_strequal (type, "set_device_name")) {
            purple_debug_info (SIGNALD_PLUGIN_ID, "Device name set\n");

        } else if (purple_strequal(type, "group_created")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "group_updated")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "list_accounts")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            signald_parse_account_list(sa, json_object_get_array_member(data, "accounts"));

        } else if (purple_strequal(type, "unexpected_error")) {
            signald_handle_unexpected_error(sa, obj);

        } else if (purple_strequal(type, "send")) {
            // TODO: keep track of messages, indicate success

        } else if (purple_strequal(type, "WebSocketConnectionState")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            const gchar *state = json_object_get_string_member(data, "state");
            if  (purple_strequal(state, "CONNECTED")) {
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
            int signald_ok;
            const gchar * user_socket = purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET);

            if (purple_debug_is_enabled ()) {
                signald_ok = execlp("signald", "signald", "-v", "-s", user_socket, "-d", data, NULL);
            } else {
                signald_ok = execlp("signald", "signald", "-s", user_socket, "-d", data, NULL);
            }
            g_free(data);

            // Error starting the daemon? (execlp only returns on error)
            purple_debug_info (SIGNALD_PLUGIN_ID, "return code starting signald: %d\n", signald_ok);
            abort(); // Stop child
        }
      sleep (SIGNALD_TIME_OUT/2);     // Wait before trying to connect
    }

    signald_usages++;
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald used %d times\n", signald_usages);
}

void
signald_node_aliased(PurpleBlistNode *node, char *oldname, PurpleConnection *pc)
{
    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
        signald_chat_rename(pc, (PurpleChat *)node);
    }
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

    purple_signal_connect(purple_blist_get_handle(),
                          "blist-node-aliased",
                          purple_connection_get_prpl(pc),
                          G_CALLBACK(signald_node_aliased),
                          pc);

    purple_connection_set_protocol_data(pc, sa);

    sa->account = account;
    sa->pc = pc;

    // Check account settings whether signald is globally running
    // (controlled by the system or the user) or whether it should
    // be controlled by the plugin.
    if (purple_account_get_bool(sa->account, "handle-signald", FALSE)) {
        signald_signald_start(sa->account);
    }

    purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTING);
    // create a socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "socket() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not create to socket."));
        return;
    }

    // connect our socket to signald socket
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;

    gchar *socket_file = NULL;
    gchar *xdg_socket_file;
    gchar *var_socket_file;

    xdg_socket_file = g_strdup_printf ("%s/%s",
                                       g_getenv (SIGNALD_GLOBAL_SOCKET_PATH_XDG),
                                       SIGNALD_GLOBAL_SOCKET_FILE);
    var_socket_file = g_strdup_printf ("%s/%s",
                                       SIGNALD_GLOBAL_SOCKET_PATH_VAR,
                                       SIGNALD_GLOBAL_SOCKET_FILE);

    const gchar * user_socket = purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET);
    if (purple_strequal (user_socket, "")) {
        socket_file = g_strdup_printf ("%s", xdg_socket_file);
        purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", socket_file);
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", user_socket);
        socket_file = g_strdup_printf ("%s", user_socket);
    }
    if (strlen(socket_file)-1 > sizeof address.sun_path) {
      purple_debug_error(
          SIGNALD_PLUGIN_ID, 
          "socket location %s exceeds maximum length %lu!\n", 
          socket_file, 
          sizeof address.sun_path
          );
      return;
    } else {
        strcpy(address.sun_path, socket_file);
    }

    // Try to connect but give signald some time (it was started in background)
    int32_t try = 0;
    int32_t err = -1;

    int32_t connecting = 3; // We have max. three socket locations to test

    while (connecting--) {

        try = 0;
        while ((err != 0) && (try <= SIGNALD_TIME_OUT)) {
            err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
            purple_debug_info(SIGNALD_PLUGIN_ID, "connecting ... %d s\n", try);
            try++;
            sleep (1);    // altogether wait SIGNALD_TIME_OUT seconds
        }

        if (err) {
            if (purple_strequal(socket_file, "")) {
                // socket is handled by pidgin => connection error
                connecting = 0;
            } else {
                if (connecting == 1) {
                    // only one location left, has to be var location
                    socket_file = g_strdup_printf ("%s", var_socket_file);  // var last
                } else if (connecting == 2) {
                    // first attempt to connect was not successful
                    if (purple_strequal (address.sun_path, xdg_socket_file)) {
                        // the first attempt already was with the xdg location 
                        connecting = 1;     // only var location left 
                        socket_file = g_strdup_printf ("%s", var_socket_file);
                    }
                    else if (purple_strequal (address.sun_path, var_socket_file)) {
                        // the first attempt already was with the var location 
                        connecting = 1;     // only xdg location left 
                        socket_file = g_strdup_printf ("%s", xdg_socket_file);
                    } else {
                        // it was another location, test both default locations
                        socket_file = g_strdup_printf ("%s", xdg_socket_file);
                    }
                }
                purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", address.sun_path);
            }

            if ((connecting > 0) &&
                (strlen(socket_file)-1 > sizeof address.sun_path)) {
                purple_debug_error(
                SIGNALD_PLUGIN_ID, 
                "socket location %s exceeds maximum length %lu!\n", 
                socket_file,
                sizeof address.sun_path
                );
                return;
            } else {
                strcpy(address.sun_path, socket_file);
            }
        }
    }

    g_free(socket_file);
    g_free (xdg_socket_file);
    g_free (var_socket_file);

    if (err) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "connect() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not connect to socket."));
        return;
    }

    sa->fd = fd;
    sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sa);

    // Initialize the container where we'll store our group mappings
    sa->groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    // get information on account
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "list_accounts");
    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write list account message."));
    }
    json_object_unref(data);
}

static void
signald_close (PurpleConnection *pc)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    purple_signal_disconnect(purple_blist_get_handle(),
                            "blist-node-aliased",
                            purple_connection_get_prpl(pc),
                            G_CALLBACK(signald_node_aliased));

    // free protocol data memory
    GSList *buddies = purple_find_buddies (sa->account, NULL);
    while (buddies != NULL) {
        PurpleBuddy *buddy = buddies->data;
        gpointer data = purple_buddy_get_protocol_data(buddy);
        if (data) {
            g_free (data);
        }
        // remove current and go to next found buddy (should only be one!)
        buddies = g_slist_delete_link (buddies, buddies);
    }

    // unsubscribe to the configured number
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "unsubscribe");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (purple_connection_get_state(pc) == PURPLE_CONNECTION_CONNECTED && !signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write message for unsubscribing."));
        purple_debug_error(SIGNALD_PLUGIN_ID, _("Could not write message for unsubscribing: %s"), strerror(errno));
    }
    // TODO: wait for signald to acknowlegde unsubscribe before closing the fd

    g_free(sa->uuid);

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

    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, HOST_NAME_MAX)) {
        strcpy(hostname, SIGNALD_DEFAULT_DEVICENAME);
    }

    option = purple_account_option_string_new(
                _("Name of the device for linking"),
                "device-name",
                hostname // strdup happens internally
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Daemon signald is controlled by pidgin, not globally or by the user"),
                "handle-signald",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                _("Socket location (leave blank for default)"),
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

    option = purple_account_option_bool_new(
                _("Automatically join group chats on startup"),
                "auto-join-group-chats",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Receive notifications for all group messages"),
                "group-msg-notifications",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Overwrite custom group icons by group avatar"),
                "use-group-avatar",
                TRUE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                _("Serve attachments from external server"),
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS,
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                _("External attachment storage directory"),
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR,
                ""
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                _("External attachment URL"),
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL,
                ""
                );
    account_options = g_list_append(account_options, option);

    return account_options;
}

static void
signald_update_contacts (PurplePluginAction* action)
{
  PurpleConnection* pc = action->context;
  SignaldAccount *sa = purple_connection_get_protocol_data(pc);

  signald_list_contacts(sa);
}

static void
signald_update_groups (PurplePluginAction* action)
{
  PurpleConnection* pc = action->context;
  SignaldAccount *sa = purple_connection_get_protocol_data(pc);

  signald_request_group_list(sa);
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
    PurplePluginAction *act = purple_plugin_action_new (("Update Contacts ..."), &signald_update_contacts);
    GList* acts = g_list_append(NULL, act);
    act = purple_plugin_action_new (("Update Groups ..."), &signald_update_groups);
    acts = g_list_append(acts, act);

    return acts;
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
    prpl_info->set_chat_topic = signald_set_chat_topic;
    /*
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
