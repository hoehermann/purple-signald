A libpurple/Pidgin plugin for [signald](https://gitlab.com/signald/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

An unofficial IRC channel exists on Libera.chat called `##purple-signald` for those who use it.

![Instant Message](/doc/instant_message.png?raw=true "Instant Message Screenshot")

### Features

* Core features:

  * Receive messages
  * Send messages

* Additional features:

  * Emoji reactions are displayed
  * Stickers can be displayed if GDK headers were available at build-time and a [GDK webp pixbuf loader](https://github.com/aruiz/webp-pixbuf-loader) is present in the system at run-time. Stickers are not animated.
  * It is possible to leave a Signal group by leaving the Pidgin chat (close the window) after removing it from the Pidgin buddy list.
  * The plug-in can cache a user-defined number of incoming messages so you can reply to them by starting the message with "@needle:" (read "at character followed by a text followed by a colon"). The most recent cached message containing the needle will be replied to.  
  ![Reply](/doc/reply.png?raw=true "Screenshot showcasing reply feature")

* Additional features contributed by [Hermann Kraus](https://github.com/herm/):

  * Receive files
  * Receive images
  * Send images
  * Receive buddy list from server

  Note: When signald is being run as a system service, downloaded files may not be accessible directly to the user. Do not forget to add yourself to the `signald` group.

* Additional features contributed by [Torsten](https://github.com/ttlmax/):

  * Link with the master device
  * Automatically start signald  
    Note: For automatically starting signald as a child proces, `signald` needs to be in `$PATH`.
  * Group Avatars

* Additional features contributed by [Brett Kosinski](https://github.com/fancypantalons/):

  * Fine-grained attachment handling (for bitlbee).

### Known Issues

* Sometimes, group chats are added to the buddy list more than once.
* In group chats, on outgoing messages the sender name may have a different color than displayed in the list of chat participants.
* When using send acknowledgements, the text is displayed "as transmitted" rather than "as typed".
* Sending out read receipts on group chats do not work util the list of participants has been loaded. This usually affects only the first message of a chat.
* Read receipts of messages sent to groups are displayed in the receivers' conversation – and only if the conversation is currently active.
* After linking, contacts are not synced and may appear offline. Reconnecting helps.

These issues may or may not be worked on since they are hard to reproduce, non-trivial to resolve, and/or not a big problem. Open an issue if they impede your experience.

### Missing Features

* signald configuration
* Sending Mentions
* Registering a new number
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Support for old-style groups

### Security Considerations

UUIDs are used for local file access. If someone manages to forge UUIDs, bypassing all checks in Signal and signald, the wrong local files might be accessed, but I do not see that happening realistically.

### Building the plug-in

    sudo apt install libpurple-dev libjson-glib-dev
    git clone --recurse-submodules https://github.com/hoehermann/purple-signald.git purple-signald
    mkdir -p purple-signald/build
    cd purple-signald/build
    cmake ..
    make
    sudo make install
    
### Getting Started with signald

1. Install [signald](https://gitlab.com/signald/signald).
1. Add your user to the `signald` group: `sudo usermod -a -G signald $USER`
1. Logout and log-in again. Or restart your computer just to be sure. Alternatively, `su` to the current account again. The reason is that `adduser` does not change existing sessions, and *only within the su shell* you're in a new session, and you need to be in the `signald` group.
1. Restart Pidgin
1. Add your a new account by selecting "signald" as the protocol. For the username, you *must* enter your full international telephone number formatted like `+12223334444`. Alternatively, you may enter your UUID.
1. Scan the generated QR code with signal on your phone to link your account. The dialog tells you where to find this option.
1. Chat someone up using your phone to verify it's working.
1. In case it is not working, you can unlink the plug-in "device" via your main device. The plug-in will ask to scan the QR code again. In extreme cases, you may need to remove the account from purple and signald manually. The latter can be achieved via `signaldctl account delete +12223334444`.

### Working with Bitlbee

Note: Compatibility with Bitlbee has been provided by contributors. The main author does not offer direct support.

Setup the account first as described above. Once that is successful, in the `&root` channel of Bitlbee, add the same phone number you authenticated via Signald:
```
account add hehoe-signald +12223334444
rename _12223334444 name-sig
account hehoe-signald set tag signal
account signal set auto-accept-invitations true
account signal set nick_format %full_name-sig
account signal on
```
To create a channel for Signal, auto join it and generally manage your contacts see - https://wiki.bitlbee.org/ManagingContactList
