#pragma once

#ifndef PLUGIN_VERSION
#error Must set PLUGIN_VERSION in Makefile
#endif
// https://github.com/LLNL/lbann/issues/117#issuecomment-334333286
#define MAKE_STR(x) _MAKE_STR(x)
#define _MAKE_STR(x) #x

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"
#define SIGNAL_DEFAULT_GROUP "Signal"

#define SIGNALD_DIALOG_TITLE "Signal Protocol"
#define SIGNALD_DIALOG_LINK "Link to Signal App"
#define SIGNALD_DEFAULT_DEVICENAME "Signal-Purple-Plugin" // must fit in HOST_NAME_MAX

#define SIGNALD_TIMEOUT_SECONDS 10
#define SIGNALD_GLOBAL_SOCKET_FILE  "signald/signald.sock"
#define SIGNALD_GLOBAL_SOCKET_PATH_VAR "/var/run"

#define SIGNALD_DATA_PATH "%s/signald"
#define SIGNALD_AVATARS_SIGNALD_DATA_PATH "/avatars/"
#define SIGNALD_AVATAR_FILE_NAME "contact-%s"
#define SIGNALD_PID_FILE SIGNALD_DATA_PATH "/pid"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_AWAY     "away"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS "external-attachments"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_DIR "external-attachments-dir"
#define SIGNALD_ACCOUNT_OPT_EXT_ATTACHMENTS_URL "external-attachments-url"

#define SIGNALD_OPTION_WAIT_SEND_ACKNOWLEDEMENT "wait-send-acknowledgement"
#define SIGNALD_OPTION_MARK_READ "mark-read"
#define SIGNALD_OPTION_DISPLAY_RECEIPTS "display-receipts"
#define SIGNALD_OPTION_REPLY_CACHE "reply-cache-capacity"
