# CoD:UO Dedicated Engine

This directory contains maintained source for the Call of Duty: United Offensive
dedicated-server engine. It covers server-side runtime systems such as
commands, cvars, filesystem access, networking, collision, animation/model
services, scripting support, and game-module host calls.

The exported tree is organized as normal buildable C/C++ source:

- `src/server/standalone/`: process and dedicated-platform code.
- `src/server/engine/`: server engine code shared with an embedded listen
  server.
- common subsystems live directly below `src/`.
- `build-mk/server.mk`: compile and link targets for the maintained engine.

## Build

```sh
make -f build-mk/server.mk objects CODUO_FP_FAITHFUL=relaxed
make -f build-mk/server.mk shared-check-link CODUO_FP_FAITHFUL=relaxed
make windows-i386 WINDOWS_DEP_PREFIX=/opt/mingw-i686
make windows-i686 WINDOWS_DEP_PREFIX=/opt/mingw-i686
```

The default `check` target compiles the maintained source into a static archive.
The shared link check permits unresolved engine-internal symbols while more
subsystems are completed.

The original Linux server is an i386 program. Build targets that use `-m32`
require a Linux toolchain with 32-bit compiler and library support installed.

The Windows targets use 32-bit MinGW and produce the engine under
`build/server/windows-i386/` or `build/server/windows-i686/`. They require a matching
MinGW zlib; use `WINDOWS_DEP_PREFIX` when it is outside the compiler's default
search path. The executable is a console application and loads
`uo/uo_game_mp_x86.dll`.

## Game-type voting

The improved game module accepts a whitespace-separated `g_voteGameTypes`
allowlist. For example:

```text
set g_voteGameTypes "tdm dm ctf dom"
```

The list applies to both `callvote g_gametype` and `callvote typemap`, with
case-insensitive, whole-name matching. An empty value (the default) allows
every installed game type. Changes take effect without restarting the map.
The ordinary `g_allowVoteGameType` and `g_allowVoteTypeMap` switches still
control whether those vote commands are enabled. Direct administrator
commands and configured rotations are unaffected. Client menus can still
display installed game types that the server will reject for voting.

## Remote screenshots

The dedicated server can ask a connected client to capture and upload a
screenshot, for anti-cheat review. An operator triggers a capture on demand
from the server console or an rcon session:

```text
requestScreenshot <client-number-or-name>
```

The target client silently takes a screenshot (the same JPEG capture used by
the local `screenshotjpeg` client command) and uploads it back to the server
over the existing reliable command channel, in small base64-encoded chunks.
No dialog or notification is shown on the client.

Two cvars control the feature:

- `sv_allowRemoteScreenshot` (default `1`): master switch. Set to `0` to
  refuse both on-demand requests and automatic captures.
- `sv_autoScreenshotInterval` (default `0`, disabled): when set to a number
  of seconds, the server automatically requests a screenshot from every
  connected client on that interval, staggered across clients rather than
  all at once.

Uploaded screenshots are written under `screenshots/remote/` relative to the
server's `fs_homepath`, named `client<N>_<leveltime>.jpg`. An upload that
exceeds the server's size limit is kept under `screenshots/remote/pending/`
instead of being renamed into the reviewed set, so an operator can still
inspect it by hand. This feature is master-branch only; it adds no new
`sv_clientCommandHandlers` entries and does not touch the recovered cgame
module.
