#ifndef __SIGNALD_MESSAGE_H__
#define __SIGNALD_MESSAGE_H__

typedef enum {
    SIGNALD_MESSAGE_TYPE_DIRECT = 1,
    SIGNALD_MESSAGE_TYPE_GROUP = 2
} SignaldMessageType;

typedef struct {
    SignaldMessageType type;
    gboolean is_sync_message;

    time_t timestamp;
    gchar *conversation_name;

    JsonObject *envelope;
    JsonObject *data;
} SignaldMessage;

gboolean
signald_format_message(SignaldAccount *sa, SignaldMessage *msg, GString **target, gboolean *has_attachment);

gboolean
signald_parse_message(SignaldAccount *sa, JsonObject *obj, SignaldMessage *msg);

int
signald_send_message(SignaldAccount *sa, SignaldMessageType type, gchar *recipient, const char *message);

#endif
