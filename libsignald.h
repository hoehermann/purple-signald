#ifndef __SIGNALD_LIBSIGNALD_H__
#define __SIGNALD_LIBSIGNALD_H__

#ifdef ENABLE_NLS
// TODO: implement localisation
#else
#      define _(a) (a)
#      define N_(a) (a)
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#ifdef __clang__
#pragma GCC diagnostic ignored "-W#pragma-messages"
#endif
#include "purple_compat.h"
#pragma GCC diagnostic pop

#if !(GLIB_CHECK_VERSION(2, 67, 3))
#define g_memdup2 g_memdup
#endif

#include "json_compat.h"

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#ifndef SIGNALD_PLUGIN_VERSION
#error Must set SIGNALD_PLUGIN_VERSION in Makefile
#endif
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"
#define SIGNAL_DEFAULT_GROUP "Signal"

#define SIGNALD_DIALOG_TITLE "Signal Protocol"
#define SIGNALD_DIALOG_LINK "Link to Signal App"
#define SIGNALD_DEFAULT_DEVICENAME "Signal-Purple-Plugin" // must fit in HOST_NAME_MAX

#define SIGNALD_TIMEOUT_SECONDS 10
#define SIGNALD_DEFAULT_SOCKET ""
#define SIGNALD_GLOBAL_SOCKET_FILE  "signald/signald.sock"
#define SIGNALD_GLOBAL_SOCKET_PATH_VAR "/var/run"

#define SIGNALD_DATA_PATH "%s/signald"
#define SIGNALD_DATA_FILE SIGNALD_DATA_PATH "/data/%s"
#define SIGNALD_AVATARS_SIGNALD_DATA_PATH "/avatars/"
#define SIGNALD_AVATAR_FILE_NAME "contact-%s"
#define SIGNALD_PID_FILE SIGNALD_DATA_PATH "/pid"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

#define SIGNALD_UNKNOWN_SOURCE_NUMBER "unknown"

#define SIGNALD_ERR_NONEXISTUSER "Attempted to connect to a non-existant user"
#define SIGNALD_ERR_AUTHFAILED   "[401] Authorization failed!"

#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS "external-attachments"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR "external-attachments-dir"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL "external-attachments-url"

#define SIGNALD_OPTION_WAIT_SEND_ACKNOWLEDEMENT "wait-send-acknowledgement"

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

void
signald_subscribe (SignaldAccount *sa);

void signald_node_aliased(PurpleBlistNode *node, char *oldname, PurpleConnection *pc);
void signald_handle_input(SignaldAccount *sa, const char * json);

#include "message.h"
#include "direct.h"
#include "groups.h"
#include "contacts.h"
#include "link.h"
#include "comms.h"

#endif
