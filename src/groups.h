#ifndef __SIGNALD_GROUPS_H__
#define __SIGNALD_GROUPS_H__

#define SIGNALD_CONV_GROUPID_KEY "signalGroupId"

// Instances of this object are stored in SignaldAccount.  The IDs are
// Signal group IDs that are used as keys in a hash table where these
// entries are stored.
typedef struct {
    // Pidgin chat ID we'll be using.
    int id;

    // The user-friendly name from this group.
    char *name;

    // The buddy list entry that represents this signal group.
    PurpleChat *chat;

    // The active conversation for this entry.
    PurpleConversation *conversation;

    // The list of users in this group.
    GList *users;
} SignaldGroup;


void
signald_process_groupV2_obj(SignaldAccount *sa, JsonObject *obj);

void
signald_parse_group_list(SignaldAccount *sa, JsonArray *groups);

void
signald_parse_groupV2_list(SignaldAccount *sa, JsonArray *groups);

void
signald_request_group_list(SignaldAccount *sa);

void
signald_process_group_message(SignaldAccount *sa, SignaldMessage *msg);

void
signald_process_groupV2_message(SignaldAccount *sa, SignaldMessage *msg);

/*
 * Pidgin hooks for group handling
 */

GList *
signald_chat_info(PurpleConnection *pc);

GHashTable
*signald_chat_info_defaults(PurpleConnection *pc, const char *chat_name);

void
signald_join_chat(PurpleConnection *pc, GHashTable *data);

void
signald_chat_leave(PurpleConnection *pc, int id);

void
signald_chat_rename(PurpleConnection *pc, PurpleChat *chat);

void
signald_chat_invite(PurpleConnection *pc, int id, const char *message, const char *who);

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags);

void
signald_set_chat_topic(PurpleConnection *pc, int id, const char *topic);

#endif
