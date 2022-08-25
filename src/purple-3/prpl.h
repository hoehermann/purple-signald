#pragma once

#include <purple.h>

#define SIGNALD_TYPE_PROTOCOL (signald_protocol_get_type())
G_DECLARE_FINAL_TYPE(SignaldProtocol, signald_protocol, SIGNALD, PROTOCOL, PurpleProtocol)
