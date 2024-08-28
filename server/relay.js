// Relay games to server from client. 
const express = require('express');
const app = express();
const PORT = 3030;

app.use(express.json());

const httpServer = app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});

app.post('/turn', (req, res) => {
    const receivedData = req.body;
    console.log('Received Turn data:', receivedData);
    fetch('http://localhost:3010/turn', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(receivedData)
    })
  });

  const { Window } = require("node-screenshots");
  let windows = Window.all();
  const sharp = require('sharp');
  async function screenshotKolf(receivedData) {
    let course;
    for (const item of windows) {
      if (item.appName === 'kolf.exe') {
        try {
          const data = await item.captureImage();
          console.log(data);
          const png = await data.toPng();
          console.log(png);
          const webp = await sharp(png).webp().toBuffer();
          const course = 'data:image/webp;base64,' + webp.toString('base64');
          receivedData['/shot'].course = course;
          const response = await fetch('http://localhost:3010/shot', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json'
            },
            body: JSON.stringify(receivedData)
          });
          if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
          }
        } catch (error) {
          console.error('Error capturing and sending image:', error);
        }
      }
    }
    console.log('Received shot data:', receivedData);
    return course;
  }

  app.post('/shot', async function (req, res) {
    try {
    const receivedData = req.body;
    screenshotKolf(receivedData);
    res.status(200).send('Data received');
    } catch (e) {
      console.log(e)
    }
  });
  
  app.post('/map', (req, res) => {
    const receivedData = req.body;
    fetch('http://localhost:3010/map', {
        method: 'POST',
        headers: {
            'Content-Type': 'text/plain'
        },
        body: receivedData
    })
    console.log('Received map data:', receivedData);
  
    res.status(200).send('Data received');
  });
  
  app.post('/hole', (req, res) => {
    const receivedData = req.body;
    fetch('http://localhost:3010/hole', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(receivedData)
    })
    console.log('Received hole data:', receivedData);
  
    res.status(200).send('Data received');
  });