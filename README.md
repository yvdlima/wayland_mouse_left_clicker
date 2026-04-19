# Wayland Mouse Left Clicker

I wasn't able to find a good macro tool for Wayland which supported mouse clicks,
both from listening to extra buttons as for sending repeated commands. So I wrote
a small script to execute a macro I used often with Windows for my logitech mouse
( PRO X 2 )

If the user holds the BTN_EXTRA ( first extra button to the left side ), the script
will spam left clicks. Works by listening to events from `/dev/input` (requires using sudo) to trigger other
events into `uinput`. The libs `uinput` and `libinput` should already be setup with Wayland.

## Path of Exile macro

Requires [wl-clipboard](https://github.com/bugaevc/wl-clipboard)

PoE 1 craft system sucks, to have it suck less poe_click_macro.c uses the BTN_EXTRA to auto-left click, but, also takes a regex and automatically does a Ctrl+C. If the regex matches the clipboard contents the script stops, presuming the item was modified to the desired state and a different craft method will be used.

This works since PoE1 provides a full item description when doing a ctrl+c with the mouse above it, so the ideia is to click on a crafting orb, eg, Orb of Transmutation, hold shift + the mouse BTN_EXTRA, the script will start applying the orb until the item is modified to match the regex, making transmut spam less tedious and error prone.

Made a separate script for it because lazy.

## Compile and run

```shell
# left click macro
gcc left_click_macro.c -o left_click_macro

# Find the <path-to-input-device> with `sudo libinput list-devices` - note that this required libinput-tools
# sudo ./left_click_macro <path-to-input-device>
sudo ./left_click_macro /dev/input/event3

# poe click macro
gcc poe_click_macro.c -o poe_click_macro

# -E is used so sudo propagates environment variables XDG_RUNETIME_DIR and WAYLAND_DISPLAY.
# they are not required but avoids some warning/error messages
sudo -E ./poe_click_macro /dev/input/event3
gcc
```
