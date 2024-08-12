const ws = new WebSocket("ws://localhost:3010");
ws.onopen = () => {
  console.log("ws opened on browser");
  ws.send("hello world");
};

ws.onmessage = (message) => {
  console.log(`message received`, message.data);

  if (typeof message.data === "string") {
    try {
      const data = JSON.parse(message.data);
      let x = data["/shot"].x;
      let y = data["/shot"].y;
      let name = data["/turn"].name;
      setCanvas(x, y, name);

      console.log(`x: ${x}, y: ${y}, name: ${name}`);
      console.log(data);
    } catch (e) {}
  }
};

const playerPositions = {};
const playerColors = {};
let currentPlayer = null;

function getRandomColor() {
  return "#" + Math.floor(Math.random() * 16777215).toString(16);
}

function setCanvas(x, y, player) {
  let canvas = document.getElementById("kolfMap");
  let ctx = canvas.getContext("2d");

  if (!playerColors[player]) {
    playerColors[player] = getRandomColor();
  }

  for (let p in playerPositions) {
    let pos = playerPositions[p];
    ctx.clearRect(pos.x - 1, pos.y - 1, 12, 12);
  }

  playerPositions[player] = { x, y };

  for (let p in playerPositions) {
    let pos = playerPositions[p];
    ctx.fillStyle = playerColors[p];
    ctx.fillRect(pos.x, pos.y, 10, 10);

    ctx.strokeStyle = "black";
    ctx.lineWidth = 2;
    ctx.strokeRect(pos.x - 1, pos.y - 1, 12, 12);
  }

  currentPlayer = player;

  let playerKey = document.getElementById("playerKey");
  playerKey.innerHTML = `Player: ${player} <br> Color: <span style="background-color: ${playerColors[player]}; padding: 2px 10px;"></span>`;
}
