const express = require('express');
const app = express();
const PORT = 3010;


app.use(express.json());

app.post('/turn', (req, res) => {
  const receivedData = req.body;
  console.log('Received data:', receivedData);
  res.status(200).send('Data received');
});

app.post('/dev', (req, res) => {
  const receivedData = req.body;
  console.log('Received dev data:', receivedData);
  res.status(200).send('Data received');
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});