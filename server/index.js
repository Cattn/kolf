const ws = new WebSocket("ws://localhost:3010");
ws.onopen = () => {
  console.log("ws opened on browser");
  ws.send("hello world");
};

let activeHole = null;

ws.onmessage = (message) => {
  console.log(`message received`, message.data, typeof message.data);

  if (typeof message.data === "string") {
    try {
      const data = JSON.parse(message.data);
      if (!data["/shot"]) {
        let map = data.map;
        let courseName = data.course;
        let hole = data.hole;

        convertKolfMapToCanvas(map, courseName, hole);

        console.log(`courseName: ${courseName}, hole: ${hole}`);
        return;
      }
      let x = data["/shot"].x;
      let y = data["/shot"].y;
      let name = data["/shot"].name;
      let hole = data["/shot"].hole;

      if (activeHole !== hole) {
        cleanCanvas(document.getElementById("kolfMap"));
      }

      setCanvas(x, y, name, hole);

      console.log(`x: ${x}, y: ${y}, name: ${name}`);
      console.log(data);
    } catch (e) {
    }
  }
};

const playerPositions = {};
const playerColors = {};
let currentPlayer = null;

function getRandomColor() {
  return "#" + Math.floor(Math.random() * 16777215).toString(16);
}

function setCanvas(x, y, player, curHole) {
  let canvas = document.getElementById("kolfMap");
  let ctx = canvas.getContext("2d");

  ctx.globalCompositeOperation = 'source-over';

  if (!playerColors[player]) {
    playerColors[player] = getRandomColor();
  }

  for (let p in playerPositions) {
    let pos = playerPositions[p];
    ctx.fillStyle = "green";
    ctx.fillRect(pos.x, pos.y, 10, 10);
    ctx.strokeStyle = "green";
    ctx.lineWidth = 2;
    ctx.strokeRect(pos.x - 1, pos.y - 1, 12, 12);
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

  let playerDiv = document.getElementById("player");
  playerDiv.innerHTML = player;

  let currentHoleDiv = document.getElementById("currentHole");
  currentHoleDiv.innerHTML = `Hole: ${curHole}`;
}

function convertKolfMapToCanvas(map, course, hole) {
  if (activeHole === hole) {
    return;
  }
  console.log(`Hole: ${hole}, Active Hole: ${activeHole}`);
  activeHole = hole;
  
  let walls = [];
  let wallConfig = `[${hole}-wall@`;
  let wallIndex = map.indexOf(wallConfig);

  console.log(`Checking wall configurations for hole: ${hole}`);
  
  let holeConfig = `[${hole}-hole@`;
  let holeIndex = map.indexOf(holeConfig);

  if (holeIndex === -1) {
    console.log("Hole configuration not found!");
    return;
  }

  let configStart = map.indexOf("\n", holeIndex) + 1;
  let configEnd = map.indexOf("[", configStart);

  if (configEnd === -1) {
    configEnd = map.length;
  }

  // Config Handling
  let configStr = map.substring(configStart, configEnd).trim();
  const regex = /^(\w+)=([\w.-]+)$/gm;
  let match;
  let config = {};

  while ((match = regex.exec(configStr)) !== null) {
    config[match[1]] = match[2];
  }

  let holeStats = document.getElementById("curHoleStats");
  holeStats.innerHTML = `<br> Course: ${course} <br> Hole: ${hole} <br> Border Walls: ${config["borderWalls"]} <br> Par: ${config["par"]} <br> Max: ${config["maxstrokes"]}`;

  console.log(`Configuration found for hole ${hole}:`, config);

  // Wall Handling
  while (wallIndex !== -1) {
    let wallConfigStart = map.indexOf("\n", wallIndex) + 1;
    let wallConfigEnd = map.indexOf("[", wallConfigStart);
    if (wallConfigEnd === -1) {
      wallConfigEnd = map.length;
    }

    let wallConfigStr = map.substring(wallConfigStart, wallConfigEnd).trim();
    const wallRegex = /^(\w+)=([\w.,-]+)$/gm;
    let wall = {};

    while ((match = wallRegex.exec(wallConfigStr)) !== null) {
      if (match[1] === "startPoint") {
        const numRegex = /([^,]+)/g;
        let nums = match[2].match(numRegex);
        wall.start = { x: parseFloat(nums[0]), y: parseFloat(nums[1]) };
      } else if (match[1] === "endPoint") {
        const numRegex = /([^,]+)/g;
        let nums = match[2].match(numRegex);
        wall.end = { x: parseFloat(nums[0]), y: parseFloat(nums[1]) };
      }
    }

    walls.push(wall);

    wallIndex = map.indexOf(wallConfig, wallConfigEnd);
  }

  console.log("Walls configuration:", walls);

  let canvas = document.getElementById("kolfMap");
  let ctx = canvas.getContext("2d");
  drawBg(canvas);

  ctx.globalCompositeOperation = 'source-over';

  if (walls.length === 0) {
    console.log("No walls found for this hole.");
  } else {
    walls.forEach(wall => {
      ctx.strokeStyle = "red";
      ctx.beginPath();
      ctx.moveTo(wall.start.x, wall.start.y);
      ctx.lineTo(wall.end.x, wall.end.y);
      ctx.stroke();
    });
  }
}

function drawBg(canvas) {
  let ctx = canvas.getContext("2d");
  ctx.globalCompositeOperation = 'destination-over';
  ctx.fillStyle = "green";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
}

function cleanCanvas(canvas) {
  let ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  drawBg(canvas);
}