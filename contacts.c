#include "libsignald.h"

void
signald_assume_buddy_online(PurpleAccount *account, PurpleBuddy *buddy)
{
    g_return_if_fail(buddy != NULL);
    
    if (purple_account_get_bool(account, "fake-online", TRUE)) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_online %s\n", buddy->name);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_ONLINE, NULL);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_MOBILE, NULL);
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_offline %s\n", buddy->name);
    }
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

static void
signald_add_purple_buddy(SignaldAccount *sa, const char *number, const char *name, const char *uuid, const char *avatar)
{
    g_return_if_fail(uuid && uuid[0]);
    
    const char *alias = NULL;
    // try number for an alias
    if (number && number[0]) {
        alias = number;
    }
    // prefer name over number
    if (name && name[0]) {
        alias = name;
    }
    // special case: contact to self
    if (!alias && purple_strequal(sa->uuid, uuid)) {
        alias = purple_account_get_alias(sa->account);
        if (!alias) {
            alias = purple_account_get_username(sa->account);
        }
    }

    // default: buddy identified by UUID
    PurpleBuddy *buddy = purple_find_buddy(sa->account, uuid);
    
    // however, ...
    if (number && number[0]) {
        // ...if the contact's number is known...
        PurpleBuddy *number_buddy = purple_find_buddy(sa->account, number);
        if (number_buddy) {
            // ...and the number identifies a buddy...
            purple_blist_rename_buddy(number_buddy, uuid); // rename (not alias) the buddy
            purple_buddy_set_protocol_data(number_buddy, NULL); // remove superflous UUID from the buddy
            buddy = number_buddy; // continue with the renamed buddy
            purple_debug_info(SIGNALD_PLUGIN_ID, "Migrated %s to %s.\n", number, uuid);
        }
    }
    
    if (!buddy) {
        // new buddy
        PurpleGroup *g = purple_find_group(SIGNAL_DEFAULT_GROUP);
        if (!g) {
            g = purple_group_new(SIGNAL_DEFAULT_GROUP);
            purple_blist_add_group(g, NULL);
        }
        buddy = purple_buddy_new(sa->account, uuid, alias);
        purple_blist_add_buddy(buddy, NULL, g, NULL);
        signald_assume_buddy_online(sa->account, buddy);
    }
    if (number && number[0]) {
        // add/update number
        // NOTE: the number is currently never used
        purple_buddy_set_protocol_data(buddy, g_strdup(number));
    }
    if (alias) {
        //purple_blist_alias_buddy(buddy, alias); // this overrides the alias set by the local user
        serv_got_alias(sa->pc, uuid, alias);
    }

    // Set or update avatar
    gchar* imgdata = NULL;
    gsize imglen = 0;
    if (avatar && avatar[0] && g_file_get_contents(avatar, &imgdata, &imglen, NULL)) {
      purple_buddy_icons_set_for_user(sa->account, uuid, imgdata, imglen, NULL);
    }
}

void
signald_process_contact(SignaldAccount *sa, JsonNode *node)
{
    JsonObject *obj = json_node_get_object(node);
    const char *name = json_object_get_string_member(obj, "name");
    const char *avatar = json_object_get_string_member(obj, "avatar");
    JsonObject *address = json_object_get_object_member(obj, "address");
    const char *number = json_object_get_string_member(address, "number");
    const char *uuid = json_object_get_string_member(address, "uuid");
    signald_add_purple_buddy(sa, number, name, uuid, avatar);
}

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *profiles)
{
    for (guint i = 0; i < json_array_get_length(profiles); i++) {
        signald_process_contact(sa, json_array_get_element(profiles, i));
    }
    //TODO: mark buddies not in contact list but in buddy list as "deleted"
}

/*
 * Purple function: Request information about a buddy.
 * 
 * See @signald_process_profile on how the answer is parsed.
 */
void
signald_get_info(PurpleConnection *pc, const char *who) {
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    g_return_if_fail(sa->uuid);
    JsonObject *message = json_object_new();
    json_object_set_string_member(message, "type", "get_profile");
    json_object_set_string_member(message, "account", sa->uuid);
    JsonObject *address = json_object_new();
    json_object_set_string_member(address, "uuid", who);
    json_object_set_object_member(message, "address", address);
    if (!signald_send_json(sa, message)) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not send request to get profile.."));
    }
    json_object_unref(message);
}

/*
 * Helper function for @signald_process_profile.
 */
static void
signald_process_profile_info_member(JsonObject *object, const gchar *member_name, JsonNode *member_node, gpointer user_data) {
    if (JSON_NODE_HOLDS_OBJECT(member_node)) {
        JsonObject *obj = json_node_get_object(member_node);
        json_object_foreach_member(obj, signald_process_profile_info_member, user_data);
    }
    if (JSON_NODE_HOLDS_VALUE(member_node)) {
        PurpleNotifyUserInfo *user_info = user_data;
        GValue value = G_VALUE_INIT;
        json_node_get_value(member_node, &value);
        GValue string = G_VALUE_INIT;
        g_value_init(&string, G_TYPE_STRING);
        g_value_transform(&value, &string);
        purple_notify_user_info_add_pair_plaintext(user_info, member_name,  g_value_get_string(&string));
    }
}

/*
 * Recursively flattens profile information into a sequence of key value pairs for information display.
 */
void
signald_process_profile(SignaldAccount *sa, JsonObject *obj) {
    JsonObject *address = json_object_get_object_member(obj, "address");
    g_return_if_fail(address);
    const char *uuid = json_object_get_string_member(address, "uuid");
    g_return_if_fail(uuid && uuid[0]);
    
    PurpleNotifyUserInfo *user_info = purple_notify_user_info_new();
    json_object_foreach_member(obj, signald_process_profile_info_member, (gpointer) user_info);
    purple_notify_userinfo(sa->pc, uuid, user_info, NULL, NULL);
    purple_notify_user_info_destroy(user_info);
}

void
signald_add_buddy(PurpleConnection *pc, PurpleBuddy *buddy, PurpleGroup *group
#if PURPLE_VERSION_CHECK(3, 0, 0)
                  ,
                  const char *message
#endif
                  )
{
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);
    signald_assume_buddy_online(sa->account, buddy);
    // does not actually do anything. buddy is added to pidgin's local list and is usable from there.
}
