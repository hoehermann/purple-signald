#ifndef __SIGNALD_GROUPS_H__
#define __SIGNALD_GROUPS_H__

#define SIGNALD_CONV_GROUPID_KEY "signalGroupId"

void
signald_parse_group_list(SignaldAccount *sa, JsonArray *groups);

void
signald_request_group_list(SignaldAccount *sa);

void
signald_process_group_message(SignaldAccount *sa, SignaldMessage *msg);

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
signald_chat_invite(PurpleConnection *pc, int id, const char *message, const char *who);

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags);

#endif
