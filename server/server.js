const express = require('express');
const app = express();
const PORT = 3010;
const DiscordRPC = require('discord-rpc');
const WebSocket = require('ws');

let player = "Unknown Player";
let courseName = "Unknown Course";


app.use(express.json());

const httpServer = app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

const wss = new WebSocket.Server({ noServer: true });

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
    wss.clients.forEach(client => {
      if (client.readyState === WebSocket.OPEN) {
        client.send(JSON.stringify(receivedData));
      }
    });
  }

  setActivity(); 

  res.status(200).send('Data received');
});

app.post('/hole', (req, res) => {
  const receivedData = req.body;
  console.log('Received hole data:', receivedData);

  res.status(200).send('Data received');
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

// WS stuff

httpServer.on('upgrade', (req, socket, head) => {
  wss.handleUpgrade(req, socket, head, (ws) => {
    wss.emit('connection', ws, req);
  });
});

wss.on('connection', (ws) => {
  ws.on('message', (message) => {
    console.log('received: %s', message);
    ws.send(`Hello, you sent -> ${message}`);
  });

  ws.send('Hi there, I am a WebSocket server');
});