#include "libsignald.h"

void
signald_assume_buddy_online(PurpleAccount *account, PurpleBuddy *buddy)
{
    if (purple_account_get_bool(account, "fake-online", TRUE)) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_online %s\n", buddy->name);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_ONLINE, NULL);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_MOBILE, NULL);
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
signald_add_purple_buddy(SignaldAccount *sa, const char *username, const char *alias)
{
    GSList *buddies;

    buddies = purple_find_buddies(sa->account, username);
    if (buddies) {
        //Already known => do nothing
        //TODO: Update alias
        g_slist_free(buddies);
        return;
    }
    //TODO: Remove old buddies: purple_blist_remove_buddy(b);
    //New buddy
    purple_debug_error(SIGNALD_PLUGIN_ID, "signald_add_purple_buddy(): Adding '%s' with alias '%s'\n", username, alias);

    PurpleGroup *g = purple_find_group(SIGNAL_DEFAULT_GROUP);
    if (!g) {
        g = purple_group_new(SIGNAL_DEFAULT_GROUP);
        purple_blist_add_group(g, NULL);
    }
    PurpleBuddy *b = purple_buddy_new(sa->account, username, alias);

    purple_blist_add_buddy(b, NULL, g, NULL);
    purple_blist_alias_buddy(b, alias);

    signald_assume_buddy_online(sa->account, b);
}

void
signald_process_contact(JsonArray *array, guint index_, JsonNode *element_node, gpointer user_data)
{
    SignaldAccount *sa = (SignaldAccount *)user_data;
    JsonObject *obj = json_node_get_object(element_node);
    const char *alias = json_object_get_string_member(obj, "name");
    const char *username = NULL;

    if (sa->legacy_protocol) {
      username = json_object_get_string_member(obj, "number");
    } else {
      JsonObject *address = json_object_get_object_member(obj, "address");

      username = json_object_get_string_member(address, "number");
    }

    signald_add_purple_buddy(sa, username, alias);
}

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *data)
{
    json_array_foreach_element(data, signald_process_contact, sa);
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
