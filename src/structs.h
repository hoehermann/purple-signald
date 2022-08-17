#pragma once

#include <purple.h>
#include <json-glib/json-glib.h>

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;
    char *session_id;
    char *uuid; // own uuid, might be NULL – always check before use

    gboolean account_exists; // whether account exists in signald

    int socket_paths_count;
    int fd;
    int readflags;
    guint watcher;

    char *last_message; // the last message which has been sent to signald
    PurpleConversation *last_conversation; // the conversation the message is relevant to

    PurpleRoomlist *roomlist;
} SignaldAccount;
