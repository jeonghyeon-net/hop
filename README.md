# hop

A tiny X11 daemon that switches workspaces with a single key (`XF86Tools` by default).

| Input | Action |
|---|---|
| Tap `Tools` | Go to the next workspace (wraps from last to first) |
| Hold `Tools` + `1`–`9`, `0` | Go to workspace 1–10 |

While `Tools` is held, the keyboard is grabbed, so the digits are not typed into the focused window. Switching is requested through EWMH `_NET_CURRENT_DESKTOP`, so it works with any EWMH-compliant window manager (tested on xfwm4).

## Requirements

- X11 (`libX11` headers), a C compiler, `pkg-config`
- A window manager with multiple workspaces, e.g. for xfwm4:

  ```sh
  xfconf-query -c xfwm4 -p /general/workspace_count -s 10
  ```

## Build & install

```sh
make
make install      # installs ~/.local/bin/hop and an XFCE autostart entry
```

Run it right away without logging out:

```sh
setsid -f ~/.local/bin/hop
```

Uninstall with `make uninstall`.
