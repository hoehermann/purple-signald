#ifndef __SIGNALD_SUBSCRIBE_H__
#define __SIGNALD_SUBSCRIBE_H__

void
signald_set_device_name (SignaldAccount *sa);

void
signald_scan_qrcode (SignaldAccount *sa);

void
signald_parse_linking_uri (SignaldAccount *sa, JsonObject *obj);

void
signald_parse_linking_successful (void);

void
signald_parse_linking_error (SignaldAccount *sa, JsonObject *obj);

void
signald_link_or_register (SignaldAccount *sa);

void
signald_parse_account_list (SignaldAccount *sa, JsonArray *data);

#endif
