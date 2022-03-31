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
    if (name && name[0]) {
        alias = name;
    } else if (purple_strequal(sa->uuid, uuid)) {
        // special case: self has no name set
        alias = purple_account_get_alias(sa->account);
        if (!alias) {
            alias = purple_account_get_username(sa->account);
        }
    }

    PurpleBuddy *buddy = purple_find_buddy(sa->account, uuid);
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
