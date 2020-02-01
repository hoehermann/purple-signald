# purple-signald

This is a forked version of the [signald plugin for pidgin](https://github.com/hoehermann/libpurple-signald). In contrast to the original version, this fork 

* does not require a new registration of the user (i.e. mobil number) but is based on linking to the signal app via QR code and
* starts the underlying [signald daemon](https://git.callpipe.com/finn/signald) by itself

Following software must be installed:

* [signald](https://git.callpipe.com/finn/signald)
* qrencode (on Debian/Ubuntu, just use  
  `sudo apt install qrencode`


##Original Readme

**This is the original readme file of the signald plugin.**

A libpurple/Pidgin plugin for [signald](https://git.callpipe.com/finn/signald) (signal, formerly textsecure).

signald is written by Finn Herzfeld.

I never wrote code for use in Pidgin before. EionRobb's [purple-discord](https://github.com/EionRobb/purple-discord) sources were of great help. 

Tested on Ubuntu 18.04.

### Known Issues

There have been reports of incoming offline-messages getting lost. As far as I observed, they are not lost but delayed and delivered after a restart of signald.

### Features

* Receive messages
* Send messages
* Receive files
* Receive images
* Send images
* Receive buddy list from server

Note: When signald is being run as a system service, downloaded files may not be accessible directly to the user. Do not forget to add yourself to the `signald` group.

![Instant Message](/instant_message.png?raw=true "Instant Message Screenshot")

### Missing Features

* signald configuration (i.e. initial number registration)
* Synchronizing messages sent from another device
* Deleting buddies from the server
* Updating contact details
* Contact colors
* Expiring messages
* Messages with quotes
* Proper group chats (right now you can send and receive group messages, but you cannot tell which one of the group members is answering)

![Group Chat](/groupchat.png?raw=true "Group Chat Screenshot")

