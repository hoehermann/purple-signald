#include "libsignald.h"

static int
signald_strequalprefix (const char *s1, const char *s2)
{
    int l1 = strlen (s1);
    int l2 = strlen (s2);

    return 0 == strncmp (s1, s2, l1 < l2 ? l1 : l2);
}

void
signald_request_sync(SignaldAccount *sa)
{
    g_return_if_fail(sa->uuid);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "request_sync");
    json_object_set_string_member(data, "account", sa->uuid);
    json_object_set_boolean_member(data, "contacts", TRUE);
    json_object_set_boolean_member(data, "groups", TRUE);
    json_object_set_boolean_member(data, "configuration", FALSE);
    json_object_set_boolean_member(data, "blocked", FALSE);

    signald_send_json_or_display_error(sa, data);

    json_object_unref(data);
}

void
signald_list_contacts(SignaldAccount *sa)
{
    g_return_if_fail(sa->uuid);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_contacts");
    json_object_set_string_member(data, "account", sa->uuid);

    signald_send_json_or_display_error(sa, data);

    json_object_unref(data);

    signald_assume_all_buddies_online(sa);
}
