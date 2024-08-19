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

// Water Handling

let waters = [];
let waterConfig = `[${hole}-puddle@`;
let waterIndex = map.indexOf(waterConfig);

while (waterIndex !== -1) {
  let waterConfigEnd = map.indexOf("[", waterIndex + 1);
  if (waterConfigEnd === -1) {
    waterConfigEnd = map.length;
  }

  let waterConfigStr = map.substring(waterIndex, waterConfigEnd).trim();
  let lines = waterConfigStr.split("\n");
  let water = {};

  // Get the coordinates from the first line
  let coords = lines[0].match(/@(\d+),(\d+)/);
  if (coords !== null) {
    water.start = {
      x: parseInt(coords[1]),
      y: parseInt(coords[2])
    };
  }

  // Get the height and width from the subsequent lines
  for (let i = 1; i < lines.length; i++) {
    let match = lines[i].match(/height=(\d+)/);
    if (match !== null) {
      water.height = parseInt(match[1]);
    }

    match = lines[i].match(/width=(\d+)/);
    if (match !== null) {
      water.width = parseInt(match[1]);
    }
  }

  waters.push(water);

  waterIndex = map.indexOf(waterConfig, waterConfigEnd);
}


// Slope Handling

// Slope Handling

let slopes = [];
let slopeConfig = `[${hole}-slope@`;
let slopeIndex = map.indexOf(slopeConfig);

while (slopeIndex !== -1) {
  let slopeConfigEnd = map.indexOf("[", slopeIndex + 1);
  if (slopeConfigEnd === -1) {
    slopeConfigEnd = map.length;
  }

  let slopeConfigStr = map.substring(slopeIndex, slopeConfigEnd).trim();
  let lines = slopeConfigStr.split("\n");
  let slope = {};

  // Get the coordinates from the first line
  let coords = lines[0].match(/@(\d+),(\d+)/);
  if (coords !== null) {
    slope.start = {
      x: parseInt(coords[1]),
      y: parseInt(coords[2])
    };
  }

  // Get the slope data from the subsequent lines
  for (let i = 1; i < lines.length; i++) {
    let match = lines[i].match(/grade=(\d+)/);
    if (match !== null) {
      slope.grade = parseInt(match[1]);
    }

    match = lines[i].match(/gradient=(.*)/);
    if (match !== null) {
      slope.gradient = match[1];
    }

    match = lines[i].match(/height=(\d+)/);
    if (match !== null) {
      slope.height = parseInt(match[1]);
    }

    match = lines[i].match(/reversed=(true|false)/);
    if (match !== null) {
      slope.reversed = match[1] === "true";
    }

    match = lines[i].match(/stuckOnGround=(true|false)/);
    if (match !== null) {
      slope.stuckOnGround = match[1] === "true";
    }

    match = lines[i].match(/width=(\d+)/);
    if (match !== null) {
      slope.width = parseInt(match[1]);
    }
  }

  slopes.push(slope);

  slopeIndex = map.indexOf(slopeConfig, slopeConfigEnd);
}

  console.log("Walls configuration:", walls);
  console.log("Water configuration:", waters);
  console.log("Slope configuration:", slopes);

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

if (waters.length === 0) {
  console.log("No water found for this hole.");
} else {
  waters.forEach(water => {
    var grd = ctx.createLinearGradient(0, 0, 200, 0);
    grd.addColorStop(0, "blue");
    grd.addColorStop(1, "blue");

    ctx.fillStyle = grd;
    ctx.beginPath();
    ctx.ellipse(water.start.x, water.start.y, water.width / 2, water.height / 2, 0, 0, 2 * Math.PI);
    ctx.fill();
  });
}

if (slopes.length === 0) {
  console.log("No slopes found for this hole.");
} else {
  slopes.forEach(slope => {
    try {
    if (typeof slope.start.x === undefined || typeof slope.start.y === undefined) {
      slope.start.x = 0;
      slope.start.y = 0;
    }
    var grd;
    if (slope.gradient == "Horizontal") {
      grd = ctx.createLinearGradient(slope.start.x, slope.start.y, slope.start.x + slope.width, slope.start.y);
      drawArrow(ctx, slope.start.x + slope.width / 2, slope.start.y, slope.start.x + slope.width, slope.start.y);
    } else if (slope.gradient == "Vertical") {
      grd = ctx.createLinearGradient(slope.start.x, slope.start.y, slope.start.x, slope.start.y + slope.height);
      drawArrow(ctx, slope.start.x, slope.start.y + slope.height / 2, slope.start.x, slope.start.y + slope.height);
    } else if (slope.gradient == "Diagonal") {
      grd = ctx.createLinearGradient(slope.start.x, slope.start.y, slope.start.x + slope.width, slope.start.y + slope.height);
      drawArrow(ctx, slope.start.x + slope.width / 2, slope.start.y + slope.height / 2, slope.start.x + slope.width, slope.start.y + slope.height);
    } else if (slope.gradient == "Opposite Diagonal") {
      grd = ctx.createLinearGradient(slope.start.x, slope.start.y + slope.height, slope.start.x + slope.width, slope.start.y);
      drawArrow(ctx, slope.start.x + slope.width / 2, slope.start.y + slope.height / 2, slope.start.x + slope.width, slope.start.y);
    } else if (slope.gradient == "Elliptic") {
      grd = ctx.createRadialGradient(slope.start.x, slope.start.y, 0, slope.start.x, slope.start.y, slope.width);
      drawArrow(ctx, slope.start.x, slope.start.y + slope.height / 2, slope.start.x, slope.start.y);
      drawArrow(ctx, slope.start.x, slope.start.y - slope.height / 2, slope.start.x, slope.start.y); 
      drawArrow(ctx, slope.start.x + slope.width / 2, slope.start.y, slope.start.x, slope.start.y); 
      drawArrow(ctx, slope.start.x - slope.width / 2, slope.start.y, slope.start.x, slope.start.y);
    } else {
      console.log("Rewefwefwefal")
      grd = ctx.createLinearGradient(slope.start.x, slope.start.y, slope.start.x, slope.start.y + slope.height);
      drawArrow(ctx, slope.start.x, slope.start.y + slope.height / 2, slope.start.x, slope.start.y + slope.height);
    }
    grd.addColorStop(0, "red");
    grd.addColorStop(0.1, "orange");
    grd.addColorStop(0.5, "yellow");
    grd.addColorStop(1, "green");

    ctx.fillStyle = grd;
    ctx.beginPath();
    ctx.ellipse(slope.start.x, slope.start.y, slope.width / 2, slope.height / 2, 0, 0, 2 * Math.PI);
    ctx.fill();

  } catch (error) {
    console.error(error);
  }
  });
}

}

function drawArrow(ctx, x1, y1, x2, y2) {
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();

  var angle = Math.atan2(y2 - y1, x2 - x1);
  var arrowSize = 10;
  ctx.beginPath();
  ctx.moveTo(x2, y2);
  ctx.lineTo(x2 - arrowSize * Math.cos(angle - Math.PI / 6), y2 - arrowSize * Math.sin(angle - Math.PI / 6));
  ctx.lineTo(x2 - arrowSize * Math.cos(angle + Math.PI / 6), y2 - arrowSize * Math.sin(angle + Math.PI / 6));
  ctx.fill();
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