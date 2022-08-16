#pragma once

#include "structs.h"

const char *
signald_get_uuid_from_address(JsonObject *obj, const char *address_key);

gboolean
signald_format_message(SignaldAccount *sa, SignaldMessage *msg, GString **target, gboolean *has_attachment);

gboolean
signald_parse_message(SignaldAccount *sa, JsonObject *obj, SignaldMessage *msg);

int
signald_send_message(SignaldAccount *sa, SignaldMessageType type, gchar *recipient, const char *message);

void
signald_send_acknowledged(SignaldAccount *sa, JsonObject *data);

void
signald_process_direct_message(SignaldAccount *sa, SignaldMessage *msg);

int
signald_send_im(PurpleConnection *pc, const gchar *who, const gchar *message, PurpleMessageFlags flags);
