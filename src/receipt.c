#include "receipt.h"
#include "structs.h"
#include "comms.h"
#include "defines.h"
#include "message.h"
#include <json-glib/json-glib.h>

void signald_mark_read(SignaldAccount * sa, gint64 timestamp_micro, const char *uuid) {
    g_return_if_fail(uuid != NULL);
    if (purple_account_get_bool(sa->account, SIGNALD_OPTION_MARK_READ, FALSE)) {
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "type", "mark_read");
        json_object_set_string_member(data, "account", sa->uuid);
        signald_set_recipient(data, "to", uuid);
        JsonArray *timestamps = json_array_new();
        json_array_add_int_element(timestamps, timestamp_micro);
        json_object_set_array_member(data, "timestamps", timestamps);
        if (!signald_send_json(sa, data)) {
            purple_debug_error(SIGNALD_PLUGIN_ID, "Unable to send receipt for %ld.\n", timestamp_micro);
        }
        json_object_unref(data);
    }
}

void signald_mark_read_chat(SignaldAccount * sa, gint64 timestamp_micro, GHashTable *users) {
    GList * uuids = g_hash_table_get_keys(users);
    for (const GList *elem = uuids; elem != NULL; elem = elem->next) {
        signald_mark_read(sa, timestamp_micro, elem->data);
    }
    g_list_free(uuids);
}
