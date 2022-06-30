#ifndef _SIGNALD_ATTACHMENTS_H_
#define _SIGNALD_ATTACHMENTS_H_

GString *
signald_prepare_attachments_message(SignaldAccount *sa, JsonObject *obj);

char *
signald_detach_images(const char *message, JsonArray *attachments);

gchar *
signald_write_external_attachment(SignaldAccount *sa, const char *filename, const char *mimetype_remote);

#endif
