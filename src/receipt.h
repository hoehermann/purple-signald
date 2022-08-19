#pragma once

#include <glib.h>
#include "structs.h"

void signald_mark_read(SignaldAccount * sa, gint64 timestamp_micro, const char *uuid);

void signald_mark_read_chat(SignaldAccount * sa, gint64 timestamp_micro, GHashTable *users);
