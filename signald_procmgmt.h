#ifndef __SIGNALD_PROCMGMT_H__
#define __SIGNALD_PROCMGMT_H__

#include "libsignald.h"

#define SIGNALD_DEFAULT_SOCKET "/var/run/signald.sock"

void signald_save_pidfile (const char *pid_file_name);
void signald_kill_process (const char *pid_file_name);
void signald_connection_closed();
void signald_signald_start(PurpleAccount *account);

#endif
