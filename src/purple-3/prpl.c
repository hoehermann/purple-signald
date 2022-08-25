//#include <stdarg.h>
//#include <string.h>
//#include <time.h>

//#include <glib.h>

#include <gplugin.h>
#include <gplugin-native.h>

#include <purple.h>

#include "prpl.h"

#include "../login.h"

struct _SignaldProtocol {
  PurpleProtocol parent;
};

#define SIGNALD_STATUS_ONLINE   "online"
#define SIGNALD_STATUS_AWAY     "away"
#define SIGNALD_STATUS_OFFLINE  "offline"

/*
 * Protocol functions
 */
static const gchar * signald_protocol_actions_get_prefix(PurpleProtocolActions *actions) {
  return "prpl-signald";
}

static GActionGroup * signald_protocol_actions_get_action_group(PurpleProtocolActions *actions, G_GNUC_UNUSED PurpleConnection *connection) {
  GSimpleActionGroup *group = NULL;
  return G_ACTION_GROUP(group);
}

static GMenu * signald_protocol_actions_get_menu(PurpleProtocolActions *actions) {
  GMenu *menu = g_menu_new();
  return menu;
}

static const char *signald_list_icon(PurpleAccount *acct, PurpleBuddy *buddy) {
  return "signal";
}

static GList * signald_protocol_get_account_options(PurpleProtocol *protocol) {
  PurpleAccountOption *option;
  option = purple_account_option_string_new(
          "Example option", /* text shown to user */
          "example",        /* pref name */
          "default");       /* default value */
  return g_list_append(NULL, option);
}

static void signald_tooltip_text(PurpleProtocolClient *client, PurpleBuddy *buddy, PurpleNotifyUserInfo *info, gboolean full) {
    purple_notify_user_info_add_pair_plaintext(info, "User info", "TODO");
}

static GList *signald_status_types(PurpleAccount *acct) {
  GList *types = NULL;
  return NULL;
}

static void blist_example_menu_item(PurpleBlistNode *node, gpointer userdata) {
}

static GList * signald_blist_node_menu(PurpleProtocolClient *client, PurpleBlistNode *node) {
  if (PURPLE_IS_GROUP(node)) {
    PurpleActionMenu *action = purple_action_menu_new(
      "Leave group",
      G_CALLBACK(blist_example_menu_item),
      NULL, // userdata passed to the callback
      NULL  // child menu items
    );
    return g_list_append(NULL, action);
  } else {
    return NULL;
  }
}

static GList * signald_chat_info(PurpleProtocolChat *protocol_chat, PurpleConnection *gc) {
  PurpleProtocolChatEntry *pce = g_new0(PurpleProtocolChatEntry, 1);
  pce->label = "Group ID";
  pce->identifier = "name";
  pce->required = TRUE;
  return g_list_append(NULL, pce);
}

static gchar * signald_get_chat_name(PurpleProtocolChat *protocol_chat, GHashTable *components) {
  const char *room = g_hash_table_lookup(components, "name");
  return g_strdup(room);
}

static GHashTable * signald_chat_info_defaults(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, const gchar *room) {
  GHashTable *defaults = NULL;
  return defaults;
}

static void signald_close(PurpleConnection *gc) {
}

static int signald_send_im(PurpleProtocolIM *im, PurpleConnection *gc, PurpleMessage *msg) {
   return 1;
}

static void notify_typing(PurpleConnection *from, PurpleConnection *to, gpointer typing) {
  //purple_serv_got_typing(to, from_username, 0, (PurpleIMTypingState)typing);
}

static unsigned int signald_send_typing(PurpleProtocolIM *im, PurpleConnection *gc, const char *name, PurpleIMTypingState typing) {
  return 0;
}

static void signald_get_info(PurpleProtocolServer *protocol_server, PurpleConnection *gc, const gchar *username) {
  PurpleNotifyUserInfo *info = purple_notify_user_info_new();
  /* show a buddy's user info in a nice dialog box */
  purple_notify_userinfo(
    gc,        /* connection the buddy info came through */
    username,  /* buddy's username */
    info,      /* body */
    NULL,      /* callback called when dialog closed */
    NULL       /* userdata for callback */
  );
}

static void signald_set_status(PurpleProtocolServer *protocol_server, PurpleAccount *acct, PurpleStatus *status) {
}

static void signald_add_buddy(PurpleProtocolServer *protocol_server, PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group, const gchar *message) {
}

static void signald_join_chat(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, GHashTable *components) {
}

static void signald_reject_chat(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, GHashTable *components) {
}

static void signald_chat_invite(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, gint id, const gchar *message, const gchar *who) {
}

static void signald_chat_leave(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, gint id) {
}

static void receive_chat_message(PurpleConversation *from, PurpleConversation *to, int id, const char *room, gpointer userdata) {
  //purple_serv_got_chat_in(to_gc, id, who, PURPLE_MESSAGE_RECV, message, time(NULL));
}

static gint signald_chat_send(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, gint id, PurpleMessage *msg) {
  return 0;
}

static void signald_register_user(PurpleProtocolServer *protocol_server, PurpleAccount *acct) {
}

static void signald_alias_buddy(PurpleProtocolServer *protocol_server, PurpleConnection *gc, const gchar *who, const gchar *alias) {
}

static void signald_remove_group(PurpleProtocolServer *protocol_server, PurpleConnection *gc, PurpleGroup *group) {
}

static void signald_set_chat_topic(PurpleProtocolChat *protocol_chat, PurpleConnection *gc, gint id, const gchar *topic) {
  return;
}

static gboolean signald_finish_get_roomlist(gpointer roomlist) {
  purple_roomlist_set_in_progress(PURPLE_ROOMLIST(roomlist), FALSE);
  g_object_unref(roomlist);
  return FALSE;
}

static PurpleRoomlist * signald_roomlist_get_list(PurpleProtocolRoomlist *protocol_roomlist, PurpleConnection *gc) {
  PurpleRoomlist *roomlist = purple_roomlist_new(purple_connection_get_account(gc));
  return roomlist;
}

/*
 * Initialize the protocol instance. see protocol.h for more information.
 */
static void signald_protocol_init(SignaldProtocol *self) {
}

/*
 * Initialize the protocol class and interfaces.
 * see protocol.h for more information.
 */

static void signald_protocol_class_init(SignaldProtocolClass *klass) {
  PurpleProtocolClass *protocol_class = PURPLE_PROTOCOL_CLASS(klass);

  protocol_class->login = signald_login;
  protocol_class->close = signald_close;
  protocol_class->status_types = signald_status_types;
  protocol_class->list_icon = signald_list_icon;

  protocol_class->get_account_options = signald_protocol_get_account_options;
  //protocol_class->get_user_splits = signald_protocol_get_user_splits;
}

static void signald_protocol_class_finalize(G_GNUC_UNUSED SignaldProtocolClass *klass) {
}

static void signald_protocol_actions_iface_init(PurpleProtocolActionsInterface *iface) {
  iface->get_prefix = signald_protocol_actions_get_prefix;
  iface->get_action_group = signald_protocol_actions_get_action_group;
  iface->get_menu = signald_protocol_actions_get_menu;
}

static void signald_protocol_client_iface_init(PurpleProtocolClientInterface *client_iface) {
  //client_iface->status_text     = signald_status_text;
  client_iface->tooltip_text    = signald_tooltip_text;
  client_iface->blist_node_menu = signald_blist_node_menu;
}

static void signald_protocol_server_iface_init(PurpleProtocolServerInterface *server_iface) {
  server_iface->register_user  = signald_register_user;
  server_iface->get_info       = signald_get_info;
  server_iface->set_status     = signald_set_status;
  server_iface->add_buddy      = signald_add_buddy;
  server_iface->alias_buddy    = signald_alias_buddy;
  server_iface->remove_group   = signald_remove_group;
}

static void signald_protocol_im_iface_init(PurpleProtocolIMInterface *im_iface) {
  im_iface->send        = signald_send_im;
  im_iface->send_typing = signald_send_typing;
}

static void signald_protocol_chat_iface_init(PurpleProtocolChatInterface *chat_iface) {
  chat_iface->info          = signald_chat_info;
  chat_iface->info_defaults = signald_chat_info_defaults;
  chat_iface->join          = signald_join_chat;
  chat_iface->reject        = signald_reject_chat;
  chat_iface->get_name      = signald_get_chat_name;
  chat_iface->invite        = signald_chat_invite;
  chat_iface->leave         = signald_chat_leave;
  chat_iface->send          = signald_chat_send;
  chat_iface->set_topic     = signald_set_chat_topic;
}

static void signald_protocol_privacy_iface_init(PurpleProtocolPrivacyInterface *privacy_iface) {
}

static void
signald_protocol_roomlist_iface_init(PurpleProtocolRoomlistInterface *roomlist_iface) {
  roomlist_iface->get_list        = signald_roomlist_get_list;
}

/*
 * define the signald protocol type. this macro defines
 * signald_protocol_register_type(PurplePlugin *) which is called in plugin_load()
 * to register this type with the type system, and signald_protocol_get_type()
 * which returns the registered GType.
 */
G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    SignaldProtocol, signald_protocol, PURPLE_TYPE_PROTOCOL, 0,
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_ACTIONS, signald_protocol_actions_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_CLIENT, signald_protocol_client_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_SERVER, signald_protocol_server_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_IM, signald_protocol_im_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_CHAT, signald_protocol_chat_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_PRIVACY, signald_protocol_privacy_iface_init)
        G_IMPLEMENT_INTERFACE_DYNAMIC(PURPLE_TYPE_PROTOCOL_ROOMLIST, signald_protocol_roomlist_iface_init)
    );

static PurpleProtocol * signald_protocol_new(void) {
  return PURPLE_PROTOCOL(g_object_new(
    SIGNALD_TYPE_PROTOCOL,
    "id", "prpl-signald",
    "name", "signald",
    "options", OPT_PROTO_NO_PASSWORD | OPT_PROTO_CHAT_TOPIC,
    NULL));
}

static GPluginPluginInfo * signald_query(GError **error) {
  const gchar *authors[] = {
    "Hermann Höhne <hoehermann@gmx.de>",
    NULL
  };
  return purple_plugin_info_new(
    "id",           "prpl-signald",
    "name",         "Signald Signal Protocol",
    "authors", authors,
    "version",      SIGNALD_PLUGIN_VERSION,
    "category",     "Protocol",
    "summary",      "Protocol plug-in for connecting to Signal via signald",
    //"description",  "Signald Plugin description",
    "website",      "https://github.com/hoehermann/purple-signald",
    "abi-version",  PURPLE_ABI_VERSION,

    /* This third-party plug-in should not use this flags,
     * but without them the plug-in will not be loaded in time.
     */
    "flags", PURPLE_PLUGIN_INFO_FLAGS_AUTO_LOAD,
    NULL
  );
}

/*
 * reference to the protocol instance, used for registering signals, prefs,
 * etc. it is set when the protocol is added in plugin_load and is required
 * for removing the protocol in plugin_unload.
 */
static PurpleProtocol *signald_protocol = NULL;

static gboolean signald_load(GPluginPlugin *plugin, GError **error) {
  PurpleProtocolManager *manager = purple_protocol_manager_get_default();
  /* register the SIGNALD_TYPE_PROTOCOL type in the type system. this function
   * is defined by G_DEFINE_DYNAMIC_TYPE_EXTENDED. */
  signald_protocol_register_type(G_TYPE_MODULE(plugin));
  /* add the protocol to the core */
  signald_protocol = signald_protocol_new();
  if(!purple_protocol_manager_register(manager, signald_protocol, error)) {
    g_clear_object(&signald_protocol);
    return FALSE;
  }
  return TRUE;
}

static gboolean signald_unload(GPluginPlugin *plugin, gboolean shutdown, GError **error) {
  PurpleProtocolManager *manager = purple_protocol_manager_get_default();
  /* remove the protocol from the core */
  if(!purple_protocol_manager_unregister(manager, signald_protocol, error)) {
    return FALSE;
  }
  g_clear_object(&signald_protocol);
  return TRUE;
}

GPLUGIN_NATIVE_PLUGIN_DECLARE(signald);
