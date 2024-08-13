# Kolf

## What's changed?
This fork adds some QOL features, such as:
- Automatic Saving during gameplay
- Stat Tracking
- Discord RPC

Stats are sent as json on port 3010 every shot.

Currently Tracked Stats:
- Name of the Players
- X cordinate of the shot
- Y cordinate of the shot
- aim-end-angle: Angle of the shot at mouse up
- Force: Force of the shot
- aim-time: Time taken to aim the shot
- timestamp: Time the shot was taken
- Score of The Hole (Including Water)

## How to play?
### Step 1: Download a binary
1. Locate a binary for you platform in the [Releases](https://github.com/Cattn/kolf/releases) tab<br>
2. Run the installer/binary!
3. Download The Server [here](https://github.com/Cattn/kolf-server/)
4. Run it! (Instructions are in the above repository.)
5. Observe your stats being posted to the console of the server!

## Running the Server And building

### Building
Good luck... No seriously, ``craft`` KDE's build tool for Kolf sucks. I wish you best of luck in setting it up. Docs are [here](https://community.kde.org/Craft)

Some general tips once craft is set up
- ``craft kolf``
- ``cs kolf``
- Now, delete all files in this folder, and clone the repository to this folder
- ``craft --compile --install --qmerge kolf``
- ``craft --run kolf``


### Running the server
- ``npm i``
- ``node server/server.js``

## Extra Info
A sample json file containing the layout of the data can be found in ``server/format.json``<br>
The server itself can be found in ``server/server.js`` and can be ran with ``node server.js`` 

Also, discord RPC will show the name of the players, as well as the current course name. An option to disable it will be provided later and this message will be updated 
 
Modified by Cattn <br>
Originally by the KDE Team.
