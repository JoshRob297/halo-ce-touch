# A dedicated server

A copy of the game can be left running as a host: it hosts a list of maps on its own,
starts its games, returns to its lobby after each one, and the machines that join are told
what the game has left. Players get in with an invite link or find it in the server
browser.

There is no central server. This is one copy of the game holding a game open for others,
which is the automated system link host the port already has (`debug.network_test`,
`port/linux/NETCODE.md`), given a map list.

## Platforms

The same host, the same `config.toml` and the same netcode, on both platforms the game
builds for as a desktop executable:

| Platform | Build it | Run it |
| --- | --- | --- |
| Linux, x86 32-bit | `python configure.py` then `ninja linux`, or the CI's `dist/halo-linux-release/` | `./halo` |
| Windows, x86 32-bit | `ninja windows` (native, or `--windows-cross` from Linux), or the CI's `dist/halo-windows-release/` | `halo.exe` |

Both are the same game: a server started on Windows and one started on Linux hold the
same games, take the same clients and read the same settings, and a client of one platform
joins a server of the other. The settings' names, the map list's syntax, the log and the
invite link are identical; only the way each system keeps the process running differs
(below, "Keep it running").

It needs no screen, no GPU and no audio device: the renderer is null and the audio is
off, so a machine with neither a display nor a sound card runs it as it is. The Android
app and the web build are clients: the app can host a game while it runs, but it has no
headless mode, and a browser tab is closed with the page.

## What it needs

- A build of the game, of the platform the machine runs (above).
- The game data: a folder that contains `maps/`, as when playing.
- A `config.toml` beside the executable with the settings below. The game writes the file
  with its defaults at the first start, and a key that is wrong is written to the log and
  left at its default.
- A machine that stays on, and one UDP port for Internet play: set `network.tunnel_port`
  to a fixed one (2302 below), so that the firewall's rule and the router's forward name
  it; 0 (the default) lets the game pick a different port at each start. The game asks the
  router to forward it by itself (UPnP), so an ordinary connection works; **it is one port
  per machine**, so two copies of the game cannot both host Internet play on one
  machine.

## The settings

The same file on both platforms, in the sections the settings belong to:

```toml
[debug]
# a server has no screen: nothing is drawn, no window is made
null_renderer = true

# the map list it hosts, in the order to play it: entries of "map" or
# "map:variant,variant,...", ";" between them, and back to the first past the
# last. Map names are the ones the game's map lists show ("levels\test\wizard",
# and a Custom Edition map as "custom_maps\<name>", refer to
# docs/custom_edition_caches.md)
network_test = "host:wizard:slayer,ctf;chillout:slayer,ctf,oddball;sidewinder:ctf,slayer"

# the minutes every game of the list lasts, so a game nobody wins still ends
# and the list moves on
network_test_time = 15

# the seconds its lobby counts down to the start of a game. 0: the game's own,
# 31 seconds
network_test_countdown = 15

# how long the host waits for a countdown before starting a game by force, when
# nothing else starts it
network_test_start = 1.0

# false: the host takes no player of its own. Its games are the players' alone
# and it waits in its lobby for the first one. true (the default) is the
# automated test's host, which plays as a player of its own: every machine
# shows that player, and the games count it
network_test_player = false

[audio]
# no audio device is opened
enabled = false

[paths]
# the data root, when the maps are not beside the executable
# data = "/srv/halo"

[network]
# the name its listing shows in everyone's server browser (32 bytes at most in
# the browser; the in-game lobby shows 15 characters)
server_name = "Joces ESMX 24/7"

# the UDP port internet play uses: a fixed one, so the firewall rule and the
# router's forward name it (0 lets the game pick one at each start)
tunnel_port = 2302

[display]
# tell the players, in the chat area, how much of the game is left and the
# lobby count down. Each machine works it out from the game's options and its
# clock, so nothing new goes between them
time_notices = true
```

Each setting has an environment variable that overrides the file for one start; they are
in the settings table of `port/linux/README.md` (the same names on Windows) and explained
in `port/linux/NETCODE.md`. On Windows, set them in the shell that starts the server, or
in a `.cmd` wrapper:

```bat
set HALO_DATA_ROOT=C:\halo
set HALO_NULL_RENDERER=1
set HALO_NO_AUDIO=1
halo.exe
```

## Run it

Enter the executable, from the folder of the data:

```
cd /srv/halo
./halo
```

```
cd C:\halo
halo.exe
```

The game writes its invite link (a `halo://join/<64 hexadecimal digits>` line) to the
standard error at the start of every hosting, and logs the rest to the standard output and
to its own log.

## Keep it running

**Linux**, a unit that keeps it running and restarts it:

```ini
[Unit]
Description=Halo: Combat Evolved dedicated server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=halo
WorkingDirectory=/srv/halo
Environment=HALO_DATA_ROOT=/srv/halo
ExecStart=/srv/halo/halo
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Windows**, either in a console window that stays open (minimized is enough), or as a
service with a wrapper such as NSSM, or a task of Task Scheduler started at logon with
"Do not stop the task if it runs longer than" unchecked. The executable has the same
settings and the same null renderer, so nothing of it shows.

On both, keep the machine from sleeping (Linux: `systemd-inhibit` or mask
`sleep.target`; Windows: the power plan's sleep set to never, only for the server's
account).

## Open the port

```
# Linux (ufw)
ufw allow 2302/udp

# Windows (an elevated prompt)
netsh advfirewall firewall add rule name="Halo CE server" dir=in action=allow protocol=UDP localport=2302
```

## What it does, and what to expect

- **It lists itself** in everyone's server browser under `network.server_name`, and prints
  its invite link, which changes on every start.
- **It waits in its lobby** for the first player, with the next entry of its list set and
  pre-cached. The countdown starts when a player is in (one player with nobody else is
  enough), and the game begins exactly as the countdown reaches zero.
- **Every game it plays ends and the list moves on**, whether the score, the time limit or
  the last player leaving ends it. A game whose players have all left is closed fifteen
  seconds after the last one (a grace: a connection that comes back inside them keeps the
  game it was in), and a variant of the list that asks for teams, with fewer than two
  players in the game, plays as that map's first variant that does not.
- **It keeps its listing** while it operates, and comes back to its lobby after every game
  instead of dropping the game it holds.
- **An operator can watch it** without a screen: its state, and how much of the game it is
  running is left, go to the log every five seconds, together with the reasons a countdown
  is not running, the machines joining and leaving, and the messages between them
  (`debug.txt`, the engine's own log, and the standard output).

```
server: state 1 (pregame 0, ingame 1, postgame 2), map levels\test\wizard\wizard,
14 of 15 minutes left after 61 s, countdown active, 0 ms left;
machines 1, a player on each 1, teams 1, players 2 of 1, all pre-cached 1
```

## Who can join

A client joins a host whose network version is in its range
(`HALO_PORT_NETWORK_VERSION_MINIMUM` to `HALO_PORT_NETWORK_VERSION_MAXIMUM`): its own,
and those that differ from it only in what the other machine drops. In this build the
range is 31 to 32, so a client of 31 or 32 joins it, and one of 30 or older does not
(`port/linux/NETCODE.md`, "Versions"). The range is the client's: a host never checks a
client's version, so a server of this build takes a 31 client as well as a 32 one, and an
old client that only knows its own version refuses a newer host until it is updated. A
machine that joins in the middle of a game plays it from where it is, and a machine that
joins the lobby is counted for the next one.

## Notes

- The settings above belong to the automated system link host. With their defaults (no map
  list, no notices, the test's own player) nothing of this operates and a host the menus
  make is the one the game always had.
- One host per machine: the Internet play port is one, and so is the copy of the game the
  links of `halo://` open.
- The games are the game's own: a dedicated host does not change what a map or a gametype
  does, only which one comes next.
