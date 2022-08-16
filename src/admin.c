#include "admin.h"
#include "comms.h"

#include <json-glib/json-glib.h>

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
