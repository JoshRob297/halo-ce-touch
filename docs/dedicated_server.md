# A dedicated server

A copy of the game can be left running as a host: it hosts a list of maps on its own,
starts its games, returns to its lobby after each one, and the machines that join are told
what the game has left. Players get in with an invite link or find it in the server
browser.

There is no central server. This is one copy of the game holding a game open for others,
which is the automated system link host the port already has (`debug.network_test`,
`port/linux/NETCODE.md`), given a map list.

## What it needs

- A build of the game: `python configure.py` and `ninja linux` (refer to `port/linux/README.md`,
  "Build the game"), or the portable release the CI publishes (`python tools/ci_build.py linux release`,
  `dist/halo-linux-release/`).
- The game data: a folder that contains `maps/`, as when playing.
- A `config.toml` next to the executable with the settings below. The game writes the file
  with its defaults at the first start, and a key that is wrong is written to the log and
  left at its default.
- A machine that stays on, and one UDP port for Internet play (`network.tunnel_port`, 2302
  by default). The game opens it through the router by itself, so an ordinary connection
  works; **it is one port per machine**, so two copies of the game cannot both host
  Internet play on one machine.

## The settings

```toml
# a server has no screen: nothing is drawn, no window is made
null_renderer = true

# the data root, when the maps are not beside the executable
# data = "/srv/halo"

# the name its listing shows in everyone's server browser
server_name = "Halo ES 24/7"

# the maps to host, in the order to play them: entries of "map" or
# "map:variant,variant,...", ";" between them, and back to the first past the
# last. Map names are the ones the game's map lists show ("levels\test\wizard",
# and a Custom Edition map as "custom_maps\<name>", refer to
# docs/custom_edition_caches.md)
network_test = "host:wizard:slayer,ctf;chillout:slayer,ctf,oddball;sidewinder:ctf,slayer"

# the minutes every game of the list lasts, so a game nobody wins still ends
# and the list moves on
network_test_time = 15

# the seconds its lobby counts down to the start of a game. Empty ("0"): the
# game's own, 31 seconds
network_test_countdown = 15

# how long the host waits for a countdown before starting a game by force, when
# nothing else starts it
network_test_start = 1.0

# false: the host takes no player of its own. Its games are the players' alone
# and it waits in its lobby for the first one. true (the default) is the
# automated test's host, which plays as a player of its own: every machine
# shows that player, and the games count it
network_test_player = false

# tell the players, in the chat area, how much of the game is left and the
# lobby count down. Each machine works it out from the game's options and its
# clock, so nothing new goes between them
time_notices = true
```

Each setting has an environment variable that overrides the file for one start; they are
in the settings table of `port/linux/README.md` and explained in `port/linux/NETCODE.md`.

## Run it

Enter the executable, from the folder of the data:

```
cd /srv/halo
./halo
```

The game writes its invite link (a `halo://join/<64 hexadecimal digits>` line) to the
standard error at the start of every hosting, and logs the rest to the standard output
and to its own log. A unit to keep it running:

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

Clients of the same network version (`HALO_PORT_NETWORK_VERSION`, 29 today) join it. A
build of an older version does not: the versions do not agree on what a machine says
(`port/linux/NETCODE.md`, "Versions"). A machine that joins in the middle of a game plays
it from where it is, and a machine that joins the lobby is counted for the next one.

## Notes

- The settings above belong to the automated system link host. With their defaults
  (no map list, no notices, the test's own player) nothing of this operates and a host the
  menus make is the one the game always had.
- One host per machine: the Internet play port is one, and so is the copy of the game the
  links of `halo://` open.
- The games are the game's own: a dedicated host does not change what a map or a gametype
  does, only which one comes next.
