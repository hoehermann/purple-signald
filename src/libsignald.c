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
#include <gmodule.h>
#include "purple_compat.h"
#include "structs.h"
#include "defines.h"
#include "comms.h"
#include "login.h"
#include "message.h"
#include "contacts.h"
#include "groups.h"
#include "options.h"
#include "signald_procmgmt.h"
#include "blist.h"
#include "reply.h"

static const char *
signald_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
    return "signal";
}

static void
signald_close (PurpleConnection *pc)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    // remove input watcher
    purple_input_remove(sa->watcher);
    sa->watcher = 0;

    if (sa->uuid) {
        // own UUID is kown, unsubscribe account
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "unsubscribe");
        json_object_set_string_member(data, "account", sa->uuid);
        if (purple_connection_get_state(pc) == PURPLE_CONNECTION_CONNECTED) { 
            if (signald_send_json(sa, data)) {
                // read one last time for acknowledgement of unsubscription
                // NOTE: this will block forever in case signald stalls
                sa->readflags = 0;
                signald_read_cb(sa, 0, 0);
            } else {
                purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Could not write message for unsubscribing.");
                purple_debug_error(SIGNALD_PLUGIN_ID, "Could not write message for unsubscribing: %s", strerror(errno));
            }
        }
        json_object_unref(data);
        // now free UUID
        g_free(sa->uuid);
        sa->uuid = NULL;
    }

    close(sa->fd);
    sa->fd = 0;
    
    signald_replycache_free(sa->replycache);
    
    signald_receipts_destroy(sa);

    g_free(sa);

    signald_connection_closed();
}

static GList *
signald_status_types(PurpleAccount *account)
{
    GList *types = NULL;
    PurpleStatusType *status;

    status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, SIGNALD_STATUS_STR_ONLINE, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_AWAY, SIGNALD_STATUS_STR_AWAY, NULL, FALSE, FALSE, FALSE);
    types = g_list_prepend(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, SIGNALD_STATUS_STR_OFFLINE, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_MOBILE, SIGNALD_STATUS_STR_MOBILE, NULL, FALSE, FALSE, TRUE);
    types = g_list_prepend(types, status);

    return types;
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

static void
signald_tooltip_text(PurpleBuddy *buddy, PurpleNotifyUserInfo *user_info, gboolean full)
{
    char * number = purple_buddy_get_protocol_data(buddy);
    if (number && number[0]) {
        purple_notify_user_info_add_pair_plaintext(user_info, "Number", number);
    }
}

static GList *
signald_actions(PurplePlugin *plugin, gpointer context)
{
    GList* acts = NULL;
    {
        PurplePluginAction *act = purple_plugin_action_new("Update Contacts", &signald_update_contacts);
        acts = g_list_append(acts, act);
    }
    {
        PurplePluginAction *act = purple_plugin_action_new("Update Groups", &signald_update_groups);
        acts = g_list_append(acts, act);
    }
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
    PurplePluginProtocolInfo *prpl_info = g_new0(PurplePluginProtocolInfo, 1);
    PurplePluginInfo *info = plugin->info;
    if (info == NULL) {
        plugin->info = info = g_new0(PurplePluginInfo, 1);
    }
    info->extra_info = prpl_info;
    // base protocol information
    prpl_info->options = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE;
    prpl_info->protocol_options = signald_add_account_options(prpl_info->protocol_options);
    prpl_info->list_icon = signald_list_icon;
    prpl_info->status_types = signald_status_types; // this actually needs to exist, else the protocol cannot be set to "online"
    prpl_info->login = signald_login;
    prpl_info->close = signald_close;
    prpl_info->send_im = signald_send_im;
    prpl_info->add_buddy = signald_add_buddy;
    // extra contact information
    prpl_info->tooltip_text = signald_tooltip_text;
    prpl_info->get_info = signald_get_info;
    // group-chat related functions
    prpl_info->chat_info = signald_chat_info;
    prpl_info->chat_info_defaults = signald_chat_info_defaults;
    prpl_info->join_chat = signald_join_chat;
    prpl_info->chat_leave = signald_chat_leave;
    prpl_info->get_chat_name = signald_get_chat_name;
    prpl_info->chat_send = signald_send_chat;
    prpl_info->set_chat_topic = signald_set_chat_topic;
    prpl_info->roomlist_get_list = signald_roomlist_get_list;
    prpl_info->blist_node_menu = signald_blist_node_menu;
    #if PURPLE_VERSION_CHECK(2,14,0)
    //prpl_info->get_cb_alias
    //prpl_info->chat_send_file
    #endif
}

static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_PROTOCOL,            /* type */
    NULL,                            /* ui_requirement */
    0,                                /* flags */
    NULL,                            /* dependencies */
    PURPLE_PRIORITY_DEFAULT,        /* priority */
    SIGNALD_PLUGIN_ID,                /* id */
    "signald",                        /* name */
    SIGNALD_PLUGIN_VERSION,            /* version */
    "",                                /* summary */
    "",                                /* description */
    "Hermann Hoehne <hoehermann@gmx.de>", /* author */
    SIGNALD_PLUGIN_WEBSITE,            /* homepage */
    libpurple2_plugin_load,            /* load */
    libpurple2_plugin_unload,        /* unload */
    NULL,                            /* destroy */
    NULL,                            /* ui_info */
    NULL,                            /* extra_info */
    NULL,                            /* prefs_info */
    signald_actions,                /* actions */
    NULL,                            /* padding */
    NULL,
    NULL,
    NULL
};

PURPLE_INIT_PLUGIN(signald, plugin_init, info);
