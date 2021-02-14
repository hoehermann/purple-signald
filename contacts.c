#include "libsignald.h"

void
signald_assume_buddy_online(PurpleAccount *account, PurpleBuddy *buddy)
{
    if (purple_account_get_bool(account, "fake-online", TRUE)) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_online %s\n", buddy->name);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_ONLINE, NULL);
        purple_prpl_got_user_status(account, buddy->name, SIGNALD_STATUS_STR_MOBILE, NULL);
    }
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_assume_buddy_offline %s\n", buddy->name);
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
signald_add_purple_buddy(SignaldAccount *sa,
                         const char *username, const char *alias,
                         const char *uuid, const char *avatar_file)
{
    GSList *buddies;
    PurpleBuddy *buddy;
    gchar* imgdata;
    gsize imglen = 0;

    if (avatar_file) {
      if (! g_file_get_contents (avatar_file, &imgdata, &imglen, NULL)) {
          imglen = 0;
          purple_debug_error(SIGNALD_PLUGIN_ID, "Can not read '%s'\n", avatar_file);
      }
    }

    buddies = purple_find_buddies(sa->account, username);

    if (buddies) {

        //Already known => only add uuid if not already exists in protocol data
        buddy = buddies->data;
        if (! purple_buddy_get_protocol_data(buddy)) {
            char *uuid_data = g_malloc (SIGNALD_UUID_LEN);
            strcpy (uuid_data, uuid);
            purple_buddy_set_protocol_data(buddy, uuid_data);
        }
        g_slist_free (buddies);

        //TODO: Update alias

    } else {

        //TODO: Remove old buddies: purple_blist_remove_buddy(b);

        //New buddy
        PurpleGroup *g = purple_find_group(SIGNAL_DEFAULT_GROUP);
        if (!g) {
            g = purple_group_new(SIGNAL_DEFAULT_GROUP);
            purple_blist_add_group(g, NULL);
        }
        buddy = purple_buddy_new(sa->account, username, alias);

        char *uuid_data = g_malloc (SIGNALD_UUID_LEN);
        strcpy (uuid_data, uuid);
        purple_buddy_set_protocol_data (buddy, uuid_data);

        purple_blist_add_buddy(buddy, NULL, g, NULL);
        purple_blist_alias_buddy(buddy, alias);

        signald_assume_buddy_online(sa->account, buddy);
    }

    // Set or update avatar
    if (buddy && imglen) {
      purple_buddy_icons_set_for_user (sa->account, username, imgdata, imglen, NULL);
    }
}

void
signald_process_contact(SignaldAccount *sa, JsonNode *node, const char *avatar_dir)
{
    JsonObject *obj = json_node_get_object(node);
    const char *alias = json_object_get_string_member(obj, "name");
    JsonObject *address = json_object_get_object_member(obj, "address");
    const char *username = json_object_get_string_member(address, "number");
    const char *uuid = json_object_get_string_member(address, "uuid");

    char *avatar_file = g_strconcat (avatar_dir,
                                     g_strdup_printf(SIGNALD_AVATAR_FILE_NAME, username),
                                     (const gchar*)NULL);
    if (! g_file_test (avatar_file, G_FILE_TEST_EXISTS)) {
        avatar_file = g_strconcat (avatar_dir, uuid, (const gchar*)NULL);
        if (! g_file_test (avatar_file, G_FILE_TEST_EXISTS)) {
            avatar_file = NULL;
        }
    }

    signald_add_purple_buddy(sa, username, alias, uuid, avatar_file);

    g_free(avatar_file);
}

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *data)
{
    gchar *avatar_dir
            = g_strconcat (g_strdup_printf (SIGNALD_DATA_PATH, purple_user_dir ()),
                           SIGNALD_AVATARS_SIGNALD_DATA_PATH, (const gchar*)NULL);
    for (guint i = 0; i < json_array_get_length (data); i++) {
        signald_process_contact (sa, json_array_get_element (data, i), avatar_dir);
    }

    g_free(avatar_dir);
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
