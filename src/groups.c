#include "groups.h"
#include "purple_compat.h"
#include "defines.h"
#include "comms.h"
#include "message.h"
#include "json-utils.h"
#include <sys/un.h> // for sockaddr_un
#include <sys/socket.h> // for socket and read

PurpleGroup * signald_get_purple_group() {
    PurpleGroup *group = purple_blist_find_group("Signal");
    if (!group) {
        group = purple_group_new("Signal");
        purple_blist_add_group(group, NULL);
    }
    return group;
}

/*
 * Add group chat to blist. Updates existing group chat if found.
 */
PurpleChat * signald_ensure_group_chat_in_blist(
    PurpleAccount *account, const char *groupId, const char *title, const char *avatar
) {
    gboolean fetch_contacts = TRUE;

    PurpleChat *chat = purple_blist_find_chat(account, groupId);

    if (chat == NULL && fetch_contacts) {
        GHashTable *comp = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
        g_hash_table_insert(comp, "name", g_strdup(groupId));
        g_hash_table_insert(comp, "title", g_strdup(title));
        chat = purple_chat_new(account, groupId, comp);
        PurpleGroup *group = signald_get_purple_group();
        purple_blist_add_chat(chat, group, NULL);
        // TODO: find out if purple_serv_got_joined_chat(gc, …) should be called
    }

    // if gtk-persistent is set, the user may close the gtk conversation window without leaving the chat
    // however, the window remains hidden even if new messages arrive
    //purple_blist_node_set_bool(&chat->node, "gtk-persistent", TRUE);

    if (title != NULL && fetch_contacts) {
        purple_blist_alias_chat(chat, title);
    }

    // set or update avatar
    if ((avatar != NULL) && (chat != NULL) &&
        ((! purple_buddy_icons_node_has_custom_icon ((PurpleBlistNode*)chat))
         || purple_account_get_bool(account, "use-group-avatar", TRUE))) {
        purple_buddy_icons_node_set_custom_icon_from_file ((PurpleBlistNode*)chat, avatar);
        purple_blist_update_node_icon ((PurpleBlistNode*)chat);
    }

    return chat;
}

/*
 * Given a JsonNode for a group member, get the uuid.
 */
char *
signald_get_group_member_uuid(JsonNode *node)
{
    JsonObject *member = json_node_get_object(node);
    return (char *)json_object_get_string_member(member, "uuid");
}

/*
 * Given a list of members, get back the list of corresponding uuid.
 * This function makes a copy of the uuids so they must be freed via g_list_free_full.
 */
GList *
signald_members_to_uuids(JsonArray *members)
{
    GList *uuids = NULL;

    for (
        GList *this_member = json_array_get_elements(members);
        this_member != NULL;
        this_member = this_member->next
    ) {
        JsonNode *element = (JsonNode *)(this_member->data);
        char *uuid = signald_get_group_member_uuid(element);
        uuids = g_list_append(uuids, g_strdup(uuid));
    }
    return uuids;
}

gboolean
signald_members_contains_uuid(JsonArray *members, char *uuid)
{
    GList *uuids = signald_members_to_uuids(members);
    gboolean result = g_list_find_custom(uuids, uuid, (GCompareFunc)g_strcmp0) != NULL;
    g_list_free_full(uuids, g_free);
    return result;
}

void
signald_accept_groupV2_invitation(SignaldAccount *sa, const char *groupId, JsonArray *pendingMembers)
{
    g_return_if_fail(sa->uuid);

    // See if we're a pending member of the group v2.
    gboolean pending = signald_members_contains_uuid(pendingMembers, sa->uuid);
    if (pending) {
        // we are a pending member, join it
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "accept_invitation");
        json_object_set_string_member(data, "account", sa->uuid);
        json_object_set_string_member(data, "groupID", groupId);
        signald_send_json_or_display_error(sa, data);
        json_object_unref(data);
    }
}

/*
 * This handles incoming Signal group information,
 * overwriting participant lists where appropriate.
 */
// TODO: a soft "remove who left, add who was added" would be nicer than
// this blunt-force "remove everyone and re-add" approach
void
signald_chat_set_participants(PurpleAccount *account, const char *groupId, JsonArray *members) {
    GList *uuids = signald_members_to_uuids(members);
    PurpleConvChat *conv_chat = purple_conversations_find_chat_with_account(groupId, account);
    if (conv_chat != NULL) { // only consider active chats
        purple_conv_chat_clear_users(conv_chat);
        for (GList * uuid_elem = uuids; uuid_elem != NULL; uuid_elem = uuid_elem->next) {
            const char* uuid = uuid_elem->data;
            PurpleConvChatBuddyFlags flags = 0;
            purple_conv_chat_add_user(conv_chat, uuid, NULL, flags, FALSE);
        }
    }
    g_list_free_full(uuids, g_free);
}

void
signald_process_groupV2_obj(SignaldAccount *sa, JsonObject *obj)
{
    const char *groupId = json_object_get_string_member(obj, "id");
    const char *title = json_object_get_string_member(obj, "title");
    const char *avatar = json_object_get_string_member_or_null(obj, "avatar");

    purple_debug_info(SIGNALD_PLUGIN_ID, "Processing group ID %s, %s\n", groupId, title);

    if (purple_account_get_bool(sa->account, "auto-accept-invitations", FALSE)) {
        signald_accept_groupV2_invitation(sa, groupId, json_object_get_array_member(obj, "pendingMembers"));
    }

    signald_ensure_group_chat_in_blist(sa->account, groupId, title, avatar); // for joining later

    // update participants
    signald_chat_set_participants(sa->account, groupId, json_object_get_array_member(obj, "members"));

    // set title as topic
    PurpleConversation *conv = purple_find_chat(sa->pc, g_str_hash(groupId));
    if (conv != NULL) {
        purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), groupId, title);
    }

    // the user might have requested a room list, fill it
    if (sa->roomlist) {
        PurpleRoomlistRoom *room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM, groupId, NULL); // this sets the room's name
        purple_roomlist_room_add_field(sa->roomlist, room, title); // this sets the room's title
        purple_roomlist_room_add(sa->roomlist, room);
    }
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

    if (sa->roomlist) {
        // in case the user explicitly requested a room list, the query is finished now
        purple_roomlist_set_in_progress(sa->roomlist, FALSE);
        purple_roomlist_unref(sa->roomlist); // unref here, roomlist may remain in ui
        sa->roomlist = NULL;
    }
}

void
signald_request_group_info(SignaldAccount *sa, const char *groupId)
{
    g_return_if_fail(sa->uuid);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "get_group");
    json_object_set_string_member(data, "account", sa->uuid);
    json_object_set_string_member(data, "groupID", groupId);

    signald_send_json_or_display_error(sa, data);

    json_object_unref(data);
}

void
signald_request_group_list(SignaldAccount *sa)
{
    g_return_if_fail(sa->uuid);
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "list_groups");
    json_object_set_string_member(data, "account", sa->uuid);
    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

PurpleConversation * signald_enter_group_chat(PurpleConnection *pc, const char *groupId, const char *title) {
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    // use hash of groupId for chat id number
    PurpleConversation *conv = purple_find_chat(pc, g_str_hash(groupId));
    if (conv == NULL) {
        conv = serv_got_joined_chat(pc, g_str_hash(groupId), groupId);
        purple_conversation_set_data(conv, "name", g_strdup(groupId));
        purple_conv_chat_set_nick(PURPLE_CONV_CHAT(conv), sa->uuid); // identify ourselves in this chat
        if (title != NULL) {
            purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), groupId, title);
        }
        signald_request_group_info(sa, groupId);
    }
    return conv;
}

void
signald_join_chat(PurpleConnection *pc, GHashTable *data)
{
    const char *groupId = g_hash_table_lookup(data, "name");
    const char *title = g_hash_table_lookup(data, "title");
    if (groupId != NULL) {
        signald_enter_group_chat(pc, groupId, title);
    }
}

/*
 * Information identifying a chat.
 */
GList *
signald_chat_info(PurpleConnection *pc)
{
    GList *infos = NULL;

    struct proto_chat_entry *pce;

    pce = g_new0(struct proto_chat_entry, 1);
    pce->label = "Group ID";
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

/*
 * Get the identifying aspect of a chat (as passed to serv_got_joined_chat)
 * given the chat_info entries. In Signal, this is the groupId.
 *
 * Borrowed from:
 * https://github.com/matrix-org/purple-matrix/blob/master/libmatrix.c
 */
char *signald_get_chat_name(GHashTable *components)
{
    const char *groupId = g_hash_table_lookup(components, "name");
    return g_strdup(groupId);
}

/*
 * This requests a list of rooms representing the Signal group chats.
 * The request is asynchronous. Responses are handled by signald_roomlist_add_room.
 *
 * A purple room has an identifying name – for Signal that is the UUID.
 * A purple room has a list of fields – in our case only Signal group name.
 *
 * Some services like spectrum expect the human readable group name field key to be "topic",
 * see RoomlistProgress in https://github.com/SpectrumIM/spectrum2/blob/518ba5a/backends/libpurple/main.cpp#L1997
 * In purple, the roomlist field "name" gets overwritten in purple_roomlist_room_join, see libpurple/roomlist.c.
 */
PurpleRoomlist *
signald_roomlist_get_list(PurpleConnection *pc) {
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    if (sa->roomlist != NULL) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Already getting roomlist.");
    } else {
        sa->roomlist = purple_roomlist_new(sa->account);
        purple_roomlist_set_in_progress(sa->roomlist, TRUE);
        GList *fields = NULL;
        fields = g_list_append(fields, purple_roomlist_field_new(
            PURPLE_ROOMLIST_FIELD_STRING, "Group Name", "topic", FALSE
        ));
        purple_roomlist_set_fields(sa->roomlist, fields);
        signald_request_group_list(sa);
    }
    return sa->roomlist;
}


static void signald_leave_group(SignaldAccount *sa, const char *groupID)
{
    g_return_if_fail(groupID != NULL);
    g_return_if_fail(sa->uuid != NULL);

    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "leave_group");
    json_object_set_string_member(data, "account", sa->uuid);
    json_object_set_string_member(data, "groupID", groupID);
    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

static void
signald_leave_chat(PurpleBlistNode *node, gpointer userdata)
{
    g_return_if_fail(node != NULL);
    PurpleChat *chat = (PurpleChat *)node;
    
    PurpleAccount *account = purple_chat_get_account(chat);
    PurpleConnection *pc = purple_account_get_connection(account);
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    GHashTable *components = purple_chat_get_components(chat);
    const char * groupID = g_hash_table_lookup(components, "name");
    
    signald_leave_group(sa, groupID);
}

static GList *
signald_chat_menu(PurpleChat *chat)
{
    GList *menu = NULL;

    PurpleMenuAction * act = purple_menu_action_new(
        "Leave Group",
        PURPLE_CALLBACK(signald_leave_chat),
        NULL, /* userdata passed to the callback */
        NULL /* child menu items */
    );
    menu = g_list_append(menu, act);

    return menu;
}

GList *signald_blist_node_menu(PurpleBlistNode *node) {
    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
        return signald_chat_menu((PurpleChat *) node);
    } else {
        return NULL;
    }
}

void signald_process_leave_group(SignaldAccount *sa, JsonObject *data) {
    JsonObject *v2 = json_object_get_object_member(data, "v2");
    const gchar *id = json_object_get_string_member(v2, "id");
    PurpleChat * chat = purple_blist_find_chat(sa->account, id);
    purple_blist_remove_chat(chat);
}

static PurpleChat * signald_blist_find_chat(PurpleAccount *account, int id) {
    for (PurpleBlistNode *group = purple_blist_get_root(); group != NULL; group = group->next) {
        for (PurpleBlistNode *node = group->child; node != NULL; node = node->next) {
            if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
                PurpleChat *chat = (PurpleChat*)node;
                if (account == chat->account) {
                    const gchar *groupId = g_hash_table_lookup(chat->components, "name");
                    if (id == g_str_hash(groupId)) {
                        return chat;
                    }
                }
            }
        }
    }
    return NULL;
}

/*
 * The user wants to leave a chat.
 * 
 * Unfortunately, this is called whenever a Pidgin chat window is closed
 * unless gtk-persistent is set.
 * 
 * This leaves the chat iff it was not added to the buddy list.
 */
void
signald_chat_leave(PurpleConnection *pc, int id) {
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    PurpleChat * chat = signald_blist_find_chat(sa->account, id);
    if (!chat) {
        PurpleConversation *conv = purple_find_chat(pc, id);
        const gchar * groupID = purple_conversation_get_name(conv);
        signald_leave_group(sa, groupID);
    }
}

char *signald_group_chat_get_participant_alias(PurpleConnection *gc, int id, const char *who) {
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_group_chat_get_participant_alias called for %s\n", who);
    char *out = NULL;
    
    SignaldAccount *sa = purple_connection_get_protocol_data(gc);
    
    // TODO: combine with signald_get_info to remove redundancy
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "get_profile");
    json_object_set_string_member(data, "account", sa->uuid);
    JsonObject *address = json_object_new();
    json_object_set_string_member(address, "uuid", who);
    json_object_set_object_member(data, "address", address);
    // TODO: combine with signald_send_json to remove redundancy
    json_object_set_string_member(data, "version", "v1");
    char *json = json_object_to_string(data);
    purple_debug_info(SIGNALD_PLUGIN_ID, "Prepared: „%s“\n", json);
    
    struct sockaddr_un socket_address = {
        .sun_family = AF_UNIX,
    };
    strcpy(socket_address.sun_path, sa->socket_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(fd, (struct sockaddr *) &socket_address, sizeof(struct sockaddr_un));
    write(fd, json, strlen(json));
    write(fd, "\n", 1);
    g_free(json);
    
    json_object_unref(data);
    
    char buffer[4096];
    char *buffer_ptr = buffer;
    while(recv(fd, buffer_ptr, 1, MSG_DONTWAIT) == 1 && *buffer_ptr != '\n') {
        buffer_ptr++;
    }
    *buffer_ptr = '\0';
    purple_debug_info(SIGNALD_PLUGIN_ID, "Received: „%s“\n", buffer);
    const size_t length = buffer_ptr - buffer;
    
    close(fd);
    
    {
        JsonParser *parser = json_parser_new();
        json_parser_load_from_data(parser, buffer, length, NULL);
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        JsonObject *data = json_object_get_object_member(obj, "data");
        const char *name = json_object_get_string_member_or_null(data, "name");
        purple_debug_info(SIGNALD_PLUGIN_ID, "name is „%s“\n", name);
        out = g_strdup(name);
        g_object_unref(parser);
    }
    
    return out;
}

