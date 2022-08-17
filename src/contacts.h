#pragma once

#include "structs.h"
#include <json-glib/json-glib.h>

void
signald_assume_all_buddies_state(SignaldAccount *sa);

void
signald_parse_contact_list(SignaldAccount *sa, JsonArray *profiles);

void
signald_get_info(PurpleConnection *pc, const char *who);

void
signald_process_profile(SignaldAccount *sa, JsonObject *obj);

void
signald_add_buddy(PurpleConnection *pc, PurpleBuddy *buddy, PurpleGroup *group);

void signald_list_contacts(SignaldAccount *sa);
