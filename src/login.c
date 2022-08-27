#include <sys/un.h> // for sockaddr_un
#include <sys/socket.h> // for socket and read
#include <errno.h>
#include <purple.h>
#include "structs.h"
#include "defines.h"
#include "comms.h"
#include "signald_procmgmt.h"
#include "input.h"
#include "reply.h"
#include "receipt.h"
#include "purple-3/compat.h"

/*
 * This struct exchanges data between threads, see @try_connect.
 */
typedef struct {
    SignaldAccount *sa;
    gchar *socket_path;
    gchar *message;
} SignaldConnectionAttempt;

/*
 * See @execute_on_main_thread.
 */
static gboolean
display_connection_error(void *data) {
    SignaldConnectionAttempt *sc = data;
    purple_connection_error(sc->sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, sc->message);
    g_free(sc->message);
    g_free(sc);
    return FALSE;
}

/*
 * See @execute_on_main_thread.
 */
static gboolean
display_debug_info(void *data) {
    SignaldConnectionAttempt *sc = data;
    purple_debug_info(SIGNALD_PLUGIN_ID, "%s", sc->message);
    g_free(sc->message);
    g_free(sc);
    return FALSE;
}

/*
 * Every function writing to the GTK UI must be executed from the GTK main thread.
 * This function is a crutch for wrapping some purple functions:
 *
 * * purple_debug_info in display_debug_info
 * * purple_connection_error in display_connection_error
 *
 * Can only handle one message string instead of variardic arguments.
 */
static void
execute_on_main_thread(GSourceFunc function, SignaldConnectionAttempt *sc, gchar * message) {
    sc->message = message;
    purple_timeout_add(0, function, g_memdup2(sc, sizeof *sc));
}

/*
 * Tries to connect to a socket at a given location.
 * It is ought to be executed in a thread.
 * Only in case it does noes not succeed AND is the last thread to stop trying,
 * the situation is considered a connection failure.
 */
static void *
do_try_connect(void * arg) {
    SignaldConnectionAttempt * sc = arg;

    struct sockaddr_un address;
    if (strlen(sc->socket_path)-1 > sizeof address.sun_path) {
        execute_on_main_thread(display_connection_error, sc, g_strdup_printf("socket path %s exceeds maximum length %lu!\n", sc->socket_path, sizeof address.sun_path));
    } else {
        // convert path to sockaddr
        memset(&address, 0, sizeof(struct sockaddr_un));
        address.sun_family = AF_UNIX;
        strcpy(address.sun_path, sc->socket_path);

        // create a socket
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            execute_on_main_thread(display_connection_error, sc, g_strdup_printf("Could not create socket: %s", strerror(errno)));
        } else {
            int32_t err = -1;

            // connect our socket to signald socket
            for(int try = 1; try <= SIGNALD_TIMEOUT_SECONDS && err != 0 && sc->sa->fd < 0; try++) {
                err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
                execute_on_main_thread(display_debug_info, sc, g_strdup_printf("Connecting to %s (try #%d)...\n", address.sun_path, try));
                sleep(1); // altogether wait SIGNALD_TIMEOUT_SECONDS
            }

            if (err == 0) {
                // successfully connected, tell purple to use our socket
                execute_on_main_thread(display_debug_info, sc, g_strdup_printf("Connected to %s.\n", address.sun_path));
                sc->sa->fd = fd;
                sc->sa->readflags = MSG_DONTWAIT;
                sc->sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sc->sa);
            }
            if (sc->sa->fd < 0) {
                // no concurrent connection attempt has been successful by now
                execute_on_main_thread(display_debug_info, sc, g_strdup_printf("No connection to %s after %d tries.\n", address.sun_path, SIGNALD_TIMEOUT_SECONDS));

                sc->sa->socket_paths_count--; // this tread gives up trying
                // NOTE: although unlikely, it is possible that above decrement and other modifications or checks happen concurrently.
                // TODO: use a mutex where appropriate.
                if (sc->sa->socket_paths_count == 0) {
                    // no trying threads are remaining
                    execute_on_main_thread(display_connection_error, sc, sc->message = g_strdup("Unable to connect to any socket location."));
                }
            }
        }
    }
    g_free(sc->socket_path);
    g_free(sc);
    return NULL;
}

/*
 * Starts a connection attempt in background.
 */
static void
try_connect(SignaldAccount *sa, gchar *socket_path) {
        SignaldConnectionAttempt *sc = g_new0(SignaldConnectionAttempt, 1);
        sc->sa = sa;
        sc->socket_path = socket_path;
        pthread_t try_connect_thread;
        int err = pthread_create(&try_connect_thread, NULL, do_try_connect, (void*)sc);
        if (err == 0) {
            // detach thread so it is "free'd" as soon it terminates
            pthread_detach(try_connect_thread);
        } else {
            gchar *errmsg = g_strdup_printf("Could not create thread for connecting in background: %s", strerror(err));
            purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, errmsg);
            g_free(errmsg);
        }
}

/*
 * Connect to signald socket.
 * Tries multiple possible default socket location at once in background.
 * In case the user has explicitly defined a socket location, only that one is considered.
 */
// TODO: find out how purple does connections in the gevent loop. use that instead of explicit sockets and threads.
void
signald_connect_socket(SignaldAccount *sa) {
    purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTING);
    sa->fd = -1; // socket is not connected, no valid value for fd, yet
    sa->socket_paths_count = 1; // there is one path to try to connect to

    const gchar * user_socket_path = purple_account_get_string(sa->account, "socket", "");
    if (user_socket_path && user_socket_path[0]) {
        try_connect(sa, g_strdup(user_socket_path));
    } else {
        const gchar *xdg_runtime_dir = g_getenv("XDG_RUNTIME_DIR");
        if (xdg_runtime_dir) {
            sa->socket_paths_count++; // there is another path to try to connect to
            gchar *xdg_socket_path = g_strdup_printf("%s/%s", xdg_runtime_dir, SIGNALD_GLOBAL_SOCKET_FILE);
            try_connect(sa, xdg_socket_path);
        } else {
            purple_debug_warning(SIGNALD_PLUGIN_ID, "Unable to read environment variable XDG_RUNTIME_DIR. Skipping the related socket location.\n");
        }

        gchar * var_socket_path = g_strdup_printf("%s/%s", SIGNALD_GLOBAL_SOCKET_PATH_VAR, SIGNALD_GLOBAL_SOCKET_FILE);
        try_connect(sa, var_socket_path);
    }
}

void
signald_login(PurpleAccount *account)
{
    PurpleConnection *pc = purple_account_get_connection(account);

    // this protocol does not support anything special right now
    PurpleConnectionFlags pc_flags;
    pc_flags = purple_connection_get_flags(pc);
    pc_flags |= PURPLE_CONNECTION_FLAG_NO_FONTSIZE;
    pc_flags |= PURPLE_CONNECTION_FLAG_NO_BGCOLOR;
    purple_connection_set_flags(pc, pc_flags);

    SignaldAccount *sa = g_new0(SignaldAccount, 1);

    purple_connection_set_protocol_data(pc, sa);

    sa->account = account;
    sa->pc = pc;
    sa->input_buffer_position = sa->input_buffer;
    
    sa->replycache = signald_replycache_init();
    signald_receipts_init(sa);

    // Check account settings whether signald is globally running
    // (controlled by the system or the user) or whether it should
    // be controlled by the plugin.
    if (purple_account_get_bool(sa->account, "handle-signald", FALSE)) {
        signald_signald_start(sa->account);
    }

    signald_connect_socket(sa);
}

void signald_subscribe(SignaldAccount *sa)
{
    // subscribe to the configured number
    JsonObject *data = json_object_new();
    json_object_set_string_member(data, "type", "subscribe");
    // TODO: subscribe with uuid
    json_object_set_string_member(data, "account", purple_account_get_username(sa->account));
    signald_send_json_or_display_error(sa, data);
    json_object_unref(data);
}

void signald_close (PurpleConnection *pc) {
    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    // stop sending receipts
    signald_receipts_destroy(sa);
    
    // free reply cache
    signald_replycache_free(sa->replycache);

    // remove input watcher
    purple_input_remove(sa->watcher);
    sa->watcher = 0;

    if (sa->uuid) {
        // own UUID is kown, unsubscribe account
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "unsubscribe");
        json_object_set_string_member(data, "account", sa->uuid);
        if (purple_connection_get_state(pc) == PURPLE_CONNECTION_CONNECTED) { 
            if (signald_send_json(sa, data)) {
                // read one last time for acknowledgement of unsubscription
                // NOTE: this will block forever in case signald stalls
                sa->readflags = 0;
                signald_read_cb(sa, 0, 0);
            } else {
                purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Could not write message for unsubscribing.");
                purple_debug_error(SIGNALD_PLUGIN_ID, "Could not write message for unsubscribing: %s", strerror(errno));
            }
        }
        json_object_unref(data);
        // now free UUID
        g_free(sa->uuid);
        sa->uuid = NULL;
    }

    close(sa->fd);
    sa->fd = 0;

    g_free(sa);

    signald_connection_closed();
}
