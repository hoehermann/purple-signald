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
#include <purple.h>
#include "structs.h"
#include "defines.h"
#include "comms.h"
#include "login.h"
#include "message.h"
#include "contacts.h"
#include "groups.h"
#include "options.h"
#include "signald_procmgmt.h"
#include "interface.h"
#include "status.h"
#include "reply.h"

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

static PurplePluginProtocolInfo prpl_info = {
    .struct_size = sizeof(PurplePluginProtocolInfo), // must be set for PURPLE_PROTOCOL_PLUGIN_HAS_FUNC to work across versions
    // base protocol information
    .options = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE,
    .list_icon = signald_list_icon,
    .status_types = signald_status_types, // this actually needs to exist, else the protocol cannot be set to "online"
    .login = signald_login,
    .close = signald_close,
    .send_im = signald_send_im,
    .add_buddy = signald_add_buddy,
    // extra contact information
    .tooltip_text = signald_tooltip_text,
    .get_info = signald_get_info,
    // group-chat related functions
    .chat_info = signald_chat_info,
    .chat_info_defaults = signald_chat_info_defaults,
    .join_chat = signald_join_chat,
    .chat_leave = signald_chat_leave,
    .get_chat_name = signald_get_chat_name,
    .chat_send = signald_send_chat,
    .set_chat_topic = signald_set_chat_topic,
    .roomlist_get_list = signald_roomlist_get_list,
    .blist_node_menu = signald_blist_node_menu,
    #if PURPLE_VERSION_CHECK(2,14,0)
    .get_cb_alias = signald_group_chat_get_participant_alias,
    //.chat_send_file
    #else
    #pragma message "Warning: libpurple is too old. Group chat participants may appear without friendly names."
    #endif
};

static void plugin_init(PurplePlugin *plugin) {
    prpl_info.protocol_options = signald_add_account_options(prpl_info.protocol_options);
}

static PurplePluginInfo info = {
    .magic = PURPLE_PLUGIN_MAGIC,
    .major_version = PURPLE_MAJOR_VERSION,
    .minor_version = PURPLE_MINOR_VERSION,
    .type = PURPLE_PLUGIN_PROTOCOL,
    .priority = PURPLE_PRIORITY_DEFAULT,
    .id = SIGNALD_PLUGIN_ID,
    .name = "signald",
    .version = SIGNALD_PLUGIN_VERSION,
    .author = "Hermann Hoehne <hoehermann@gmx.de>",
    .homepage = SIGNALD_PLUGIN_WEBSITE,
    .load = libpurple2_plugin_load,
    .unload = libpurple2_plugin_unload,
    .extra_info = &prpl_info,
    .actions = signald_actions
};

PURPLE_INIT_PLUGIN(signald, plugin_init, info);
