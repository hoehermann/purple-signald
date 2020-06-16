#include "pragma.h"
#include "json_compat.h"
#include "purple_compat.h"
#include "libsignald.h"
#include "message.h"
#include "groups.h"
#include "comms.h"

#pragma GCC diagnostic pop

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

    // We hash the group ID to create a persistent ID for the chat.  It's not
    // stated anywhere that this is required, but it seems to have resolved some
    // weird issues I was having, so...

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

void
signald_process_group_message(SignaldAccount *sa, SignaldMessage *msg)
{
    JsonObject *groupInfo = json_object_get_object_member(msg->data, "groupInfo");

    const gchar *type = json_object_get_string_member(groupInfo, "type");
    const gchar *groupid_str = json_object_get_string_member(groupInfo, "groupId");
    const gchar *groupname = json_object_get_string_member(groupInfo, "name");

    if (purple_strequal(type, "UPDATE")) {
        signald_update_group(sa, groupid_str, groupname, json_object_get_array_member(groupInfo, "members"));

    } else if (purple_strequal(type, "QUIT")) {
        char *username = (char *)purple_account_get_username(sa->account);
        char *quit_source = (char *)json_object_get_string_member(msg->envelope, "source");

        if (purple_strequal(username, quit_source)) {
            signald_quit_group(sa, groupid_str);
        } else {
            signald_update_group(sa, groupid_str, groupname, json_object_get_array_member(groupInfo, "members"));
        }

    } else if (purple_strequal(type, "DELIVER")) {
        PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
        PurpleConvChat *conv = PURPLE_CONV_CHAT(g_hash_table_lookup(sa->groups, groupid_str));
        int id = purple_conv_chat_get_id(conv);

        gboolean has_attachment = FALSE;
        GString *content = NULL;

        if (! signald_format_message(msg, &content, &has_attachment)) {
            return;
        }

        if (has_attachment) {
            flags |= PURPLE_MESSAGE_IMAGES;
        }

        if (msg->is_sync_message) {
            flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;

            purple_conv_chat_write(conv, msg->conversation_name, content->str, flags, msg->timestamp);
        } else {
            purple_serv_got_chat_in(sa->pc, id, msg->conversation_name, flags, content->str, msg->timestamp);
        }

        g_string_free(content, TRUE);
    }
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

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags)
{
  SignaldAccount *sa = purple_connection_get_protocol_data(pc);
  gchar *groupId = signald_find_groupid_for_conv_id(sa, id);

  if (groupId == NULL) {
      return 0;
  }

  int ret = signald_send_message(sa, SIGNALD_MESSAGE_TYPE_GROUP, groupId, message);

  if (ret > 0) {
      // TODO - Need to fix this to use purple_conv_chat_write
      purple_serv_got_chat_in(pc, id, sa->account->username, flags, message, time(NULL));
  }

  return ret;
}

