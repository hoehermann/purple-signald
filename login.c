#include "libsignald.h"
#include "login.h"
#include "signald_procmgmt.h"
#include <sys/un.h> // for sockaddr_un
#include <sys/socket.h> // for socket and read
#include <errno.h>

void
signald_read_cb(gpointer data, gint source, PurpleInputCondition cond)
{
    SignaldAccount *sa = data;
    // this function essentially just reads bytes into a buffer until a newline is reached
    // using getline would be cool, but I do not want to find out what happens if I wrap this fd into a FILE* while the purple handle is connected to it
    const size_t BUFSIZE = 500000; // TODO: research actual maximum message size
    char buf[BUFSIZE];
    char *b = buf;
    gssize read = recv(sa->fd, b, 1, MSG_DONTWAIT);
    while (read > 0) {
        b += read;
        if(b[-1] == '\n') {
            *b = 0;
            purple_debug_info(SIGNALD_PLUGIN_ID, "got newline delimited message: %s", buf);
            signald_handle_input(sa, buf);
            // reset buffer
            *buf = 0;
            b = buf;
        }
        if (b-buf+1 == BUFSIZE) {
            purple_debug_error(SIGNALD_PLUGIN_ID, "message exceeded buffer size: %s\n", buf);
            b = buf;
            // NOTE: incomplete message may be passed to handler during next call
            return;
        }
        read = recv(sa->fd, b, 1, MSG_DONTWAIT);
    }
    if (read == 0) {
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Connection to signald lost."));
    }
    if (read < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            // assume the message is complete and was probably handled
        } else {
            // TODO: error out?
            purple_debug_error(SIGNALD_PLUGIN_ID, "recv error is %s\n",strerror(errno));
            return;
        }
    }
    if (*buf) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "left in buffer: %s\n", buf);
    }
}

gboolean sockaddr_from_path(struct sockaddr_un * address, const gchar * path) {
    memset(address, 0, sizeof(struct sockaddr_un));
    address->sun_family = AF_UNIX;
    if (strlen(path)-1 > sizeof address->sun_path) {
      purple_debug_error(SIGNALD_PLUGIN_ID, "socket path %s exceeds maximum length %lu!\n", path, sizeof address->sun_path); // TODO: show error in ui
      return FALSE;
    } else {
        strcpy(address->sun_path, path);
        return TRUE;
    }
}

typedef struct {
    SignaldAccount *sa;
    gchar *socket_path;
    gchar *message;
} SignaldConnection;

static gboolean
display_connection_error(void *data) {
    SignaldConnection *sc = data;
    purple_connection_error(sc->sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, sc->message);
    g_free(sc->message);
    g_free(sc);
    return FALSE;
}

static gboolean
display_debug_info(void *data) {
    SignaldConnection *sc = data;
    purple_debug_info(SIGNALD_PLUGIN_ID, "%s",sc->message);
    g_free(sc->message);
    g_free(sc);
    return FALSE;
}

static void *
do_try_connect(void * arg) {
    SignaldConnection * sc = arg;
    struct sockaddr_un address;
    if (sockaddr_from_path(&address, sc->socket_path))
     {
        // create a socket
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            sc->message = g_strdup_printf("Could not create socket: %s", strerror(errno));
            purple_timeout_add(0, display_connection_error, g_memdup2(sc, sizeof *sc));
        } else {
            int32_t err = -1;
            // connect our socket to signald socket
            for(int try = 1; try <= SIGNALD_TIMEOUT_SECONDS && err != 0 && sc->sa->fd == 0; try++) {
                err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
                sc->message = g_strdup_printf("Connecting to %s (try #%d)...\n", address.sun_path, try);
                purple_timeout_add(0, display_debug_info, g_memdup2(sc, sizeof *sc));
                sleep(1); // altogether wait SIGNALD_TIMEOUT_SECONDS
            }

            if (err == 0) {
                // successfully connected, tell purple to use our socket
                sc->message = g_strdup_printf("Connected to %s.\n", address.sun_path);
                purple_timeout_add(0, display_debug_info, g_memdup2(sc, sizeof *sc));
                sc->sa->fd = fd;
                sc->sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sc->sa);
            }
            if (sc->sa->fd == 0) {
                sc->message = g_strdup_printf("No connection to %s after %d tries.\n", address.sun_path, SIGNALD_TIMEOUT_SECONDS);
                purple_timeout_add(0, display_debug_info, g_memdup2(sc, sizeof *sc));
                sc->sa->socket_paths_count--;
                if (sc->sa->socket_paths_count == 0) {

                    sc->message = g_strdup("Unable to connect to any socket location.");
                    purple_timeout_add(0, display_connection_error, g_memdup2(sc, sizeof *sc));
                }
            }
        }
    }
    g_free(sc->socket_path);
    g_free(sc);
    return NULL;
}

static void
try_connect(SignaldAccount *sa, gchar *socket_path) {
        SignaldConnection *sc = g_new0(SignaldConnection, 1);
        sc->sa = sa;
        sc->socket_path = socket_path;
        pthread_t try_connect_thread;
        // TODO: handle error int err = 
        pthread_create(&try_connect_thread, NULL, do_try_connect, (void*)sc);
}

void
signald_connect_socket(SignaldAccount *sa) {
    purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTING);

    const gchar * user_socket_path = purple_account_get_string(sa->account, "socket", SIGNALD_DEFAULT_SOCKET);
    if (user_socket_path && user_socket_path[0]) {
        sa->socket_paths_count = 1;
        
        try_connect(sa, g_strdup(user_socket_path));
    } else {
        sa->socket_paths_count = 2;
        
        const gchar *xdg_runtime_dir = g_getenv("XDG_RUNTIME_DIR");
        gchar *xdg_socket_path = g_strdup_printf("%s/%s", xdg_runtime_dir, SIGNALD_GLOBAL_SOCKET_FILE);
        try_connect(sa, xdg_socket_path);
        
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
    pc_flags |= PURPLE_CONNECTION_NO_FONTSIZE;
    pc_flags |= PURPLE_CONNECTION_NO_BGCOLOR;
    pc_flags |= PURPLE_CONNECTION_ALLOW_CUSTOM_SMILEY;
    purple_connection_set_flags(pc, pc_flags);

    SignaldAccount *sa = g_new0(SignaldAccount, 1);

    purple_signal_connect(purple_blist_get_handle(),
                          "blist-node-aliased",
                          purple_connection_get_prpl(pc),
                          G_CALLBACK(signald_node_aliased),
                          pc);

    purple_connection_set_protocol_data(pc, sa);

    sa->account = account;
    sa->pc = pc;

    // Check account settings whether signald is globally running
    // (controlled by the system or the user) or whether it should
    // be controlled by the plugin.
    if (purple_account_get_bool(sa->account, "handle-signald", FALSE)) {
        signald_signald_start(sa->account);
    }

    signald_connect_socket(sa);

    // Initialize the container where we'll store our group mappings
    sa->groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}
