#ifndef __SIGNALD_CONTACTS_H__
#define __SIGNALD_CONTACTS_H__

void
signald_assume_all_buddies_online(SignaldAccount *sa);

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *profiles);

void
signald_get_info(PurpleConnection *pc, const char *who);

void
signald_process_profile(SignaldAccount *sa, JsonObject *obj);

void
signald_add_buddy(PurpleConnection *pc, PurpleBuddy *buddy, PurpleGroup *group
#if PURPLE_VERSION_CHECK(3, 0, 0)
                  ,
                  const char *message
#endif
                  );

#endif
