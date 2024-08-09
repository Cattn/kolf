const express = require('express');
const app = express();
const PORT = 3010;
const DiscordRPC = require('discord-rpc');

let player = "Unknown Player";
let courseName = "Unknown Course";

app.use(express.json());

app.post('/turn', (req, res) => {
  const receivedData = req.body;
  console.log('Received turn data:', receivedData);

  if (receivedData['/turn']) {
    player = receivedData['/turn'].name || player;
  }

  setActivity(); 

  res.status(200).send('Data received');
});

app.post('/shot', (req, res) => {
  const receivedData = req.body;
  console.log('Received shot data:', receivedData);

  if (receivedData['/shot']) {
    courseName = receivedData['/shot'].hole || courseName;
  }

  setActivity(); 

  res.status(200).send('Data received');
});

app.post('/hole', (req, res) => {
  const receivedData = req.body;
  console.log('Received hole data:', receivedData);

  res.status(200).send('Data received');
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

const clientId = '1271305657106305075';
DiscordRPC.register(clientId);

const rpc = new DiscordRPC.Client({ transport: 'ipc' });
const startTimestamp = new Date();

async function setActivity() {
  try {
    await rpc.setActivity({
      details: `Playing a Course`,
      state: `Current Player: ${player}`,
      startTimestamp,
      largeImageKey: 'mainkolf',
      largeImageText: courseName,
      instance: false,
    });

    console.log(`Activity set! Player: ${player}, Course: ${courseName}`);
  } catch (error) {
    console.error('Error setting activity:', error);
  }
}

rpc.on('ready', () => {
  console.log('Discord RPC Client is ready');
  setActivity(); 
});

rpc.login({ clientId }).catch(console.error);
