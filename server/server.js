const express = require('express');
const app = express();
const PORT = 3010;


app.use(express.json());

app.post('/turn', (req, res) => {
  const receivedData = req.body;
  console.log('Received turn data:', receivedData);
  res.status(200).send('Data received');
});

app.post('/dev', (req, res) => {
  const receivedData = req.body;
  console.log('Received dev data:', receivedData);
  res.status(200).send('Data received');
});

app.post('/shot', (req, res) => {
  const receivedData = req.body;
  console.log('Received shot data:', receivedData);
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