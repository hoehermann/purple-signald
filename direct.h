#ifndef __SIGNALD_DIRECT_H__
#define __SIGNALD_DIRECT_H__

int
signald_send_im(PurpleConnection *pc,
#if PURPLE_VERSION_CHECK(3, 0, 0)
                PurpleMessage *msg);
#else
                const gchar *who, const gchar *message, PurpleMessageFlags flags);
#endif

void
signald_process_direct_message(SignaldAccount *sa, SignaldMessage *msg);

#endif
