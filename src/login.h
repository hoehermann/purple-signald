#pragma once

#include <purple.h>
#include "structs.h"

void signald_login(PurpleAccount *account);
void signald_subscribe(SignaldAccount *sa);
void signald_close(PurpleConnection *pc);
