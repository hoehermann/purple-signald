#include "libsignald.h"

PurpleChat *
signald_blist_find_chat(SignaldAccount *sa, const char *groupId)
{
    PurpleBuddyList *bl = purple_get_blist();
    PurpleBlistNode *group;

    for (group = bl->root; group != NULL; group = group->next) {
        PurpleBlistNode *node;

        for (node = group->child; node != NULL; node = node->next) {
            if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
                PurpleChat *chat = (PurpleChat *)node;

                if (sa->account != chat->account) {
                    continue;
                }

                char *gid = g_hash_table_lookup(chat->components, "groupId");

                if (purple_strequal(groupId, gid)) {
                    return chat;
                }
            }
        }
    }

    return NULL;
}

/**
 ** Utility functions for finding groups based on name or Pidgin conversation IDs.
 **/
gchar *
signald_find_groupid_for_conv_id(SignaldAccount *sa, int id)
{
    PurpleConversation *conv = purple_find_chat(sa->pc, id);

    if (conv == NULL) {
        return NULL;
    } else {
        return (gchar *)purple_conversation_get_data(conv, SIGNALD_CONV_GROUPID_KEY);
    }
}

gchar *
signald_find_groupid_for_conv_name(SignaldAccount *sa, gchar *name)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iter, sa->groups);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SignaldGroup *group = (SignaldGroup *)value;

        if (purple_strequal(group->name, name)) {
            return (gchar *)key;
        }
    }

    return NULL;
}

/**
 ** Utility functions for dealing with group member entries.
 **/

/*
 * Given a JsonNode for a group member, get the number.
 */
char *
signald_get_group_member_number(SignaldAccount *sa, JsonNode *node)
{
    JsonObject *member = json_node_get_object(node);
    return (char *)json_object_get_string_member(member, "number");
}


int signald_find_uuid_user(gconstpointer buddy, gconstpointer uuid)
{
    const char *data = (char *)purple_buddy_get_protocol_data ((PurpleBuddy *)buddy);
    return g_strcmp0(data, (char *)uuid);
}

/*
 * Given a JsonNode for a group member, get the uuid.
 */
char *
signald_get_group_member_uuid(SignaldAccount *sa, JsonNode *node)
{
    JsonObject *member = json_node_get_object(node);
    return (char *)json_object_get_string_member(member, "uuid");
}

/*
 * Given a list of members, get back the list of corresponding numbers.
 * This function makes a copy of the number so it must be freed.
 */
GList *
signald_members_to_numbers(SignaldAccount *sa, JsonArray *members)
{
    GList *numbers = NULL;

    for (
        GList *this_member = json_array_get_elements(members);
        this_member != NULL;
        this_member = this_member->next
    ) {
        JsonNode *element = (JsonNode *)(this_member->data);
        char *number = signald_get_group_member_number(sa, element);
        if (!number) {
            // number is null, if only uuid is known
            // TODO: clean this up so signald_members_to_numbers actually returns numbers only. or rename the function, align with _uuid variants
            number = signald_get_group_member_uuid(sa, element);
        }
        numbers = g_list_append(numbers, g_strdup(number));
    }
    return numbers;
}

/*
 * Given a list of members, get back the list of corresponding uuid.
 * This function makes a copy of the number so it must be freed.
 */
GList *
signald_members_to_uuids(SignaldAccount *sa, JsonArray *members)
{
    GList *numbers = NULL;

    for (
        GList *this_member = json_array_get_elements(members);
        this_member != NULL;
        this_member = this_member->next
    ) {
        JsonNode *element = (JsonNode *)(this_member->data);
        char *number = signald_get_group_member_uuid(sa, element);
        numbers = g_list_append(numbers, g_strdup(number));
    }
    return numbers;
}

/*
 * Check if the member list contains the given number.  This is unnecessarily
 * expensive as it gets the numbers for all entries in order to perform the
 * comparison, but it's also blissfully short, so... tradeoffs.
 */
gboolean
signald_members_contains_number(SignaldAccount *sa, JsonArray *members, char *number)
{
    GList *numbers = signald_members_to_numbers(sa, members);
    gboolean result = g_list_find_custom(numbers, number, (GCompareFunc)g_strcmp0) != NULL;
    g_list_free_full(numbers, g_free);
    return result;
}

/*
 * Variation of signald_members_contains_number, but for uuids in V2 groups.
 */
gboolean
signald_members_contains_uuid(SignaldAccount *sa, JsonArray *members, char *uuid)
{
    GList *uuids = signald_members_to_uuids(sa, members);
    gboolean result = g_list_find_custom(uuids, uuid, (GCompareFunc)g_strcmp0) != NULL;
    g_list_free_full(uuids, g_free);
    return result;
}

/**
 ** Functions to manipulate our groups.
 **/

void
signald_update_group_avatar (SignaldAccount *sa, SignaldGroup *group, const char *avatar)
{
    if (avatar != NULL && group->chat != NULL) {
        if (! purple_buddy_icons_node_has_custom_icon ((PurpleBlistNode*)group->chat)
            || purple_account_get_bool(sa->account, "use-group-avatar", TRUE)) {
            purple_buddy_icons_node_set_custom_icon_from_file ((PurpleBlistNode*)group->chat, avatar);
            purple_blist_update_node_icon ((PurpleBlistNode*)group->chat);
        }
    }
}

/*
 * Replace the group member list with the one specified.
 *
 * Optionally performs a diff between the current list and the provided list
 * and gives back the set of added and removed users if desired.
 *
 * Note, the values in the added and removed lists are copies and must be
 * freed.
 */
void
signald_update_group_user_list(SignaldAccount *sa, SignaldGroup *group, JsonArray *members, GList **added, GList **removed)
{
    GList *numbers = signald_members_to_numbers(sa, members);

    if (removed != NULL) {
        *removed = NULL;

        // Go through our user list and find entries that aren't in the
        // member list.  These were removed.
        for (GList *this_user = group->users; this_user != NULL; this_user = this_user->next) {
            if (! g_list_find_custom(numbers, this_user->data, (GCompareFunc)g_strcmp0)) {
                *removed = g_list_append(*removed, g_strdup(this_user->data));
            }
        }
    }

    if (added != NULL) {
        *added = NULL;

        // Go through our member list and find entries that aren't in the
        // user list.  These were added.
        for (GList *this_number = numbers; this_number != NULL; this_number = this_number->next) {
            if (! g_list_find_custom(group->users, this_number->data, (GCompareFunc)g_strcmp0)) {
                *added = g_list_append(*added, g_strdup(this_number->data));
            }
        }
    }

    if (group->users) {
        g_list_free_full(group->users, g_free);
    }

    group->users = numbers;
}

/*
 * Function to add a set of users to a Pidgin conversation.  The main logic,
 * here, is setting up the flags appropriately.
 */
void
signald_add_users_to_conv(SignaldAccount *sa, SignaldGroup *group, GList *users)
{
    GList *flags = NULL;
    // replace uuid (groupv2) by the user's name
    GSList *buddies = purple_find_buddies (sa->account, NULL);
    GList *user = users;
    int i = 0;
    while (user != NULL) {
        flags = g_list_append(flags, GINT_TO_POINTER(PURPLE_CBFLAGS_NONE));
        GSList *found = g_slist_find_custom(buddies, user->data, (GCompareFunc)signald_find_uuid_user);
        if (found) {
            user->data = g_strdup ((gpointer) purple_buddy_get_name (found->data));
        }
        user = user->next;
        i++;
    }
    purple_chat_conversation_add_users(PURPLE_CONV_CHAT(group->conversation), users, NULL, flags, FALSE);
    g_list_free(flags);
}

/*
 * Add a new group to our list of known groups.  Takes the group id, name, and
 * the set of members supplied.
 */
void
signald_add_group(SignaldAccount *sa, const char *groupId, const char *groupName,
                                      const char *avatar, JsonArray *members)
{
    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupId);

    if (group != NULL) {
        // Belt and suspenders check to make sure we don't join the same group twice.
        return;
    }

    group = g_new0(SignaldGroup, 1);

    // We hash the group ID to create a persistent ID for the chat.  It's not
    // stated anywhere that this is required, but it seems to have resolved some
    // weird issues I was having, so...

    group->id = g_str_hash(groupId);
    group->name = g_strdup(groupName);
    group->conversation = NULL;
    group->users = NULL;

    signald_update_group_user_list(sa, group, members, NULL, NULL);

    group->chat = signald_blist_find_chat(sa, groupId);

    if (group->chat == NULL) {
        GHashTable *comp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

        g_hash_table_insert(comp, g_strdup("name"), g_strdup(group->name));
        g_hash_table_insert(comp, g_strdup("groupId"), g_strdup(groupId));

        group->chat = purple_chat_new(sa->account, group->name, comp);

        purple_blist_add_chat(group->chat, NULL, NULL);
        purple_blist_node_set_bool((PurpleBlistNode *)group->chat, "gtk-persistent", TRUE);
    }

    signald_update_group_avatar (sa, group, avatar);

    g_hash_table_insert(sa->groups, g_strdup(groupId), group);
}

/*
 * Given a group ID, open up an actual conversation for the group.  This is
 * ultimately what opens the chat window, channel, etc.
 */
void
signald_open_conversation(SignaldAccount *sa, const char *groupId)
{
    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupId);

    if (group == NULL) {
        return;
    }

    if (group->conversation != NULL) {
        return;
    }

    // This is the magic that triggers the chat to actually open.
    group->conversation = serv_got_joined_chat(sa->pc, group->id, group->name);

    purple_conv_chat_set_topic(PURPLE_CONV_CHAT(group->conversation), group->name, group->name);

    // Squirrel away the group ID as part of the conversation for easy access later.
    purple_conversation_set_data(group->conversation, SIGNALD_CONV_GROUPID_KEY, g_strdup(groupId));

    // Populate the channel user list.
    signald_add_users_to_conv(sa, group, group->users);
}

/*
 * Function to destroy the information about a Signal group chat when the user
 * has truly left the chat (as opposed to just closing it).
 */
void
signald_quit_group(SignaldAccount *sa, const char *groupId)
{
    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupId);

    if (group == NULL) {
        return;
    }

    if (group->conversation != NULL) {
        serv_got_chat_left(sa->pc, group->id);
    }

    if (group->chat != NULL) {
        // This also deallocates the chat
        purple_blist_remove_chat(group->chat);
    }

    g_free(group->name);
    g_list_free_full(group->users, g_free);

    // This will free the key and the group automatically.
    g_hash_table_remove(sa->groups, groupId);
}

void
signald_rename_group(SignaldAccount *sa, const char *groupId, const char *groupName)
{
    SignaldGroup *group = g_hash_table_lookup(sa->groups, groupId);

    g_free(group->name);

    group->name = g_strdup(groupName);

    purple_blist_alias_chat(group->chat, groupName);

    g_hash_table_replace(purple_chat_get_components(group->chat),
                         g_strdup("name"),
                         g_strdup(groupName));

    if (group->conversation) {
        purple_conversation_set_name(group->conversation, groupName);
    }
}

void
signald_accept_groupV2_invitation(SignaldAccount *sa, const char *groupId, JsonArray *pendingMembers)
{
    if (sa->uuid == NULL) {
        // this account's uuid is not known. cannot do anything with v2 groups.
    }
    // See if we're a pending member of the group v2.
    gboolean pending = signald_members_contains_uuid(sa, pendingMembers, sa->uuid);
    if (pending) {
        // we are a pending member, join it
        JsonObject *message = json_object_new();
        json_object_set_string_member(message, "type", "accept_invitation");
        json_object_set_string_member(message, "account", purple_account_get_username(sa->account));
        json_object_set_string_member(message, "groupID", groupId);
        if (!signald_send_json(sa, message)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not send message for accepting group invitation."));
        }
    }
}

/*
 * Swiss army knife function for all possible operations on a group chat,
 * including joining, leaving, or changes to group membership.
 */
void
signald_update_group(SignaldAccount *sa, const char *groupId, const char *groupName,
                                         const char *avatar, JsonArray *members)
{
    // Find any existing group entry if we have one.
    SignaldGroup *group = g_hash_table_lookup(sa->groups, groupId);

    // See if we're a member of the group.
    char *username = (char *)purple_account_get_username(sa->account);
    gboolean in_group = signald_members_contains_number(sa, members, username);
    // See if we're a member of the group v2.
    if (sa->uuid) {
        in_group = in_group || signald_members_contains_uuid(sa, members, sa->uuid);
    }

    if ((group == NULL) && (!in_group)) {
        // Chat that we neither know about nor we're in.
        // Let's see if we had an old buddy list entry we should delete.
        PurpleChat *chat = signald_blist_find_chat(sa, groupId);

        if (chat != NULL) {
            purple_blist_remove_chat(chat);
        }

        return;
    } else if ((group == NULL) && in_group) {
        // Brand new chat and we're a member?  Add it!
        signald_add_group(sa, groupId, groupName, avatar, members);
        // Now open the conversation window.
        if (purple_account_get_bool(sa->account, "auto-join-group-chats", FALSE)) {
            signald_open_conversation(sa, groupId);
        }

        return;
    } else if ((group != NULL) && ! in_group) {
        // Existing chat but we're *not* a member?  Quit it.
        signald_quit_group(sa, groupId);

        return;
    }

    // So conv != NULL and in_group, which means we are both in this group and we know about it,
    // so this is an update of the group membership.

    GList *added = NULL;
    GList *removed = NULL;

    signald_update_group_user_list(sa, group, members, &added, &removed);

    signald_update_group_avatar (sa, group, avatar);

    if (group->conversation != NULL) {
        if (removed != NULL) {
            purple_conv_chat_remove_users(PURPLE_CONV_CHAT(group->conversation), removed, NULL);
        }

        if (added != NULL) {
            signald_add_users_to_conv(sa, group, added);
        }
    }

    if (! purple_strequal(group->name, groupName)) {
        signald_rename_group(sa, groupId, groupName);
    }

    g_list_free_full(added, g_free);
    g_list_free_full(removed, g_free);
}

/**
 ** Signal protocol functions and callbacks.
 **/
void
signald_process_group(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);

    signald_update_group(sa,
        json_object_get_string_member(obj, "groupId"),
        json_object_get_string_member(obj, "name"),
        json_object_get_string_member(obj, "avatarId"),
        json_object_get_array_member(obj, "members")
    );
}

void
signald_process_groupV2_obj(SignaldAccount *sa, JsonObject *obj)
{
    if (purple_account_get_bool(sa->account, "auto-join-group-chats", FALSE)) {
        signald_accept_groupV2_invitation(sa,
            json_object_get_string_member(obj, "id"),
            json_object_get_array_member(obj, "pendingMembers")
        );
    }

    signald_update_group(sa,
        json_object_get_string_member(obj, "id"),
        json_object_get_string_member(obj, "title"),
        json_object_get_string_member(obj, "avatar"),
        json_object_get_array_member(obj, "members")
    );
}

void
signald_process_groupV2(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);
    signald_process_groupV2_obj(sa, obj);
}

void
signald_parse_groupV2_list(SignaldAccount *sa, JsonArray *groups)
{
    json_array_foreach_element(groups, signald_process_groupV2, sa);
}

void
signald_parse_group_list(SignaldAccount *sa, JsonArray *groups)
{
    json_array_foreach_element(groups, signald_process_group, sa);
}

void
signald_request_group_info(SignaldAccount *sa, const char *groupid_str)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "get_group");
    json_object_set_string_member(data, "account", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "version", "v1");
    json_object_set_string_member(data, "groupID", groupid_str);

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request group info."));
    }

    json_object_unref(data);
}

void
signald_request_group_list(SignaldAccount *sa)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_groups");
    json_object_set_string_member(data, "account", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "version", "v1");

    if (!signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not request contacts."));
    }

    json_object_unref(data);
}

/*
 * Actually display a group chat message.
 */
void
signald_display_group_message(SignaldAccount *sa, const char *groupid_str, SignaldMessage *msg)
{
    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupid_str);
    if (group == NULL) {
        // I am not (yet) aware of having joined this group.
        return;
    }
    PurpleMessageFlags flags = 0;
    gboolean has_attachment = FALSE;
    GString *content = NULL;

    if (!group->conversation) {
        // In this case, we know about the conversation but it's not been
        // opened yet so there's no place to write the message.
        signald_open_conversation(sa, groupid_str);
    }

    if (signald_format_message(sa, msg, &content, &has_attachment)) {
        if (has_attachment) {
            flags |= PURPLE_MESSAGE_IMAGES;
        }

        if (msg->is_sync_message) {
            flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;

            purple_conv_chat_write(PURPLE_CONV_CHAT(group->conversation),
                                   msg->conversation_name,
                                   content->str, flags,
                                   msg->timestamp);
        } else {
            flags |= PURPLE_MESSAGE_RECV;

            // pretend the user's nick was mentioned in order to force
            // a full notification as for instant messages
            if (purple_account_get_bool (sa->account, "group-msg-notifications", FALSE))
                flags |= PURPLE_MESSAGE_NICK;

            purple_serv_got_chat_in(sa->pc,
                                    group->id,
                                    msg->conversation_name,
                                    flags,
                                    content->str,
                                    msg->timestamp);
        }
    }
    g_string_free(content, TRUE);
}

/*
 * Process a message we've received from signald that's directed at a group
 * chat.
 *
 * This could be a message indicating an update to the group membership, an
 * indication that we've quit the group (possibly from another client), or
 * finally that a message has arrived.
 */
void
signald_process_group_message(SignaldAccount *sa, SignaldMessage *msg)
{
    JsonObject *groupInfo = json_object_get_object_member(msg->data, "group");

    const gchar *type = json_object_get_string_member(groupInfo, "type");
    const gchar *groupid_str = json_object_get_string_member(groupInfo, "groupId");
    const gchar *avatar = json_object_get_string_member(groupInfo, "avatar");
    const gchar *groupname = json_object_get_string_member(groupInfo, "name");

    if (purple_strequal(type, "UPDATE")) {
        // Group membership update.  Let's apply it!
        signald_update_group(sa, groupid_str, groupname, avatar, json_object_get_array_member(groupInfo, "members"));

    } else if (purple_strequal(type, "QUIT")) {
        // Someone quit the group.  It could be me, or it could be another user
        // in the group.  So we need to check who originated the event and then
        // either update the membership or record that we've quit the group.
        char *username = (char *)purple_account_get_username(sa->account);
        const char *quit_source = signald_get_number_from_field(sa, msg->envelope, "source");

        if (purple_strequal(username, quit_source)) {
            signald_quit_group(sa, groupid_str);
        } else {
            signald_update_group(sa, groupid_str, groupname, avatar, json_object_get_array_member(groupInfo, "members"));
        }

    } else if (purple_strequal(type, "DELIVER")) {
        // Alright, it's a message, so we need to write it into the conversation.
        signald_display_group_message(sa, groupid_str, msg);
    }
}

/*
 * Process a message we've received from signald that's directed at a group 
 * v2 chat.
 *
 * Can only receive messages, no administrative functions are implemented.
 */
void
signald_process_groupV2_message(SignaldAccount *sa, SignaldMessage *msg)
{
    JsonObject *groupInfo = json_object_get_object_member(msg->data, "groupV2");
    const gchar *groupid_str = json_object_get_string_member(groupInfo, "id");

    SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupid_str);
    if (group == NULL) {
        signald_request_group_info(sa, groupid_str);
    }

    signald_display_group_message(sa, groupid_str, msg);
}

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    gchar *groupId = signald_find_groupid_for_conv_id(sa, id);
    PurpleConvChat *conv = PURPLE_CONV_CHAT(purple_find_chat(sa->pc, id));

    if ((groupId == NULL) || (conv == NULL)) {
        return 0;
    }

    int ret = signald_send_message(sa, SIGNALD_MESSAGE_TYPE_GROUP, groupId, message);

    if (ret > 0) {
        purple_conv_chat_write(conv, groupId, message, flags, time(NULL));
    }

    return ret;
}

void
signald_join_chat(PurpleConnection *pc, GHashTable *data)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    const char *name = g_hash_table_lookup(data, "name");
    gchar *groupId = signald_find_groupid_for_conv_name(sa, (char *)name);

    if (groupId != NULL) {
        SignaldGroup *group = (SignaldGroup *)g_hash_table_lookup(sa->groups, groupId);

        if (group->conversation == NULL) {
            signald_open_conversation(sa, groupId);
        }

        return;
    }

    if (FALSE) {
        // TODO: this breaks V2 groups :(
        JsonObject *message = json_object_new();

        json_object_set_string_member(message, "type", "update_group");
        json_object_set_string_member(message, "username", purple_account_get_username(sa->account));
        json_object_set_string_member(message, "groupName", name);

        if (! signald_send_json(sa, message)) {
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not send message for joining group."));
        }

        // Once the above send completes we'll get a "group_created" event that'll
        // trigger the subsequent actions to make the channel available.

        json_object_unref(message);
    }

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
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not send message for leaving group."));
    }

    json_object_unref(message);

    signald_quit_group(sa, groupId);
}

void
signald_chat_rename(PurpleConnection *pc, PurpleChat *chat)
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    char *groupId = g_hash_table_lookup(chat->components, "groupId");

    if (groupId == NULL) {
        return;
    }

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "update_group");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));
    json_object_set_string_member(data, "recipientGroupId", groupId);
    json_object_set_string_member(data, "groupName", chat->alias);

    if (! signald_send_json(sa, data)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write message for renaming group."));
    }

    signald_rename_group(sa, groupId, chat->alias);

    json_object_unref(data);
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
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write message for inviting contacts to group."));
    }

    json_object_unref(data);

}

/**
 ** Functions to supply Pidgin with metadata about this plugin.
 **/
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
signald_set_chat_topic(PurpleConnection *pc, int id, const char *topic)
{
    // Nothing to do here. For some reason this callback has to be
    // registered if Pidgin is going to enable the "Alias..." menu
    // option in the conversation.
}
