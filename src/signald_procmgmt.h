#pragma once

#include <purple.h>

#define SIGNALD_DEFAULT_SOCKET "/var/run/signald.sock"

void signald_save_pidfile (const char *pid_file_name);
void signald_kill_process (const char *pid_file_name);
void signald_connection_closed();
void signald_signald_start(PurpleAccount *account);
