const express = require('express');
const app = express();
const htmlServed = express();
const PORT = 3010;
// const DiscordRPC = require('discord-rpc');
const WebSocket = require('ws');
const fs = require('node:fs');


let player = "Unknown Player";
let courseName = "Unknown Course";


app.use(express.json());

const httpServer = app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

const htmlServer = htmlServed.listen(8080, () => {
  console.log(`Server running on port 8080`);
});

const wss = new WebSocket.Server({ noServer: true });

htmlServed.get('/', function(req, res){
  res.sendFile(__dirname + '/index.html');
});

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

  setActivity(); 

  sendCourseToClients(receivedData);

  res.status(200).send('Data received');
});

app.post('/map', (req, res) => {
  const receivedData = req.body;
  console.log('Received map data:', receivedData);

  res.status(200).send('Data received');
});

app.post('/hole', (req, res) => {
  const receivedData = req.body;
  console.log('Received hole data:', receivedData);

  res.status(200).send('Data received');
});

// const clientId = '1271305657106305075';
// DiscordRPC.register(clientId);

// const rpc = new DiscordRPC.Client({ transport: 'ipc' });
// const startTimestamp = new Date();

async function setActivity() {
  // try {
  //   await rpc.setActivity({
  //     details: `Playing a Course`,
  //     state: `Current Player: ${player}`,
  //     startTimestamp,
  //     largeImageKey: 'mainkolf',
  //     largeImageText: courseName,
  //     instance: false,
  //   });

  //   console.log(`Activity set! Player: ${player}, Course: ${courseName}`);
  // } catch (error) {
  //   console.error('Error setting activity:', error);
  // }
}

// rpc.on('ready', () => {
//   console.log('Discord RPC Client is ready');
//   setActivity(); 
// });

// rpc.login({ clientId }).catch(console.error);

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

async function sendCourseToClients(receivedData) {
  let map;
try {
  let data = fs.readFileSync(receivedData['/shot'].map, 'utf8');
  map = {
    'map': data,
    'course': receivedData['/shot'].course || courseName,
    'hole': receivedData['/shot'].hole
  };
} catch (err) {
  console.error(err);
}
      
       if (receivedData['/shot']) {
        courseName = receivedData['/shot'].course || courseName;
        wss.clients.forEach(client => {
          if (client.readyState === WebSocket.OPEN) {
            client.send(JSON.stringify(receivedData));
            client.send(JSON.stringify(map));
          }
        });
      }
} 