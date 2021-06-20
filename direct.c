#include "libsignald.h"

void
signald_process_direct_message(SignaldAccount *sa, SignaldMessage *msg)
{
    PurpleIMConversation *imconv = purple_conversations_find_im_with_account(msg->conversation_name, sa->account);

    PurpleMessageFlags flags = 0;
    GString *content = NULL;
    gboolean has_attachment = FALSE;

    if (signald_format_message(sa, msg, &content, &has_attachment)) {

        if (imconv == NULL) {
            // Open conversation if isn't already and if the message is not empty
            imconv = purple_im_conversation_new(sa->account, msg->conversation_name);
        }

        if (has_attachment) {
            flags |= PURPLE_MESSAGE_IMAGES;
        }

        if (msg->is_sync_message) {
            flags |= PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND | PURPLE_MESSAGE_DELAYED;
            purple_conv_im_write(imconv, msg->conversation_name, content->str, flags, msg->timestamp);
        } else {
            flags |= PURPLE_MESSAGE_RECV;
            purple_serv_got_im(sa->pc, msg->conversation_name, content->str, flags, msg->timestamp);
        }
    }
    g_string_free(content, TRUE);
}

int
signald_send_im(PurpleConnection *pc,
#if PURPLE_VERSION_CHECK(3, 0, 0)
                PurpleMessage *msg)
{
    const gchar *who = purple_message_get_recipient(msg);
    const gchar *message = purple_message_get_contents(msg);
#else
                const gchar *who, const gchar *message, PurpleMessageFlags flags)
{
#endif
    purple_debug_info(SIGNALD_PLUGIN_ID, "signald_send_im: flags: %x msg: »%s«\n", flags, message);

    if (purple_strequal(who, SIGNALD_UNKNOWN_SOURCE_NUMBER)) {
        return 0;
    }

    SignaldAccount *sa = purple_connection_get_protocol_data(pc);

    return signald_send_message(sa, SIGNALD_MESSAGE_TYPE_DIRECT, (char *)who, message);
}
