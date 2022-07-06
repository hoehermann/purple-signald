#ifndef __SIGNALD_GROUPS_H__
#define __SIGNALD_GROUPS_H__

void
signald_process_groupV2_obj(SignaldAccount *sa, JsonObject *obj);

void
signald_parse_groupV2_list(SignaldAccount *sa, JsonArray *groups);

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

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags);

void
signald_set_chat_topic(PurpleConnection *pc, int id, const char *topic);

void
signald_request_group_list(SignaldAccount *sa);

char *signald_get_chat_name(GHashTable *components);

#endif
