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

#include "json_compat.h"

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#ifndef SIGNALD_PLUGIN_VERSION
#error Must set SIGNALD_PLUGIN_VERSION in Makefile
#endif
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"
#define SIGNAL_DEFAULT_GROUP "Signal"

#define SIGNALD_DIALOG_TITLE "Signal Protocol"
#define SIGNALD_DIALOG_LINK "Link to Signal App"

#define SIGNALD_TIME_OUT 10
#define SIGNALD_DEFAULT_SOCKET "/var/run/signald/signald.sock"
#define SIGNALD_DEFAULT_SOCKET_LOCAL "/tmp/signald.sock"
#define SIGNALD_START "signald -s " SIGNALD_DEFAULT_SOCKET_LOCAL " -d " SIGNALD_DATA_PATH " &"

#define SIGNALD_DATA_PATH "%s/signald"
#define SIGNALD_DATA_FILE SIGNALD_DATA_PATH "/data/%s"
#define SIGNALD_PID_FILE SIGNALD_DATA_PATH "/pid"

#define SIGNALD_TMP_QRFILE "/tmp/signald_link_purple_qrcode.png"
#define SIGNALD_QRCREATE_CMD "qrencode -s 6 -o " SIGNALD_TMP_QRFILE " '%s'"
#define SIGNALD_QR_MSG "echo Link by scanning QR with Signal App"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

#define SIGNALD_UNKNOWN_SOURCE_NUMBER "unknown"

#define SIGNALD_ERR_NONEXISTUSER "Attempted to connect to a non-existant user"
#define SIGNALD_ERR_AUTHFAILED   "Authorization failed"

#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS "external-attachments"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR "external-attachments-dir"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL "external-attachments-url"

typedef struct {
    char *name;

    GHashTable *transitions;
} SignaldState;

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;

    gboolean legacy_protocol;

    int fd;
    guint watcher;
    SignaldState *current;

    // Maps signal group IDs to libpurple PurpleConversation objects that represent those chats.
    GHashTable *groups;
} SignaldAccount;

typedef gboolean (*SignaldTransitionCb)(SignaldAccount *, JsonObject *obj);

typedef struct {
    SignaldTransitionCb filter;
    SignaldTransitionCb handler;
    SignaldTransitionCb next_message;

    SignaldState *next;
} SignaldStateTransition;

void
signald_subscribe (SignaldAccount *sa);

int
signald_strequalprefix (const char *s1, const char *s2);

#include "state.h"
#include "message.h"
#include "direct.h"
#include "groups.h"
#include "contacts.h"
#include "link.h"
#include "comms.h"

#endif
