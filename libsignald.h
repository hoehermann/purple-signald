/*
 *   signald plugin for libpurple
 *   Copyright (C) 2016 hermann Höhne
 *   Copyright (C) 2020 Torsten Lilge
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma GCC diagnostic pop

#define SIGNALD_PLUGIN_ID "prpl-hehoe-signald"
#ifndef SIGNALD_PLUGIN_VERSION
#error Must set SIGNALD_PLUGIN_VERSION in Makefile
#endif
#define SIGNALD_PLUGIN_WEBSITE "https://github.com/hoehermann/libpurple-signald"
#define SIGNAL_DEFAULT_GROUP "Signal"

#define SIGNALD_DIALOG_TITLE "Signal Protocol"
#define SIGNALD_DIALOG_LINK "Link to Signal App"

#define SIGNALD_TIME_OUT 6
#define SIGNALD_DEFAULT_SOCKET "/var/run/signald/signald.sock"
#define SIGNALD_DATA_PATH "%s/plugins/signald"
#define SIGNALD_DATA_FILE SIGNALD_DATA_PATH "/data/%s"
#define SIGNALD_PID_FILE SIGNALD_DATA_PATH "/pid"
#define SIGNALD_START "signald -s " SIGNALD_DEFAULT_SOCKET " -d " SIGNALD_DATA_PATH " &"

#define SIGNALD_TMP_QRFILE "/tmp/signald_link_purple_qrcode.png"
#define SIGNALD_TMP_QRFILE_URI "file://"SIGNALD_TMP_QRFILE
#define SIGNALD_PID_FILE_QR SIGNALD_DATA_PATH "/pidqr"
#define SIGNALD_QRCREATE_CMD "qrencode -s 6 -o " SIGNALD_TMP_QRFILE " '%s'"
#define SIGNALD_QRCREATE_MAXLEN 512
#define SIGNALD_QR_MSG "echo Link by scanning QR with Signal App"
#define SIGNALD_LINK_TYPE "linking_"

#define SIGNALD_STATUS_STR_ONLINE   "online"
#define SIGNALD_STATUS_STR_OFFLINE  "offline"
#define SIGNALD_STATUS_STR_MOBILE   "mobile"

#define SIGNALD_UNKNOWN_SOURCE_NUMBER "unknown"

typedef struct {
    PurpleAccount *account;
    PurpleConnection *pc;

    int fd;
    guint watcher;
} SignaldAccount;

static void
signald_add_purple_buddy (SignaldAccount *sa, const char *username, const char *alias);

gboolean signald_send_json (SignaldAccount *sa, JsonObject *data);

void signald_do_link_cb (gpointer data, int choice);

void signald_subscribe (SignaldAccount *sa);

void signald_save_pidfile (const char *pid_file_name);

void signald_kill_process (const char *pid_file_name);
