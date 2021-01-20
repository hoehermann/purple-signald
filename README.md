A libpurple/Pidgin plugin for [signald](https://git.callpipe.com/finn/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

I never wrote code for use in Pidgin before. EionRobb's [purple-discord](https://github.com/EionRobb/purple-discord) sources were of great help. 

Tested on Ubuntu 18.04. An unofficial IRC channel exists on Freenode called `##purple-signald` for those who use it.

Windows users may take a sneak peek at [purple-signal](https://github.com/hoehermann/purple-signal).

### Known Issues

There have been reports of incoming offline-messages getting lost. As far as I observed, they are not lost but delayed and delivered after a restart of signald.

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
    Note: For linking with the master device, `qrencode` needs to be installed.
  * Automatically start signald  
    Note: For automatically starting signald as a child proces, `signald` needs to be in `$PATH`.

* Additional features contributed by [Brett Kosinski](https://github.com/fancypantalons/):

  * Support for group chats.
  * Fine-grained attachment handling (for bitlbee).

![Instant Message](/instant_message.png?raw=true "Instant Message Screenshot")

### Bitlbee configuration
First setup your phone number and authorize it in Signald, see https://gitlab.com/signald/signald

Once that is successful, in the `&root` channel of Bitlbee, add the same phone number you authenticated via Signald:
```
account add hehoe-signald +12223334444
rename _12223334444 name-sig
account hehoe-signald on
```
To create a channel for Signal, auto join it and generally manage your contacts see - https://wiki.bitlbee.org/ManagingContactList

### Missing Features

* signald configuration (i.e. initial number registration)
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Messages with quotes
