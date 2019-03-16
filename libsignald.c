/*
 *   signald plugin for libpurple
 *   Copyright (C) 2016 hermann Höhne
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

#ifdef ENABLE_NLS
// TODO: implement localisation
#else
#      define _(a) (a)
#      define N_(a) (a)
#endif

//#include "glib_compat.h"
#include "json_compat.h"
#include "purple_compat.h"

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#ifndef SIGNALD_PLUGIN_VERSION
#error Must set SIGNALD_PLUGIN_VERSION in Makefile
#endif
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"

#define SIGNALD_DEFAULT_SOCKET "/var/run/signald/signald.sock"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;

    int fd;
    guint watcher;
} SignaldAccount;

static const char *
signald_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
    return "signald";
}

void
signald_assume_buddy_online(PurpleAccount *account, PurpleBuddy *buddy)
{
    purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_ONLINE, NULL);
    purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_MOBILE, NULL);
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

void
signald_process_message(SignaldAccount *sa,
        const gchar *username, const gchar *content, const gchar *timestamp_str,
        const gchar *groupid_str, const gchar *groupname)
{
    PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
    time_t timestamp = purple_str_to_time(timestamp_str, FALSE, NULL, NULL, NULL);
    const gchar * sender = groupid_str && *groupid_str ? groupid_str : username;
    purple_serv_got_im(sa->pc, sender, content, flags, timestamp);
}

void
signald_handle_input(SignaldAccount *sa, const char * json)
{
    JsonParser *parser = json_parser_new();
    JsonNode *root;

    if (!json_parser_load_from_data(parser, json, -1, NULL)) {
        purple_debug_error("signald", "Error parsing input.\n");
        return;
    }

    root = json_parser_get_root(parser);

    if (root != NULL) {
        JsonObject *obj = json_node_get_object(root);
        const gchar *type = json_object_get_string_member(obj, "type");
        if (purple_strequal(type, "version")) {
            purple_debug_error("signald", "signald version ignored.\n");
        } else if (purple_strequal(type, "success")) {
            // TODO: mark message as delayed (maybe do not echo) until success is reported
            purple_debug_error("signald", "Success noticed.\n");
        } else if (purple_strequal(type, "subscribed")) {
            purple_debug_error("signald", "Subscribed!\n");
            purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);
            if (purple_account_get_bool(sa->account, "fake-online", TRUE)) {
                signald_assume_all_buddies_online(sa);
            }
        } else if (purple_strequal(type, "message")) {
            obj = json_object_get_object_member(obj, "data");
            gboolean isreceipt = json_object_get_boolean_member(obj, "isReceipt");
            if (isreceipt) {
                // TODO: this could be displayed in the conversation window
                // purple_conv_chat_write(to, username, msg, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
                purple_debug_error("signald", "Received reciept.\n");
            } else {
                const gchar *username = json_object_get_string_member(obj, "source");
                const gchar *timestamp_str = json_object_get_string_member(obj, "timestampISO"); // TODO: create time_t from integer timestamp as timestampISO probably means "time of delivery" instead of the time the message was sent
                // NOTE: time_t is an integer timestamp, but which timezone?
                obj = json_object_get_object_member(obj, "dataMessage");
                const gchar *message = json_object_get_string_member(obj, "message");
                obj = json_object_get_object_member(obj, "groupInfo");
                const gchar *groupid_str = NULL;
                const gchar *groupname = NULL;
                if (obj) {
                    groupid_str = json_object_get_string_member(obj, "groupId");
                    groupname = json_object_get_string_member(obj, "name");
                }
                signald_process_message(sa, username, message, timestamp_str, groupid_str, groupname);
            }
        } else {
            purple_debug_error("signald", "Ignored message of unknown type.\n");
        }
    }

    g_object_unref(parser);
}

void
signald_read_cb(gpointer data, gint source, PurpleInputCondition cond)
{
    SignaldAccount *sa;
    sa = data;
    // this function essentially just reads bytes into a buffer until a newline is reached
    // using getline would be cool, but I do not want to find out what happens if I wrap this fd into a FILE* while the purple handle is connected to it
    gssize read = 1;
    const size_t BUFSIZE = 5000; // TODO: research actual maximum message size
    char buf[BUFSIZE];
    char *b = buf;
    while (read > 0) {
        read = recv(sa->fd, b++, 1, MSG_DONTWAIT);
        if(b[-1] == '\n') {
            *b = 0;
            purple_debug_info("signald", "got newline delimeted message: %s", buf);
            signald_handle_input(sa, buf);
            // reset buffer
            *buf = 0;
            b = buf;
        }
        if (b-buf+1 == BUFSIZE) {
            purple_debug_info("signald", "message exceeded buffer size: %s\n", buf);
            b = buf;
            // NOTE: incomplete message may be passed to handler during next call
            return;
        }
    }
    if (read < 0)
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            // assume the message is complete and was probably handled
        } else {
            // TODO: error out?
            purple_debug_info("signald", "recv error is %s\n",strerror(errno));
            return;
        }
    }
    if (*buf) {
        purple_debug_info("signald", "left in buffer: %s\n", buf);
    }
}

gchar *
append_newline(gchar *str)
{
    int desired_length = strlen(str)+2;
    char *strn = g_malloc0(desired_length);
    strcpy(strn, str);
    strn[desired_length-2] = '\n';
    return strn;
}

void
signald_login(PurpleAccount *account)
{
    PurpleConnection *pc = purple_account_get_connection(account);

    // this protocol does not support anything special right now
    PurpleConnectionFlags pc_flags;
    pc_flags = purple_connection_get_flags(pc);
    pc_flags |= PURPLE_CONNECTION_NO_IMAGES;
    pc_flags |= PURPLE_CONNECTION_NO_FONTSIZE;
    pc_flags |= PURPLE_CONNECTION_NO_NEWLINES;
    pc_flags |= PURPLE_CONNECTION_NO_BGCOLOR;
    purple_connection_set_flags(pc, pc_flags);

    SignaldAccount *sa = g_new0(SignaldAccount, 1);
    purple_connection_set_protocol_data(pc, sa);
    sa->account = account;
    sa->pc = pc;

    purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTING);
    // create a socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_debug_info("signald", "socket() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not create to socket."));
        return;
    }

    // connect our socket to signald socket
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET));
    if (connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0)
    {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_debug_info("signald", "connect() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not connect to socket."));
        return;
    }
    sa->fd = fd;
    sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sa);

    // subscribe to the configured number
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "subscribe");
    json_object_set_string_member(data, "username", purple_account_get_username(account));
    // TODO: create a "write json" helper function
    char *json = json_object_to_string(data);
    char *jsonn = append_newline(json);
    g_free(json);
    int l = strlen(jsonn);
    int w = write(fd, jsonn, l);
    g_free(jsonn);
    if (w != l) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_debug_info("signald", "wrote %d, wanted %d, error is %s\n",w,l,strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscribtion message."));
        return;
    }
}

static void
signald_close(PurpleConnection *pc)
{
    // TODO: find out if this is needed here
    //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    purple_input_remove(sa->watcher);
    sa->watcher = 0;
    close(sa->fd);
    sa->fd = 0;
    g_free(sa);
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
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    // build json
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "send");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, who[0]=='+' ? "recipientNumber" : "recipientGroupId", who);
    json_object_set_string_member(data, "messageBody", message);
    char *json = json_object_to_string(data);
    char *jsonn = append_newline(json);
    g_free(json);
    //purple_debug_info("signald", "Sending:%s", jsonn);
    // send json message
    int l = strlen(jsonn);
    int w = write(sa->fd, jsonn, l);
    g_free(jsonn);
    json_object_unref(data);
    if (w != l) {
        purple_debug_info("signald", "wrote %d, wanted %d, error is %s\n",w,l,strerror(errno));
        return -errno;
    }
    return 1;
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
    if (purple_account_get_bool(sa->account, "fake-online", TRUE)) {
        signald_assume_buddy_online(sa->account, buddy);
    }
    // does not actually do anything. buddy is added to pidgin's local list and is usable from there.
}

static GList *
signald_add_account_options(GList *account_options)
{
    PurpleAccountOption *option;

    option = purple_account_option_string_new(
                _("socket"),
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
#if PURPLE_MINOR_VERSION >= 5
    //
#endif
#if PURPLE_MINOR_VERSION >= 8
    //
#endif

    prpl_info->options = OPT_PROTO_NO_PASSWORD;
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
    /*
	prpl_info->chat_info = discord_chat_info;
	prpl_info->chat_info_defaults = discord_chat_info_defaults;
    */
    prpl_info->login = signald_login;
    prpl_info->close = signald_close;
    prpl_info->send_im = signald_send_im;
    /*
	prpl_info->send_typing = discord_send_typing;
	prpl_info->join_chat = discord_join_chat;
	prpl_info->get_chat_name = discord_get_chat_name;
	prpl_info->find_blist_chat = discord_find_chat;
	prpl_info->chat_invite = discord_chat_invite;
	prpl_info->chat_send = discord_chat_send;
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
