#pragma once

#include <glib.h>
#include "structs.h"

void signald_receipts_init(SignaldAccount *sa);

void signald_receipts_destroy(SignaldAccount *sa);

void signald_mark_read(SignaldAccount *sa, gint64 timestamp_micro, const char *uuid);

void signald_mark_read_chat(SignaldAccount *sa, gint64 timestamp_micro, GHashTable *users);

void signald_process_receipt(SignaldAccount *sa, JsonObject *obj);
