# Wayland Mouse Left Clicker

I wasn't able to find a good macro tool for Wayland which supported mouse clicks,
both from listening to extra buttons as for sending repeated commands. So I wrote
a small script to execute a macro I used often with Windows for my logitech mouse
( PRO X 2 )

If the user holds the BTN_EXTRA ( first extra button to the left side ), the script
will spam left clicks. Works by listening to events from `libinput` to trigger other
events into `uinput`. The libs `uinput` and `libinput` should already be setup with Wayland.

## Install and run

```shell
git clone git@github.com:yvdlima/wayland_mouse_left_clicker.git
cd wayland_mouse_left_clicker
gcc src.c -o left_click_macro
# Find the <path-to-input-device> with `sudo libinput list-devices`
# sudo ./left_click_macro <path-to-input-device>
sudo ./left_click_macro /dev/input/event6
```
