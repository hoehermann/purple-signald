#include "signald_procmgmt.h"

static int signald_usages = 0;

void
signald_save_pidfile (const char *pid_file_name)
{
    int pid = getpid ();
    FILE *pid_file = fopen (pid_file_name, "w");
    if (pid_file) {
        fprintf (pid_file, "%d\n", pid);
        fclose (pid_file);
    }
}

void
signald_kill_process (const char *pid_file_name)
{
    pid_t pid;
    FILE *pid_file = fopen (pid_file_name, "r");
    if (pid_file) {
        if (fscanf (pid_file, "%d\n", &pid)) {
            fclose (pid_file);
        } else {
            purple_debug_info(SIGNALD_PLUGIN_ID, "Failed to read signald pid from file.");
        }
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Failed to access signald pidfile.");
    }
    kill(pid, SIGTERM);
    remove(pid_file_name);
}

void
signald_signald_start(PurpleAccount *account)
{
    // Controlled by plugin.
    // We need to start signald if it not already running

    purple_debug_info (SIGNALD_PLUGIN_ID, "signald handled by plugin\n");

    if (0 == signald_usages) {
        purple_debug_info (SIGNALD_PLUGIN_ID, "starting signald\n");

        // Start signald daemon as forked process for killing it when closing
        int pid = fork();
        if (pid == 0) {
            // The child, redirect it to signald

            // Save pid for later killing the daemon
            gchar *pid_file = g_strdup_printf(SIGNALD_PID_FILE, purple_user_dir());
            signald_save_pidfile (pid_file);
            g_free(pid_file);

            // Start the daemon
            gchar *data = g_strdup_printf(SIGNALD_DATA_PATH, purple_user_dir());
            int signald_ok;
            const gchar * user_socket = purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET);

            if (purple_debug_is_enabled ()) {
                signald_ok = execlp("signald", "signald", "-v", "-s", user_socket, "-d", data, NULL);
            } else {
                signald_ok = execlp("signald", "signald", "-s", user_socket, "-d", data, NULL);
            }
            g_free(data);

            // Error starting the daemon? (execlp only returns on error)
            purple_debug_info (SIGNALD_PLUGIN_ID, "return code starting signald: %d\n", signald_ok);
            abort(); // Stop child
        }
      sleep (SIGNALD_TIME_OUT/2);     // Wait before trying to connect
    }

    signald_usages++;
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald used %d times\n", signald_usages);
}


void
signald_connection_closed() {
    // Kill signald daemon and remove its pid file if this was the last
    // account using the daemon. There is no need to check the option for
    // controlling signald again, since usage count is only greater 0 if
    // controlled by the plugin.
    if (signald_usages) {
        signald_usages--;
        if (0 == signald_usages) {
            // This was the last instance, kill daemon and remove pid file
            gchar *pid_file = g_strdup_printf(SIGNALD_PID_FILE, purple_user_dir());
            signald_kill_process(pid_file);
            g_free(pid_file);
        }
        purple_debug_info(SIGNALD_PLUGIN_ID, "signald used %d times after closing\n", signald_usages);
    }
}

