#include <gmodule.h>
#include "libsignald.h"

SignaldState *entrypoint = NULL;
SignaldState *current = NULL;

void
signald_add_transition(SignaldState *prev, char *received, SignaldState *state, SignaldTransitionCb handler, SignaldTransitionCb next)
{
    SignaldStateTransition *transition = g_new0(SignaldStateTransition, 1);

    transition->handler = handler;
    transition->next_message = next;
    transition->next = state;

    g_hash_table_insert(prev->transitions, received, transition);
}

SignaldState *
signald_new_state(SignaldState *prev, char *name, char *received, SignaldTransitionCb handler, SignaldTransitionCb next)
{
    SignaldState *state = g_new0(SignaldState, 1);

    state->name = name;
    state->transitions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (prev != NULL) {
        signald_add_transition(prev, received, state, handler, next);
    }

    return state;
}

gboolean
signald_received_version(SignaldAccount *sa, JsonObject *obj)
{
    obj = json_object_get_object_member(obj, "data");
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald version: %s\n", json_object_get_string_member(obj, "version"));

    return TRUE;
}

gboolean
signald_check_proto_version(SignaldAccount *sa, JsonObject *obj)
{
    //
    // This is a bit of a hack!  We want to detect if we're dealing with a
    // new version of signald with the new protocol, or the old version.
    //
    // To test, we call get_user on the user account, using the new
    // JsonAddress form of the call.  If it succeeds, we're dealing with
    // a new signald.  If it fails, it's the old one.
    //

    const gchar *username = purple_account_get_username(sa->account);
    JsonObject *address = json_object_new();

    json_object_set_string_member(address, "number", username);

    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "get_user");
    json_object_set_string_member(data, "username", username);
    json_object_set_object_member(data, "recipientAddress", address);

    if (!signald_send_json (sa, data)) {
        //purple_connection_set_state(pc, PURPLE_DISCONNECTED);
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));

        json_object_unref(data);
        return FALSE;
    }

    json_object_unref(data);

    return TRUE;
}

gboolean
signald_subscribed(SignaldAccount *sa, JsonObject *obj)
{
    purple_connection_set_state(sa->pc, PURPLE_CONNECTION_CONNECTED);

    return TRUE;
}

gboolean
signald_initialize_contacts(SignaldAccount *sa, JsonObject *obj)
{
    JsonObject *data = json_object_new();

    json_object_set_string_member(data, "type", "list_contacts");
    json_object_set_string_member(data, "username", purple_account_get_username(sa->account));

    if (!signald_send_json (sa, data)) {
        purple_connection_error (sa->pc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Could not write subscription message."));
    }

    json_object_unref(data);

    signald_assume_all_buddies_online(sa);

    return TRUE;
}

gboolean
signald_linking_uri(SignaldAccount *sa, JsonObject *obj)
{
    signald_parse_linking_uri(sa, obj);

    return TRUE;
}

gboolean
signald_linking_successful(SignaldAccount *sa, JsonObject *obj)
{
    signald_parse_linking_successful();

    return TRUE;
}

gboolean
signald_subscribe_after_link(SignaldAccount *sa, JsonObject *obj)
{
    signald_subscribe(sa);

    return TRUE;
}

gboolean
signald_linking_error(SignaldAccount *sa, JsonObject *obj)
{
    signald_parse_linking_error(sa, obj);

    purple_notify_close_with_handle (purple_notify_get_handle ());
    remove (SIGNALD_TMP_QRFILE);

    return TRUE;
}

gboolean
signald_new_protocol(SignaldAccount *sa, JsonObject *obj)
{
    sa->legacy_protocol = FALSE;

    return TRUE;
}

gboolean
signald_old_protocol(SignaldAccount *sa, JsonObject *obj)
{
    JsonObject *data = json_object_get_object_member(obj, "data");
    JsonObject *request = json_object_get_object_member(data, "request");
    const char *type = json_object_get_string_member(request, "type");

    if (purple_strequal(type, "get_user")) {
        sa->legacy_protocol = TRUE;

        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
signald_received_contact_list(SignaldAccount *sa, JsonObject *obj)
{
    signald_parse_contact_list(sa, json_object_get_array_member(obj, "data"));

    return TRUE;
}

gboolean
signald_get_groups(SignaldAccount *sa, JsonObject *obj)
{
    signald_request_group_list(sa);

    return TRUE;
}

gboolean
signald_received_group_list(SignaldAccount *sa, JsonObject *obj)
{
    obj = json_object_get_object_member(obj, "data");
    signald_parse_group_list(sa, json_object_get_array_member(obj, "groups"));

    return TRUE;
}

gboolean
signald_received_message(SignaldAccount *sa, JsonObject *obj)
{
    SignaldMessage msg;

    if (signald_parse_message(sa, json_object_get_object_member(obj, "data"), &msg)) {
        switch(msg.type) {
            case SIGNALD_MESSAGE_TYPE_DIRECT:
                signald_process_direct_message(sa, &msg);
                break;

            case SIGNALD_MESSAGE_TYPE_GROUP:
                signald_process_group_message(sa, &msg);
                break;
        }
    }

    return TRUE;
}

gboolean
signald_received_account_list(SignaldAccount *sa, JsonObject *obj)
{
    JsonObject *data = json_object_get_object_member(obj, "data");

    signald_parse_account_list(sa, json_object_get_array_member(data, "accounts"));

    return TRUE;
}

void
signald_init_state_machine()
{
    if (entrypoint != NULL) {
        current = entrypoint;
        return;
    }

    SignaldState *version;
    SignaldState *subscribed, *linking, *linked;
    SignaldState *user, *user_not_registered, *unexpected_error;
    SignaldState *contacts;
    SignaldState *running;

    /*
     * Initialization portion of the state machine.
     *
     * From "start" we can get the version and then go to linking or subscribing.
     *
     * At the end of both paths we end up in "subscribed".
     */
    entrypoint = signald_new_state(NULL, "start", NULL, NULL, NULL);

    version = signald_new_state(entrypoint, "got version, waiting", "version", signald_received_version, NULL);

    /*
     * Easy case, already subscribed.  Just get the message and move on to
     * checking the protocol version.
     */
    subscribed = signald_new_state(version, "subscribe successful, check protocol", "subscribed", signald_subscribed, signald_check_proto_version);

    /*
     * The fun case.  Request the linking URI, wait for success (or error),
     * then if successful, subscribe.
     */
    linking = signald_new_state(version, "got linking uri, waiting", "linking_uri", signald_linking_uri, NULL);
    linked = signald_new_state(linking, "linking successful, subscribing", "linking_successful", signald_linking_successful, signald_subscribe_after_link);
    signald_add_transition(linked, "subscribed", subscribed, signald_subscribed, signald_check_proto_version);

    signald_new_state(linking, "error linking", "linking_error", signald_linking_error, NULL);

    /*
     * Alright, we're subscribed and we've initiated messaging to check the
     * protocol version.
     *
     * What we get back determines the protocol version, and then we request
     * the contact list.
     */
    // If we get a user back, we have the new protocol
    user = signald_new_state(subscribed, "new protocol detected, get contacts", "user", signald_new_protocol, signald_initialize_contacts);
    // If we get no user back, we have the new protocol
    user_not_registered = signald_new_state(subscribed, "new protocol detected, get contacts", "user_not_registered", signald_new_protocol, signald_initialize_contacts);
    // If we get an error, we have the old protocol
    unexpected_error = signald_new_state(subscribed, "old protocol detected, get contacts", "unexpected_error", signald_old_protocol, signald_initialize_contacts);

    /*
     * Once the contact list comes back, we request the group list.
     */
    contacts = signald_new_state(user, "got contacts, get groups", "contact_list", signald_received_contact_list, signald_get_groups);
    signald_add_transition(user_not_registered, "contact_list", contacts, signald_received_contact_list, signald_get_groups);
    signald_add_transition(unexpected_error, "contact_list", contacts, signald_received_contact_list, signald_get_groups);

    /*
     * Group list has arrived, transition to running state.
     */
    running = signald_new_state(contacts, "running", "group_list", signald_received_group_list, NULL);

    /*
     * Normal even loop.  We transition from "running" back to "running", with
     * various for the message types.
     */
    signald_add_transition(running, "message", running, signald_received_message, NULL);
    signald_add_transition(running, "group_created", running, signald_get_groups, NULL);
    signald_add_transition(running, "group_updated", running, signald_get_groups, NULL);
    signald_add_transition(running, "group_list", running, signald_received_group_list, NULL);
    signald_add_transition(running, "account_list", running, signald_received_account_list, NULL);

    current = entrypoint;
}

gboolean
signald_handle_message(SignaldAccount *sa, JsonObject *obj)
{
    if (current == NULL) {
        return FALSE;
    }

    const gchar *type = json_object_get_string_member(obj, "type");
    SignaldStateTransition *transition = g_hash_table_lookup(current->transitions, type);

    purple_debug_info(SIGNALD_PLUGIN_ID,
                       "In state '%s' received message type '%s'\n", 
                       current->name,
                       type);

    if (transition == NULL) {
        return FALSE;
    }

    if ((transition->handler != NULL) && ! transition->handler(sa, obj)) {
        return FALSE;
    }

    purple_debug_info(SIGNALD_PLUGIN_ID,
                      "Transitioned from '%s' to '%s'\n", 
                      current->name,
                      transition->next->name);

    if ((transition->next_message != NULL) && ! transition->next_message(sa, obj)) {
        purple_debug_info(SIGNALD_PLUGIN_ID, "Reached terminal state in state machine.\n");

        return FALSE;
    }

    current = transition->next;

    return TRUE;
}
