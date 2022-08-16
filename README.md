A libpurple/Pidgin plugin for [signald](https://gitlab.com/signald/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

An unofficial IRC channel exists on Libera.chat called `##purple-signald` for those who use it.

### Known Issues

* Sometimes, group chats are added to the buddy list more than once.
* In group chats, on outgoing messages the sender name may have a different color than displayed in the list of chat participants.

### Getting Started

See [HOWTO](HOWTO.md).

### Features

* Core features:

  * Receive messages
  * Send messages

* Additional features:

  * Emoji reactions are displayed
  * Stickers can be displayed if GDK headers were available at build-time and a [GDK webp pixbuf loader](https://github.com/aruiz/webp-pixbuf-loader) is present in the system at run-time. Stickers are not animated.

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

![Instant Message](/doc/instant_message.png?raw=true "Instant Message Screenshot")

### Missing Features

* signald configuration (i.e. initial number registration)
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Support for old-style groups

### Security Considerations

UUIDs are used for local file access. If someone manages to forge UUIDs, bypassing all checks in Signal and signald, the wrong local files might be accessed, but I do not see that happening realistically.
