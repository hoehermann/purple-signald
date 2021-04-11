## Getting Started

This document outlines how to get started with using purple-signald. 
These directions may change if signald changes. 
You can also check the [signald](https://gitlab.com/signald/signald) documentation.

### Linking with your Phone

1. Install [signald](https://gitlab.com/signald/signald). If using Debian (or Ubuntu etc), you might need to run `dpkg --add--architecture i386` first. Also install "qrencode". (Do *not* use `signaldctl`.)
3. `make && sudo make install`
4. `sudo adduser $USER signald`
4. Restart your computer. Alternatively, `su` to the current account again. The reason is that `adduser` does not change existing sessions, and *only within the su shell* you're in a new session, and you need to be in the `signald` group.
5. Restart pidgin
6. Add your a new account by selecting "signald" as the protocol. For the username, you *must* enter your full international telephone number formatted like `+12223334444`.
7. Scan the generated QR code with signal on your phone to link your account. The dialog tells you where to find this option.
8. Chat someone up using your phone to verify it's working.

If you made a mistake, you can either delete the signal data directory as instructed, or use `signaldctl account delete +123456789` on the command-line. unlink the broken "device" entry, and delete the account in pidgin before trying again.

### Registering a stand-alone account

Setup is similar to linking. Please refer to the signald documentation for the details.

### Bitlbee

First setup your phone number and authorize it in Signald, see https://gitlab.com/signald/signald

Once that is successful, in the `&root` channel of Bitlbee, add the same phone number you authenticated via Signald:
```
account add hehoe-signald +12223334444
rename _12223334444 name-sig
account hehoe-signald set tag signal
account signal set auto-join-group-chats true
account signal set nick_format %full_name-sig
account signal on
```
To create a channel for Signal, auto join it and generally manage your contacts see - https://wiki.bitlbee.org/ManagingContactList
