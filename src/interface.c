#include "interface.h"

const char * signald_list_icon(PurpleAccount *account, PurpleBuddy *buddy) {
    return "signal";
}

void signald_tooltip_text(PurpleBuddy *buddy, PurpleNotifyUserInfo *user_info, gboolean full) {
    char * number = purple_buddy_get_protocol_data(buddy);
    if (number && number[0]) {
        purple_notify_user_info_add_pair_plaintext(user_info, "Number", number);
    }
}
