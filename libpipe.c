/*
 *   Pipe plugin for libpurple
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
//
#else
#      define _(a) (a)
#      define N_(a) (a)
#endif

#include "glib_compat.h"
#include "purple_compat.h"

#define PIPE_PLUGIN_ID "prpl-hehoe-pipe"
#ifndef PIPE_PLUGIN_VERSION
#define PIPE_PLUGIN_VERSION "0.1"
#endif
#define PIPE_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-pipe"

#define SIGNALD_DEFAULT_SOCKET "/var/run/signald/signald.sock"

static GRegex *some_regex = NULL;

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;

    int fd;
    guint watcher;
} PipeAccount;

/** libpurple requires unique chat id's per conversation.
	we use a hash function to convert the 64bit conversation id
	into a platform-dependent chat id (worst case 32bit).
	previously we used g_int64_hash() from glib, 
	however libpurple requires positive integers */

static const char *
pipe_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
    return "pipe";
}

void
pipe_handle_input(const char * json)
{
    /*
     * 	JsonParser *parser = json_parser_new();
    JsonNode *root;
    gint64 opcode;

    purple_debug_info("discord", "got frame data: %s\n", frame);
    */
}

void
pipe_read_cb(gpointer data, gint source, PurpleInputCondition cond)
{
    PipeAccount *da;
    da = data;
    gssize read = 1;
    const size_t BUFSIZE = 5000;
    char buf[BUFSIZE];
    char *b = buf;
    while (read > 0) {
        read = recv(da->fd, b++, 1, MSG_DONTWAIT);
        if(b[-1] == '\n') {
            *b = 0;
            purple_debug_info("pipe", "got newline delimeted message: %s", buf);
            pipe_handle_input(buf);
            *buf = 0;
        }
        if (b-buf+1 == BUFSIZE) {
            purple_debug_info("pipe", "message exceeded buffer size: %s\n", buf);
            b = buf;
            // TODO: error out
        }
    }
    if (read < 0)
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            // message could be complete
        } else {
            //peer_connection_destroy(conn, OSCAR_DISCONNECT_LOST_CONNECTION, g_strerror(errno));
            purple_debug_info("pipe", "recv error is %s\n",strerror(errno));
            return;
        }
    }
    if (*buf) {
        purple_debug_info("pipe", "left in buffer: %s\n", buf);
    }
}

void
pipe_login(PurpleAccount *account)
{
    PurpleConnection *pc = purple_account_get_connection(account);
    PurpleConnectionFlags pc_flags;

    pc_flags = purple_connection_get_flags(pc);
    pc_flags |= PURPLE_CONNECTION_NO_IMAGES;
    pc_flags |= PURPLE_CONNECTION_NO_FONTSIZE;
    pc_flags |= PURPLE_CONNECTION_NO_NEWLINES;
    pc_flags |= PURPLE_CONNECTION_NO_BGCOLOR;
    purple_connection_set_flags(pc, pc_flags);

    PipeAccount *da = g_new0(PipeAccount, 1);
    purple_connection_set_protocol_data(pc, da);
    da->account = account;
    da->pc = pc;

    purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTING);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        purple_debug_info("pipe", "socket() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not create to socket."));
        return;
    }

    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET));
    if (connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0)
    {
        purple_debug_info("pipe", "connect() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not connect to socket."));
        return;
    }
    da->fd = fd;
    da->watcher = purple_input_add(fd, PURPLE_INPUT_READ, pipe_read_cb, da);

    char subscribe_msg[128];
    sprintf(subscribe_msg, "{\"type\": \"subscribe\", \"username\": \"%s\"}", purple_account_get_username(account));
    int l = strlen(subscribe_msg);
    int w = write(fd, subscribe_msg, l);
    if (w != l) {
        purple_debug_info("pipe", "wrote %d, wanted %d, error is %s\n",w,l,strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could write subscribtion message."));
        return;
    }

    purple_connection_set_state(da->pc, PURPLE_CONNECTION_CONNECTED);
}

static void
pipe_close(PurpleConnection *pc)
{
    PipeAccount *da = purple_connection_get_protocol_data(pc);
    close(da->fd);
    g_free(da);
}

static GList *
pipe_status_types(PurpleAccount *account)
{
    GList *types = NULL;
    PurpleStatusType *status;

    status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, "set-online", _("Online"), TRUE, FALSE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, "set-offline", _("Offline"), TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    return types;
}

static int
pipe_send_im(PurpleConnection *pc,
#if PURPLE_VERSION_CHECK(3, 0, 0)
                PurpleMessage *msg)
{
    const gchar *who = purple_message_get_recipient(msg);
    const gchar *message = purple_message_get_contents(msg);
#else
                const gchar *who, const gchar *message, PurpleMessageFlags flags)
{
#endif
    //DiscordAccount *da = purple_connection_get_protocol_data(pc);
    //gchar *room_id = g_hash_table_lookup(da->one_to_ones_rev, who);
    // send_message(da, to_int(room_id), message);
    return 1;
}

static GList *
pipe_add_account_options(GList *account_options)
{
    PurpleAccountOption *option;

    option = purple_account_option_string_new(
                _("Command to execute as sub-process"),
                "command",
                ""
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                _("socket"),
                "socket",
                SIGNALD_DEFAULT_SOCKET
                );
    account_options = g_list_append(account_options, option);

    return account_options;
}

static GList *
pipe_actions(
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
    some_regex = g_regex_new("&lt;#(\\d+)&gt;", G_REGEX_OPTIMIZE, 0, NULL);
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin, GError **error)
{
	purple_signals_disconnect_by_handle(plugin);
    g_regex_unref(some_regex);
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
    prpl_info->protocol_options = pipe_add_account_options(prpl_info->protocol_options);

    /*
	prpl_info->get_account_text_table = discord_get_account_text_table;
	prpl_info->list_emblem = discord_list_emblem;
	prpl_info->status_text = discord_status_text;
	prpl_info->tooltip_text = discord_tooltip_text;
    */
    prpl_info->list_icon = pipe_list_icon;
    /*
	prpl_info->set_status = discord_set_status;
	prpl_info->set_idle = discord_set_idle;
    */
    prpl_info->status_types = pipe_status_types;
    /*
	prpl_info->chat_info = discord_chat_info;
	prpl_info->chat_info_defaults = discord_chat_info_defaults;
    */
    prpl_info->login = pipe_login;
    prpl_info->close = pipe_close;
    prpl_info->send_im = pipe_send_im;
    /*
	prpl_info->send_typing = discord_send_typing;
	prpl_info->join_chat = discord_join_chat;
	prpl_info->get_chat_name = discord_get_chat_name;
	prpl_info->find_blist_chat = discord_find_chat;
	prpl_info->chat_invite = discord_chat_invite;
	prpl_info->chat_send = discord_chat_send;
	prpl_info->set_chat_topic = discord_chat_set_topic;
	prpl_info->get_cb_real_name = discord_get_real_name;
	prpl_info->add_buddy = discord_add_buddy;
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
	PIPE_PLUGIN_ID,				/* id */
    "Pipe",						/* name */
	PIPE_PLUGIN_VERSION,			/* version */
	"",								/* summary */
	"",								/* description */
    "Hermann Hoehne <hoehermann@gmx.de>", /* author */
	PIPE_PLUGIN_WEBSITE,			/* homepage */
	libpurple2_plugin_load,			/* load */
	libpurple2_plugin_unload,		/* unload */
	NULL,							/* destroy */
	NULL,							/* ui_info */
	NULL,							/* extra_info */
	NULL,							/* prefs_info */
    pipe_actions,				/* actions */
	NULL,							/* padding */
	NULL,
	NULL,
	NULL
};

PURPLE_INIT_PLUGIN(pipe, plugin_init, info);

#else
/* Purple 3 plugin load functions */
#perror This was never tested.
#endif
