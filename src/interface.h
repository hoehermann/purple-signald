#pragma once

#include <purple.h>

const char * signald_list_icon(PurpleAccount *account, PurpleBuddy *buddy);
void signald_tooltip_text(PurpleBuddy *buddy, PurpleNotifyUserInfo *user_info, gboolean full);
