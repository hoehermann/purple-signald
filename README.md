A libpurple/Pidgin plugin for [signald](https://gitlab.com/signald/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

An unofficial IRC channel exists on Libera.chat called `##purple-signald` for those who use it.

### Known Issues

**Attention:** As of September 2021, this plug-in is falling apart. I am annoyed by the number of protocol and API changes and I lack motivation keeping up with them.

If notifications for group chats are enabled, the colors of sender names will differ from those in the member list, see [#69](https://github.com/hoehermann/libpurple-signald/issues/69).

When used with Pidgin, the user must never close a group conversation window or incoming messages will get lost. Deferred messages (sent by others while offline) will always get lost.

### Getting Started

See [HOWTO](HOWTO.md).

### Features

* Core features:

  * Receive messages
  * Send messages

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

  * Support for group chats.
  * Fine-grained attachment handling (for bitlbee).

![Instant Message](/instant_message.png?raw=true "Instant Message Screenshot")

### Missing Features

* signald configuration (i.e. initial number registration)
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Messages with quotes

### Security Considerations

UUIDs are used for local file access. If someone manages to forge UUIDs, bypassing all checks in Signal and signald, the wrong local files might be accessed, but I do not see that happening realistically.
