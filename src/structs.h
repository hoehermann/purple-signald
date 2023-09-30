#pragma once

#include <purple.h>
#include <json-glib/json-glib.h>

#define SIGNALD_INPUT_BUFSIZE 500000 // TODO: research actual maximum message size

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
    char input_buffer[SIGNALD_INPUT_BUFSIZE];
    char * input_buffer_position;

    char *last_message; // the last message which has been sent to signald
    PurpleConversation *last_conversation; // the conversation the message is relevant to
    
    GQueue *replycache; // cache of messages for "reply to" function
    
    guint receipts_timer; // handler for timer which sends receipts
    GHashTable *outgoing_receipts; // buffer for receipts

    PurpleRoomlist *roomlist;
    
    char *show_profile; // name of the user-requested profile (NULL in case of system-requested profile)
} SignaldAccount;
