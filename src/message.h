#pragma once

#include "structs.h"

const char *
signald_get_uuid_from_address(JsonObject *obj, const char *address_key);

gboolean
signald_format_message(SignaldAccount *sa, JsonObject *data, GString **target, gboolean *has_attachment);

void
signald_process_message(SignaldAccount *sa, JsonObject *obj);

int
signald_send_message(SignaldAccount *sa, const gchar *who, gboolean is_chat, const char *message);

void
signald_send_acknowledged(SignaldAccount *sa, JsonObject *data);

void
signald_display_message(SignaldAccount *sa, const char *who, const char *groupId, gint64 timestamp, gboolean is_sync_message, JsonObject *message_data);

int
signald_send_im(PurpleConnection *pc, const gchar *who, const gchar *message, PurpleMessageFlags flags);

int
signald_send_chat(PurpleConnection *pc, int id, const char *message, PurpleMessageFlags flags);

void
signald_set_recipient(JsonObject *obj, const gchar *key, const gchar *recipient);
