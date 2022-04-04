#include "libsignald.h"
#include "login.h"
#include "signald_procmgmt.h"
#include <sys/un.h> // for sockaddr_un
#include <sys/socket.h> // for socket and read

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

void
signald_login(PurpleAccount *account)
{
    purple_debug_info(SIGNALD_PLUGIN_ID, "login\n");

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

    purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTING);
    // create a socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        purple_debug_error(SIGNALD_PLUGIN_ID, "socket() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not create socket."));
        return;
    }

    // connect our socket to signald socket
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;

    gchar *socket_file = NULL;
    gchar *xdg_socket_file;
    gchar *var_socket_file;

    xdg_socket_file = g_strdup_printf ("%s/%s",
                                       g_getenv (SIGNALD_GLOBAL_SOCKET_PATH_XDG),
                                       SIGNALD_GLOBAL_SOCKET_FILE);
    var_socket_file = g_strdup_printf ("%s/%s",
                                       SIGNALD_GLOBAL_SOCKET_PATH_VAR,
                                       SIGNALD_GLOBAL_SOCKET_FILE);

    const gchar * user_socket = purple_account_get_string(account, "socket", SIGNALD_DEFAULT_SOCKET);
    if (purple_strequal (user_socket, "")) {
        socket_file = g_strdup_printf ("%s", xdg_socket_file);
        purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", socket_file);
    } else {
        purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", user_socket);
        socket_file = g_strdup_printf ("%s", user_socket);
    }
    if (strlen(socket_file)-1 > sizeof address.sun_path) {
      purple_debug_error(
          SIGNALD_PLUGIN_ID, 
          "socket location %s exceeds maximum length %lu!\n", 
          socket_file, 
          sizeof address.sun_path
          );
      return;
    } else {
        strcpy(address.sun_path, socket_file);
    }

    // Try to connect but give signald some time (it was started in background)
    int32_t try = 0;
    int32_t err = -1;

    int32_t connecting = 3; // We have max. three socket locations to test

    while (connecting--) {

        try = 0;
        while ((err != 0) && (try <= SIGNALD_TIME_OUT)) {
            err = connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un));
            purple_debug_info(SIGNALD_PLUGIN_ID, "connecting ... %d s\n", try);
            try++;
            sleep (1);    // altogether wait SIGNALD_TIME_OUT seconds
        }

        if (err) {
            if (purple_strequal(socket_file, "")) {
                // socket is handled by pidgin => connection error
                connecting = 0;
            } else {
                if (connecting == 1) {
                    // only one location left, has to be var location
                    socket_file = g_strdup_printf ("%s", var_socket_file);  // var last
                } else if (connecting == 2) {
                    // first attempt to connect was not successful
                    if (purple_strequal (address.sun_path, xdg_socket_file)) {
                        // the first attempt already was with the xdg location 
                        connecting = 1;     // only var location left 
                        socket_file = g_strdup_printf ("%s", var_socket_file);
                    }
                    else if (purple_strequal (address.sun_path, var_socket_file)) {
                        // the first attempt already was with the var location 
                        connecting = 1;     // only xdg location left 
                        socket_file = g_strdup_printf ("%s", xdg_socket_file);
                    } else {
                        // it was another location, test both default locations
                        socket_file = g_strdup_printf ("%s", xdg_socket_file);
                    }
                }
                purple_debug_info(SIGNALD_PLUGIN_ID, "global socket location %s\n", address.sun_path);
            }

            if ((connecting > 0) &&
                (strlen(socket_file)-1 > sizeof address.sun_path)) {
                purple_debug_error(
                SIGNALD_PLUGIN_ID, 
                "socket location %s exceeds maximum length %lu!\n", 
                socket_file,
                sizeof address.sun_path
                );
                return;
            } else {
                strcpy(address.sun_path, socket_file);
            }
        }
    }

    g_free(socket_file);
    g_free (xdg_socket_file);
    g_free (var_socket_file);

    if (err) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "connect() error is %s\n", strerror(errno));
        purple_connection_error(pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not connect to socket."));
        return;
    }

    sa->fd = fd;
    sa->watcher = purple_input_add(fd, PURPLE_INPUT_READ, signald_read_cb, sa);

    // Initialize the container where we'll store our group mappings
    sa->groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}
