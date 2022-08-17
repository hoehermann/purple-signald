## Getting Started

This document outlines how to get started with using purple-signald. 
These directions may change if signald changes. 
You can also check the [signald](https://gitlab.com/signald/signald) documentation.

### Linking with your Phone

1. Install [signald](https://gitlab.com/signald/signald).
1. `git clone --recurse-submodules git@github.com:hoehermann/purple-signald.git` (or alternatively `git clone git@github.com:hoehermann/purple-signald.git && git submodule init && git submodule update`)
2. Create and enter build directory: `mkdir -p purple-signald/build && cd purple-signald/build`
3. Generate, build and install: `cmake .. && make && sudo make install`
4. Add your user to the `signald` group: `sudo usermod -a -G signald $USER`
5. Logout and log-in again. Or restart your computer just to be sure. Alternatively, `su` to the current account again. The reason is that `adduser` does not change existing sessions, and *only within the su shell* you're in a new session, and you need to be in the `signald` group.
6. Restart pidgin
7. Add your a new account by selecting "signald" as the protocol. For the username, you *must* enter your full international telephone number formatted like `+12223334444`. Alternatively, you may enter your UUID.
8. Scan the generated QR code with signal on your phone to link your account. The dialog tells you where to find this option.
9. Chat someone up using your phone to verify it's working.

If you made a mistake, you can either delete the signal data directory as instructed, or use `signaldctl account delete +12223334444` on the command-line. On your main device, unlink the broken "device".

### Registering a stand-alone account

Setup is similar to linking. Please refer to the signald documentation for the details.

### Bitlbee

First setup your phone number and authorize it in Signald, see https://gitlab.com/signald/signald

Once that is successful, in the `&root` channel of Bitlbee, add the same phone number you authenticated via Signald:
```
account add hehoe-signald +12223334444
rename _12223334444 name-sig
account hehoe-signald set tag signal
account signal set auto-accept-invitations true
account signal set nick_format %full_name-sig
account signal on
```
To create a channel for Signal, auto join it and generally manage your contacts see - https://wiki.bitlbee.org/ManagingContactList
