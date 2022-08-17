#include <errno.h>
#include <sys/socket.h> // for socket and read
#include "purple_compat.h"
#include "structs.h"
#include "defines.h"
#include "comms.h"
#include "input.h"
#include <json-glib/json-glib.h>

/*
 * Implements the read callback.
 * Called when data has been sent by signald and is ready to be handled.
 */
void
signald_read_cb(gpointer data, gint source, PurpleInputCondition cond)
{
    SignaldAccount *sa = data;
    // this function essentially just reads bytes into a buffer until a newline is reached
    // using getline would be cool, but I do not want to find out what happens if I wrap this fd into a FILE* while the purple handle is connected to it
    const size_t BUFSIZE = 500000; // TODO: research actual maximum message size
    char buf[BUFSIZE];
    char *b = buf;
    gssize read = recv(sa->fd, b, 1, sa->readflags);
    while (read > 0) {
        b += read;
        if(b[-1] == '\n') {
            *b = 0;
            purple_debug_info(SIGNALD_PLUGIN_ID, "got newline delimited message: %s", buf);
            signald_parse_input(sa, buf);
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
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, "Connection to signald lost.");
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

gboolean
signald_send_str(SignaldAccount *sa, char *s)
{
    int l = strlen(s);
    int w = write(sa->fd, s, l);
    if (w != l) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "wrote %d, wanted %d, error is %s\n", w, l, strerror(errno));
        return 0;
    }
    return 1;
}

gboolean
signald_send_json(SignaldAccount *sa, JsonObject *data)
{
    // Set version to v1
    json_object_set_string_member(data, "version", "v1");

    gboolean success;
    char *json = json_object_to_string(data);
    purple_debug_info(SIGNALD_PLUGIN_ID, "Sending: %s\n", json);
    success = signald_send_str(sa, json);
    if (success) {
        success = signald_send_str(sa, "\n");
    }
    g_free(json);
    return success;
}

gboolean
signald_send_json_or_display_error(SignaldAccount *sa, JsonObject *data)
{
    if (!signald_send_json(sa, data)) {
        const gchar *type = json_object_get_string_member(data, "type");
        char *error_message = g_strdup_printf("Could not write %s message.", type);
        purple_connection_error(sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error_message);
        g_free(error_message);
    }
}

gchar *
json_object_to_string(JsonObject *obj)
{
    JsonNode *node;
    gchar *str;
    JsonGenerator *generator;

    node = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(node, obj);

    // a json string ...
    generator = json_generator_new();
    json_generator_set_root(generator, node);
    str = json_generator_to_data(generator, NULL);
    g_object_unref(generator);
    json_node_free(node);

    return str;
}
