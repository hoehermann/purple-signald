#include "status.h"
#include "defines.h"

GList * signald_status_types(PurpleAccount *account) {
    GList *types = NULL;
    PurpleStatusType *status;

    status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, SIGNALD_STATUS_STR_ONLINE, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_AWAY, SIGNALD_STATUS_STR_AWAY, NULL, FALSE, FALSE, FALSE);
    types = g_list_prepend(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, SIGNALD_STATUS_STR_OFFLINE, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_MOBILE, SIGNALD_STATUS_STR_MOBILE, NULL, FALSE, FALSE, TRUE);
    types = g_list_prepend(types, status);

    return types;
}
