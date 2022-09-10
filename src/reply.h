#pragma once

#include <purple.h>
#include <json-glib/json-glib.h>
#include "structs.h"

typedef struct {
    PurpleConversation *conversation;
    char *author_uuid;
    gint64 id;
    char *text;
} SignaldMessage;

GQueue * signald_replycache_init();

void signald_replycache_free(GQueue *queue);

void signald_replycache_add_message(SignaldAccount *sa, PurpleConversation *conv, const char *author_uuid, guint64 timestamp_micro, const char *body);

SignaldMessage * signald_replycache_check(SignaldAccount *sa, const gchar *message);

const gchar * signald_replycache_strip_needle(const gchar * message);

void signald_replycache_apply(JsonObject *data, const SignaldMessage * msg);
