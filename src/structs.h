#pragma once

#include <purple.h>
#include <json-glib/json-glib.h>

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;
    char *session_id;
    char *uuid; // own uuid, might be NULL – always check before use

    gboolean account_exists; // whether account exists in signald
    gboolean groups_updated; // whether groups have been updated after login

    int socket_paths_count;
    int fd;
    guint watcher;

    char *last_message; // the last message which has been sent to signald
    PurpleConversation *last_conversation; // the conversation the message is relevant to

    // Maps signal group IDs to libpurple PurpleConversation objects that represent those chats.
    GHashTable *groups;
} SignaldAccount;


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
