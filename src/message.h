#ifndef __SIGNALD_MESSAGE_H__
#define __SIGNALD_MESSAGE_H__

typedef enum {
    SIGNALD_MESSAGE_TYPE_DIRECT = 1,
    SIGNALD_MESSAGE_TYPE_GROUPV2 = 3
} SignaldMessageType;

typedef struct {
    SignaldMessageType type;
    gboolean is_sync_message;

    time_t timestamp;
    gchar *sender_uuid;

    JsonObject *envelope;
    JsonObject *data;
} SignaldMessage;

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
#endif
