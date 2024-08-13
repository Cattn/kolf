const ws = new WebSocket("ws://localhost:3010");
ws.onopen = () => {
  console.log("ws opened on browser");
  ws.send("hello world");
};

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

  drawBg(canvas);

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

  let playerDiv = document.getElementById("player");
  playerDiv.innerHTML = player;

  let currentHoleDiv = document.getElementById("currentHole");
  currentHoleDiv.innerHTML = `Hole: ${curHole}`;
}


function convertKolfMapToCanvas(kolfMap, course, curHole) {
  let holeConfigStart = "[" + curHole + "-hole@";
  let holeStartIndex = kolfMap.indexOf(holeConfigStart);


  if (holeStartIndex === -1) {
    console.log("Hole configuration not found!");
    return;
  }

  let configStartIndex = kolfMap.indexOf("\n", holeStartIndex) + 1;
  let configEndIndex = kolfMap.indexOf("[", configStartIndex);

  if (configEndIndex === -1) {
    configEndIndex = kolfMap.length;
  }


  // Config Handling
  let configString = kolfMap.substring(configStartIndex, configEndIndex).trim();
  const regex = /^(\w+)=([\w.-]+)$/gm;
  let match;
  let config = {};

  while ((match = regex.exec(configString)) !== null) {
    config[match[1]] = match[2];
  }

  let curHoleStats = document.getElementById("curHoleStats");
  curHoleStats.innerHTML = `<br> Course: ${course} <br> Hole: ${curHole} <br> Border Walls: ${config["borderWalls"]} <br> Par: ${config["par"]} <br> Max: ${config["maxstrokes"]}`;

  console.log("Configuration found for hole " + curHole + ":");
  console.log(config);

  // Wall Handling
  let walls = [];
  let wallConfigPrefix = "[" + curHole + "-wall@";
  let wallStartIndex = kolfMap.indexOf(wallConfigPrefix);
  
  while (wallStartIndex !== -1) {
    let wallConfigStartIndex = kolfMap.indexOf("\n", wallStartIndex) + 1;
    let wallConfigEndIndex = kolfMap.indexOf("[", wallConfigStartIndex);
    if (wallConfigEndIndex === -1) {
      wallConfigEndIndex = kolfMap.length;
    }
  
    let wallConfigString = kolfMap.substring(wallConfigStartIndex, wallConfigEndIndex).trim();
    const wallRegex = /^(\w+)=([\w.,-]+)$/gm;
    let wall = {};
    
    while ((match = wallRegex.exec(wallConfigString)) !== null) {
      if (match[1] === "startPoint") {
        const numregx = /([^,]+)/g;
        let nums = match[2].match(numregx);
        wall.start = {x: nums[0], y: nums[1]};
      } else if (match[1] === "endPoint") {
        const numregx = /([^,]+)/g;
        let nums = match[2].match(numregx);
        wall.end = {x: nums[0], y: nums[1]};
      }
    }
    
    walls.push(wall);
    
    wallStartIndex = kolfMap.indexOf(wallConfigPrefix, wallConfigEndIndex);
  }
  console.log("Walls configuration:");
  console.log(walls);


  let canvas = document.getElementById("kolfMap");
  let ctx = canvas.getContext("2d");

  walls.forEach(wall => {
    ctx.strokeStyle = "red";
    ctx.moveTo(wall.start.x, wall.start.y);
    ctx.lineTo(wall.end.x, wall.end.y);
    ctx.stroke();
  });
}

function drawBg(canvas) {
  let ctx = canvas.getContext("2d");
  ctx.globalCompositeOperation = 'destination-under';
  ctx.fillStyle = "green";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
}