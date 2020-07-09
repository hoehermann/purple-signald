#ifndef __SIGNALD_INIT_H__
#define __SIGNALD_INIT_H__

typedef gboolean (*SignaldTransitionCb)(SignaldAccount *, JsonObject *obj);

typedef struct {
    char *name;

    GHashTable *transitions;
} SignaldState;

typedef struct {
    SignaldTransitionCb handler;
    SignaldTransitionCb next_message;

    SignaldState *next;
} SignaldStateTransition;

void
signald_init_state_machine();

gboolean
signald_handle_message(SignaldAccount *sa, JsonObject *obj);

#endif
