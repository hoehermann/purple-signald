#pragma once

#include <json-glib/json-glib.h>

GString *
signald_prepare_attachments_message(SignaldAccount *sa, JsonObject *obj);

void
signald_parse_attachment(SignaldAccount *sa, JsonObject *obj, GString *message);

char *
signald_detach_images(const char *message, JsonArray *attachments);

gchar *
signald_write_external_attachment(SignaldAccount *sa, const char *filename, const char *mimetype_remote);
