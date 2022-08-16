#include <errno.h>
#include <gmodule.h>
#include "purple_compat.h"
#include "structs.h"
#include "defines.h"
#include "comms.h"
#include <json-glib/json-glib.h>

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
