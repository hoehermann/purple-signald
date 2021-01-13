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
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    json_object_unref(data);
}

void
signald_initialize_contacts(SignaldAccount *sa)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_contacts");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
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

        if (purple_strequal(type, "version")) {
            obj = json_object_get_object_member(obj, "data");
            purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));

        } else if (purple_strequal(type, "success")) {
            // TODO: mark message as delayed (maybe do not echo) until success is reported
            purple_debug_info(SIGNALD_PLUGIN_ID, "Success noticed.\n");

        } else if (purple_strequal(type, "subscribed")) {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Subscribed!\n");
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);

            signald_initialize_contacts(sa);

        } else if (purple_strequal(type, "contact_list")) {
            signald_parse_contact_list(sa, json_object_get_array_member(obj, "data"));

            if (! sa->initialized) {
                signald_request_group_list(sa);
            }

        } else if (purple_strequal(type, "group_list")) {
            obj = json_object_get_object_member(obj, "data");
            signald_parse_group_list(sa, json_object_get_array_member(obj, "groups"));

            if (! sa->initialized) {
                sa->initialized = TRUE;
            }

        } else if (purple_strequal(type, "message")) {
            SignaldMessage msg;

            if (signald_parse_message(sa, json_object_get_object_member(obj, "data"), &msg)) {
                switch(msg.type) {
                    case SIGNALD_MESSAGE_TYPE_DIRECT:
                        signald_process_direct_message(sa, &msg);
                        break;

                    case SIGNALD_MESSAGE_TYPE_GROUP:
                        signald_process_group_message(sa, &msg);
                        break;
                }
            }
        } else if (purple_strequal(type, "linking_uri")) {
            signald_parse_linking_uri(sa, obj);

        } else if (purple_strequal (type, "linking_successful")) {
            signald_parse_linking_successful();

            // FIXME: Sometimes, messages are not received by pidgin after
            //        linking to the main account and are only shown there.
            //        Is it robust to subscribe here?
            signald_subscribe (sa);

        } else if (purple_strequal (type, "linking_error")) {
            signald_parse_linking_error(sa, obj);

            purple_notify_close_with_handle (purple_notify_get_handle ());
            remove (SIGNALD_TMP_QRFILE);

        } else if (signald_strequalprefix(type, "linking_")) {
            gchar *text = g_strdup_printf("Unknown message related to linking:\n%s", type);
            purple_notify_warning (NULL, SIGNALD_DIALOG_TITLE, SIGNALD_DIALOG_LINK, text);
            g_free (text);

        } else if (purple_strequal(type, "group_created")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "group_updated")) {
            // Big hammer, but this should work.
            signald_request_group_list(sa);

        } else if (purple_strequal(type, "account_list")) {
            JsonObject *data = json_object_get_object_member(obj, "data");
            signald_parse_account_list(sa, json_object_get_array_member(data, "accounts"));

        } else if (purple_strequal(type, "unexpected_error")) {
            signald_handle_unexpected_error(sa, obj);

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
            if (purple_debug_is_enabled ()) {
                signald_ok = execlp("signald", "signald", "-v", "-s",
                                    SIGNALD_DEFAULT_SOCKET_LOCAL, "-d",
                                    data, (char *) NULL);
            } else {
                signald_ok = execlp("signald", "signald", "-s",
                                    SIGNALD_DEFAULT_SOCKET_LOCAL, "-d",
                                    data, (char *) NULL);
            }
            g_free(data);

            // Error starting the daemon? (execlp only returns on error)
            purple_debug_info (SIGNALD_PLUGIN_ID,
                               "return code starting signald: %d\n", signald_ok);
        }
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

    if (purple_account_get_bool(sa->account, "handle_signald", FALSE)) {
      // signald is handled by the plugin, use the local socket
      purple_debug_info(SIGNALD_PLUGIN_ID, "using local socket %s\n", SIGNALD_DEFAULT_SOCKET_LOCAL);
      strcpy(address.sun_path, SIGNALD_DEFAULT_SOCKET_LOCAL);
    } else {
      // signald is handled globally
      purple_debug_info(SIGNALD_PLUGIN_ID, "using local socket %s\n", purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET));
      strcpy(address.sun_path, purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET));
    }

    // Try to connect but give signald some time (it was started in background)
    int try = 0;
    int err = -1;
    while ((err != 0) && (try <= SIGNALD_TIME_OUT))
    {
      err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
      purple_debug_info(SIGNALD_PLUGIN_ID, "connecting ... %d s\n", try);
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
    sa->groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    if (! purple_account_get_bool(sa->account, "handle_signald", FALSE)) {
        // subscribe if signald is globally running
        signald_subscribe (sa);
    } else {
        // Otherwise: get information on account for deciding what do do
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "list_accounts");
        if (!signald_send_json(sa, data)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write list account message."));
        }
        json_object_unref(data);
    }
}

static void
signald_close (PurpleConnection *pc)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    purple_signal_disconnect(purple_blist_get_handle(),
                            "blist-node-aliased",
                            purple_connection_get_prpl(pc),
                            G_CALLBACK(signald_node_aliased));

    // unsubscribe to the configured number
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "unsubscribe");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
      purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write message for unsubscribing."));
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

    option = purple_account_option_bool_new(
                _("Automatically join group chats on startup"),
                "auto-join-group-chats",
                FALSE
                );
    account_options = g_list_append(account_options, option);

#ifdef SUPPORT_EXTERNAL_ATTACHMENTS

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

#endif

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
