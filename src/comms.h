#pragma once

#include <purple.h>
#include <json-glib/json-glib.h>

gchar *
json_object_to_string(JsonObject *obj);

gboolean
signald_send_json(SignaldAccount *sa, JsonObject *data);

gboolean
signald_send_json_or_display_error(SignaldAccount *sa, JsonObject *data);

void
signald_read_cb(gpointer data, gint source, PurpleInputCondition cond);
