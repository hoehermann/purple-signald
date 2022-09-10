#include "reply.h"
#include "defines.h"
#include "message.h"

GQueue * signald_replycache_init() {
    return g_queue_new();
}

static void signald_replycache_message_free(SignaldMessage *message) {
    g_return_if_fail(message != NULL);
    g_free(message->author_uuid);
    g_free(message->text);
    g_free(message);
}

void signald_replycache_free(GQueue *queue) {
    g_queue_free_full(queue, (GDestroyNotify)signald_replycache_message_free);
}

void signald_replycache_add_message(SignaldAccount *sa, PurpleConversation *conv, const char *author_uuid, guint64 timestamp_micro, const char *body) {
    if (body != NULL) {
        const int capacity = purple_account_get_int(sa->account, SIGNALD_OPTION_REPLY_CACHE, 0);
        if (capacity > 0) {
            SignaldMessage *msg = g_new0(SignaldMessage, 1);
            msg->conversation = conv;
            msg->author_uuid = g_strdup(author_uuid);
            msg->text = g_strdup(body);
            msg->id = timestamp_micro;
            g_queue_push_head(sa->replycache, msg);
        }
        while (g_queue_get_length(sa->replycache) > capacity) {
            g_queue_pop_tail(sa->replycache);
        }
    }
}

static int signald_replycache_predicate(SignaldMessage * msg, const char* needle) {
    return !(strstr(msg->text, needle) != NULL);
}

SignaldMessage * signald_replycache_check(SignaldAccount *sa, const gchar *message) {
    g_return_val_if_fail(message != NULL, NULL);
    if (message[0] == '@') {
        char * colon = strchr(message, ':');
        if (colon != NULL) {
            const size_t needle_len = colon-message-1;
            char * needle = g_new0(char, needle_len);
            strncpy(needle, message+1, needle_len);
            GList * elem = g_queue_find_custom(sa->replycache, needle, (GCompareFunc)signald_replycache_predicate);
            if (elem != NULL) {
                return (SignaldMessage *)elem->data;
            }
        }
    }
    return NULL;
}

const gchar * signald_replycache_strip_needle(const gchar * message) {
    // find separator
    message = strchr(message, ':') + 1;
    // strip leading spaces
    while (*message != 0 && *message == ' ') {
        message++;
    }
    return message;
}

void signald_replycache_apply(JsonObject *data, const SignaldMessage * msg) {
    g_return_if_fail(data != NULL);
    g_return_if_fail(msg != NULL);
    JsonObject *quote = json_object_new();
    json_object_set_int_member(quote, "id", msg->id);
    signald_set_recipient(quote, "author", msg->author_uuid);
    json_object_set_string_member(quote, "text", msg->text);
    json_object_set_object_member(data, "quote", quote);
}
