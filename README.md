# Kolf

Kolf is a miniature golf game from KDE. This branch is developing an integrated
online multiplayer mode alongside the existing local game.

For a private online service, set `KOLF_ONLINE_DEFAULT_ENDPOINT` to its `wss://`
or `ws://` address before launching Kolf. You may also set
`KOLF_ONLINE_DEFAULT_SERVER_NAME` for a friendly label. Kolf remembers the last
server that connected successfully and preselects it when Online is opened again.
Choose **Connect** to join it, or choose another recent server or edit the address.
**Change Server** returns to the Connect page from the connected entry, lobby,
game, or Results; **Leave Online** returns to the paused local game. Without a
configured or saved server, Online prompts for an address. Use remote
`ws://` addresses only on trusted networks.

## How to play?
### Download a binary
1. Locate a binary for you platform in the [Releases](https://github.com/Cattn/kolf/releases) tab<br>
2. Run the installer/binary!

## Building on Windows with KDE Craft

Set up Craft using the [KDE Windows development guide](https://community.kde.org/Get_Involved/development/Windows) with the Qt 6 / Visual Studio 2022 MSVC environment. Open a PowerShell session in Craft's Kolf source checkout and run:

```powershell
.\scripts\build-windows-craft.ps1
```

The helper installs Qt WebSockets, applies the Windows libkdegames audio shutdown fix, then builds and installs libkdegames and Kolf. Run it again after changing source. To launch Kolf, initialize Craft in the same PowerShell session as the run command:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --run kolf
```

Craft builds the source checkout associated with its Kolf blueprint. Use `cs kolf` in a Craft shell to locate that checkout; a clone elsewhere on disk is not automatically the source used by `craft kolf`. The upstream Kolf blueprint currently omits Qt WebSockets. libkdegames currently destroys its OpenAL runtime during Windows DLL unload, which can stall process exit; the helper ties that runtime to `QApplication` instead.
Modified by Cattn <br>
Originally by the KDE Team.
