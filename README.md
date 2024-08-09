# Kolf

## What's changed?
This fork adds some QOL features, such as:
- Automatic Saving during gameplay
- Stat Tracking

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

## Running the Server And building

### Building
Good luck... No seriously, ``craft`` KDE's build tool for Kolf sucks. I wish you best of luck in setting it up. Docs are [here](https://community.kde.org/Craft)

### Running the server
- ``npm i``
- ``node server/server.js``

## Extra Info
A sample json file containing the layout of the data can be found in ``server/format.json``<br>
The server itself can be found in ``server/server.js`` and can be ran with ``node server.js`` 
 
Modified by Cattn <br>
Originally by the KDE Team.
