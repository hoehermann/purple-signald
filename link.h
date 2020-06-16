#ifndef __SIGNALD_SUBSCRIBE_H__
#define __SIGNALD_SUBSCRIBE_H__

void
signald_scan_qrcode (SignaldAccount *sa);

void
signald_parse_linking_uri (SignaldAccount *sa, JsonObject *obj);

void
signald_parse_linking_successful (SignaldAccount *sa, JsonObject *obj);

void
signald_parse_linking_error (SignaldAccount *sa, JsonObject *obj);

void
signald_link_or_register (SignaldAccount *sa);

#endif
