#define _DEFAULT_SOURCE // for gethostname in unistd.h
#include <unistd.h>
#include <purple.h>
#include "defines.h"
#include "options.h"

// from https://github.com/ars3niy/tdlib-purple/blob/master/tdlib-purple.cpp
static GList * add_choice(GList *choices, const char *description, const char *value)
{
    PurpleKeyValuePair *kvp = g_new0(PurpleKeyValuePair, 1);
    kvp->key = g_strdup(description);
    kvp->value = g_strdup(value);
    return g_list_append(choices, kvp);
}

GList *
signald_add_account_options(GList *account_options)
{
    PurpleAccountOption *option;

    /*
    option = purple_account_option_bool_new(
                "Link to an existing account",
                "link",
                TRUE
                );
    account_options = g_list_append(account_options, option);
    */

    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, HOST_NAME_MAX)) {
        strcpy(hostname, SIGNALD_DEFAULT_DEVICENAME);
    }

    option = purple_account_option_string_new(
                "Name of the device for linking",
                "device-name",
                hostname // strdup happens internally
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                "Daemon signald is controlled by pidgin, not globally or by the user",
                "handle-signald",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                "Custom socket location",
                "socket",
                ""
                );
    account_options = g_list_append(account_options, option);

    {
        GList *choices = NULL;
        choices = add_choice(choices, "Online", SIGNALD_STATUS_STR_ONLINE);
        choices = add_choice(choices, "Away", SIGNALD_STATUS_STR_AWAY);
        choices = add_choice(choices, "Offline", SIGNALD_STATUS_STR_OFFLINE);
        option = purple_account_option_list_new(
            "Contacts' online state",
            "fake-status",
            choices
        );
        account_options = g_list_append(account_options, option);
    }

    option = purple_account_option_bool_new(
                "Wait for send acknowledgement",
                SIGNALD_OPTION_WAIT_SEND_ACKNOWLEDEMENT,
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                "Automatically accept invitations.",
                "auto-accept-invitations",
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                "Overwrite custom group icons by group avatar",
                "use-group-avatar",
                TRUE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_bool_new(
                "Serve attachments from external location",
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS,
                FALSE
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                "External attachment storage directory",
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR,
                ""
                );
    account_options = g_list_append(account_options, option);

    option = purple_account_option_string_new(
                "External attachment URL",
                SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL,
                ""
                );
    account_options = g_list_append(account_options, option);

    return account_options;
}
