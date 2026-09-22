# Kolf

Kolf is a miniature golf game from KDE. This branch is developing an integrated
online multiplayer mode alongside the existing local game.

## How to play?
### Download a binary
1. Locate a binary for you platform in the [Releases](https://github.com/Cattn/kolf/releases) tab<br>
2. Run the installer/binary!

## Building

### Building
Good luck... No seriously, ``craft`` KDE's build tool for Kolf sucks. I wish you best of luck in setting it up. Docs are [here](https://community.kde.org/Craft)

Some general tips once craft is set up
- ``craft kolf``
- ``cs kolf``
- Now, delete all files in this folder, and clone the repository to this folder
- ``craft --compile --install --qmerge kolf``
- ``craft --run kolf``
Modified by Cattn <br>
Originally by the KDE Team.
