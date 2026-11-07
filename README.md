# Kolf

Kolf is a miniature golf game from KDE. This fork adds online play with friends:
host a game, share its code or invite link, and play together on
[kolf.pro](https://kolf.pro) or your own server.

## Play

Download Kolf for Windows (installer) or Linux (Flatpak bundle) from the
[Releases](https://github.com/Cattn/kolf/releases) page. Each download has a
`.sha256` file to check it against.

```sh
flatpak install --user kolf-<version>-linux-x86_64.flatpak
flatpak run org.kde.kolf
```

Choose **Play Online** to connect to kolf.pro. Host a game and send the code or
the invite link, or enter a code a friend sent you. Other servers can be added
under **Settings → Configure Kolf → Servers**.

## Build

Kolf is a CMake project. It needs:

| Dependency | Version |
| --- | --- |
| CMake and Extra CMake Modules (ECM) | 3.16, 6.0 |
| Qt 6: Core, Gui, Widgets, Network, WebSockets, Test | 6.5 |
| KDE Frameworks 6: Completion, Config, ConfigWidgets, CoreAddons, Crash, DocTools, I18n, IconThemes, JobWidgets, KIO, TextWidgets, WidgetsAddons, XmlGui | 6.0 |
| libkdegames (KDEGames6) | 6.0 |

```sh
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
ctest --test-dir build
cmake --install build
```

`KOLF_DEFAULT_SERVER_URL` and `KOLF_DEFAULT_SERVER_NAME` change the built-in
server (default `wss://kolf.pro/ws`, `kolf.pro`). `KOLF_TEST_AUTOMATION=OFF`
leaves out the scripted test drivers.

### Linux with Flatpak

The quickest reproducible build uses the KDE Flatpak runtime, which provides
every dependency:

```sh
flatpak install --user flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
flatpak-builder --user --install --force-clean build-dir .flatpak-manifest.json
flatpak run org.kde.kolf
```

### Windows with KDE Craft

Set up Craft with the Qt 6 / Visual Studio 2022 environment from the
[KDE Windows development guide](https://community.kde.org/Get_Involved/development/Windows),
then, in Craft's Kolf source checkout (`cs kolf` in a Craft shell):

```powershell
.\scripts\build-windows-craft.ps1
& C:\CraftRoot\craft\craftenv.ps1
craft --run kolf
```

The helper installs Qt WebSockets (the upstream Kolf blueprint does not declare
it) and applies a libkdegames fix for a stall when Kolf exits on Windows, then
builds libkdegames and Kolf. `scripts/release-windows.ps1` packages an installer.

## Online server

The server lives in `server/` and needs Node.js 22.18 or newer:

```sh
cd server
npm ci
npm test
KOLF_PORT=3011 npm start
```

Point Kolf at it with **Settings → Configure Kolf → Servers → Add**
(`localhost` or a LAN address works without TLS). `deploy/README.md` covers
running a public server behind nginx, and `tests/multiplayer/README.md` lists
the server settings and the native test scenarios.

Modified by Cattn. Originally by the KDE team.
