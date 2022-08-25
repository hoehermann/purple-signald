A libpurple/Pidgin plugin for [signald](https://gitlab.com/signald/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

An unofficial IRC channel exists on Libera.chat called `##purple-signald` for those who use it.

![Instant Message](/doc/instant_message.png?raw=true "Instant Message Screenshot")

### Known Issues

* Sometimes, group chats are added to the buddy list more than once.
* In group chats, on outgoing messages the sender name may have a different color than displayed in the list of chat participants.
* When sending an image with delayed acknowledgements, the image is not displayed locally.
* Sending out read receipts on group chats do not work util the list of participants has been loaded. This usually affects only the first message of a chat.
* Read receipts of messages sent to groups are displayed in the receivers' conversation – and only if the conversation is currently active.
* After linking, contacts are not synced and may appear offline. Reconnecting helps.

### Getting Started

See [HOWTO](HOWTO.md).

### Features

* Core features:

  * Receive messages
  * Send messages

* Additional features:

  * Emoji reactions are displayed
  * Stickers can be displayed if GDK headers were available at build-time and a [GDK webp pixbuf loader](https://github.com/aruiz/webp-pixbuf-loader) is present in the system at run-time. Stickers are not animated.
  * It is possible to leave a Signal group by leaving the Pidgin chat (close the window) after removing it from the Pidgin buddy list.

* Additional features contributed by [Hermann Kraus](https://github.com/herm/):

  * Receive files
  * Receive images
  * Send images
  * Receive buddy list from server

  Note: When signald is being run as a system service, downloaded files may not be accessible directly to the user. Do not forget to add yourself to the `signald` group.

* Additional features contributed by [Torsten](https://github.com/ttlmax/libpurple-signald):

  * Link with the master device
  * Automatically start signald  
    Note: For automatically starting signald as a child proces, `signald` needs to be in `$PATH`.
  * Group Avatars

* Additional features contributed by [Brett Kosinski](https://github.com/fancypantalons/):

  * Fine-grained attachment handling (for bitlbee).

### Missing Features

* signald configuration (i.e. initial number registration)
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Support for old-style groups

### Security Considerations

UUIDs are used for local file access. If someone manages to forge UUIDs, bypassing all checks in Signal and signald, the wrong local files might be accessed, but I do not see that happening realistically.
