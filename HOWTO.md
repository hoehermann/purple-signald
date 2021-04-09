1. Install [signald](https://gitlab.com/signald/signald). If using Debian (or Ubuntu etc), you might need to run `dpkg --add--architecture i386` first. Also install "qrencode". (Do *not* use `signaldctl`.)
3. `make && sudo make install`
4. `sudo adduser $USER signald`
4. Restart your computer. Alternatively, `su` to the current account again. The reason is that `adduser` does not change existing sessions, and *only within the su shell* you're in a new session, and you need to be in the `signald` group.
5. Restart pidgin
6. Add your a new account by selecting "signald" as the protocol. For the username, you *must* enter your full international telephone number formatted like `+12223334444`.
7. Scan the generated QR code with signal on your phone to link your account. The dialog tells you where to find this option.
8. Chat someone up using your phone to verify it's working.

If you made a mistake, you can either delete the signal data directory as instructed, or use `signaldctl account delete +123456789` on the command-line. unlink the broken "device" entry, and delete the account in pidgin before trying again.
