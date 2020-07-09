#ifndef __SIGNALD_INIT_H__
#define __SIGNALD_INIT_H__

void
signald_init_state_machine();

gboolean
signald_handle_message(SignaldAccount *sa, JsonObject *obj);

#endif
