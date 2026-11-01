import http from 'node:http';
import express from 'express';
import { WebSocketServer, WebSocket } from 'ws';
import {
  AKA_BIN_MAGIC,
  AKA_BIN_PACKET_SIZE,
  CAR_COUNT,
  DRIVERS,
  MAX_SPEED,
  SEG_LEN,
  TRACK_DEFS,
  buildRoadGeometry,
  type ClientCarTelemetry,
  type DevicePlatform,
  type NetCarSnapshot,
  type ParsedRoad,
  type RoomPhase,
  type RoomStatePayload,
  type RoomSummary,
} from './shared/raceProtocol.ts';

const PORT = Number(process.env.PORT) || 3000;

interface ServerCarState extends NetCarSnapshot {
  topFactor: number;
  skill: number;
  accelF: number;
  laneBias: number;
  targetX: number;
  lapStart: number;
  lastUpdateMs: number;
}

interface ClientConn {
  ws: WebSocket;
  clientId: string;
  roomCode: string;
  slot: number;
  binaryMode: boolean;
  pingMs: number;
}

interface RaceRoom {
  code: string;
  hostClientId: string;
  trackId: number;
  road: ParsedRoad;
  laps: number;
  difficulty: number;
  fillAi: boolean;
  phase: RoomPhase;
  phaseTime: number;
  raceTime: number;
  cars: ServerCarState[];
  clients: Map<string, ClientConn>;
  lastActiveMs: number;
}

const rooms = new Map<string, RaceRoom>();

function sanitizeCode(raw: string | undefined): string {
  const cleaned = (raw || 'AKA1')
    .toUpperCase()
    .replace(/[^A-Z0-9-]/g, '')
    .slice(0, 8);
  return cleaned || 'AKA1';
}

function sanitizeName(raw: string | undefined, fallback: string): string {
  const cleaned = (raw || fallback)
    .toUpperCase()
    .replace(/[^A-Z0-9 _-]/g, '')
    .trim()
    .slice(0, 8);
  return cleaned || fallback;
}

function createInitialCars(fillAi: boolean): ServerCarState[] {
  const cars: ServerCarState[] = [];
  for (let i = 0; i < CAR_COUNT; i++) {
    const row = Math.trunc(i / 2);
    const col = i % 2;
    const dist = -SEG_LEN * (3 + row * 6) - (col ? SEG_LEN * 2 : 0);
    const x = col ? 0.45 : -0.45;
    cars.push({
      slot: i,
      clientId: `AI-${i}`,
      name: DRIVERS[i].name,
      platform: 'AI',
      driverLook: i,
      ready: true,
      dist,
      x,
      speed: 0,
      steerVis: 0,
      braking: false,
      nitroOn: false,
      pitting: false,
      damage: 0,
      lapsDone: 0,
      bestLap: 0,
      finishTime: 0,
      finished: false,
      retired: false,
      place: i + 1,
      pingMs: 0,
      topFactor: 0.96 - i * 0.015,
      skill: Math.max(0.35, 0.9 - i * 0.06),
      accelF: 0.92,
      laneBias: ((i % 3) - 1) * 0.25,
      targetX: x,
      lapStart: 0,
      lastUpdateMs: Date.now(),
    });
    if (!fillAi) {
      cars[i].retired = true;
    }
  }
  return cars;
}

function getOrCreateRoom(rawCode: string): RaceRoom {
  const code = sanitizeCode(rawCode);
  let room = rooms.get(code);
  if (!room) {
    const road = buildRoadGeometry(0);
    room = {
      code,
      hostClientId: '',
      trackId: 0,
      road,
      laps: 3,
      difficulty: 1,
      fillAi: true,
      phase: 'LOBBY',
      phaseTime: 0,
      raceTime: 0,
      cars: createInitialCars(true),
      clients: new Map(),
      lastActiveMs: Date.now(),
    };
    rooms.set(code, room);
  }
  return room;
}

// Seed default public paddock room
getOrCreateRoom('AKA1');

function resetGridPositions(room: RaceRoom) {
  room.road = buildRoadGeometry(room.trackId);
  room.raceTime = 0;
  room.phaseTime = 0;

  const diffScale = [0.83, 0.91, 0.96, 1.01][room.difficulty] ?? 0.91;

  for (let i = 0; i < CAR_COUNT; i++) {
    const c = room.cars[i];
    const row = Math.trunc(i / 2);
    const col = i % 2;
    c.dist = -SEG_LEN * (3 + row * 6) - (col ? SEG_LEN * 2 : 0);
    c.x = col ? 0.45 : -0.45;
    c.targetX = c.x;
    c.speed = 0;
    c.steerVis = 0;
    c.braking = false;
    c.nitroOn = false;
    c.pitting = false;
    c.damage = 0;
    c.lapsDone = 0;
    c.bestLap = 0;
    c.finishTime = 0;
    c.finished = false;
    c.lapStart = 0;
    c.place = i + 1;
    if (c.platform === 'AI') {
      c.retired = !room.fillAi;
      c.topFactor = diffScale - (i * 0.011);
      c.skill = Math.max(0.3, 0.88 - i * 0.06);
      c.accelF = 0.88 + (CAR_COUNT - i) * 0.015;
    } else {
      c.retired = false;
    }
  }
}

function computeRankings(room: RaceRoom) {
  const active = [...room.cars].sort((a, b) => {
    if (a.platform === 'AI' && !room.fillAi && b.platform !== 'AI') return 1;
    if (b.platform === 'AI' && !room.fillAi && a.platform !== 'AI') return -1;
    if (a.finished !== b.finished) return a.finished ? -1 : 1;
    if (a.finished) return a.finishTime - b.finishTime;
    return b.dist - a.dist;
  });
  for (let k = 0; k < active.length; k++) {
    active[k].place = k + 1;
  }
}

function updateServerAiCar(room: RaceRoom, c: ServerCarState, dt: number) {
  if (c.retired) {
    c.speed = Math.max(0, c.speed - MAX_SPEED * 0.5 * dt);
    c.dist += c.speed * dt;
    return;
  }
  const n = room.road.segs.length;
  const wrapZ = ((c.dist % room.road.length) + room.road.length) % room.road.length;
  const baseIdx = Math.trunc(wrapZ / SEG_LEN) % n;

  // Look ahead for curves to modulate AI speed & racing line
  let maxCurve = 0;
  let curveSum = 0;
  const lookAhead = 18;
  for (let k = 1; k <= lookAhead; k++) {
    const cv = room.road.segs[(baseIdx + k) % n].curve;
    if (Math.abs(cv) > maxCurve) maxCurve = Math.abs(cv);
    curveSum += cv;
  }

  let desiredX = c.laneBias;
  if (Math.abs(curveSum) > 8) {
    desiredX = curveSum > 0 ? 0.52 * c.skill : -0.52 * c.skill; // apex cut
  }

  // Avoid cars directly ahead
  for (const other of room.cars) {
    if (other.slot === c.slot || (other.platform === 'AI' && !room.fillAi)) continue;
    let dz = other.dist - c.dist;
    if (dz > 0 && dz < SEG_LEN * 8 && Math.abs(other.x - c.x) < 0.42) {
      desiredX = other.x > 0 ? Math.max(-0.78, other.x - 0.55) : Math.min(0.78, other.x + 0.55);
    }
  }

  c.targetX += (desiredX - c.targetX) * Math.min(1, dt * 3.5);
  const dx = c.targetX - c.x;
  c.x += Math.max(-1.4 * dt, Math.min(1.4 * dt, dx * 2.6 * dt));
  c.steerVis = Math.max(-1, Math.min(1, dx * 2.2 - room.road.segs[baseIdx].curve * 0.09));

  const cornerFactor = maxCurve > 3.5 ? Math.max(0.66, 1 - (maxCurve - 3.0) * 0.065 * (1.25 - 0.5 * c.skill)) : 1;
  const top = MAX_SPEED * c.topFactor * cornerFactor;

  if (c.speed < top) {
    const pct = c.speed / MAX_SPEED;
    c.speed = Math.min(top, c.speed + (MAX_SPEED / 5) * c.accelF * dt * (1 - 0.45 * pct * pct));
    c.braking = false;
  } else {
    const prev = c.speed;
    c.speed = Math.max(top, c.speed - MAX_SPEED * 0.65 * dt);
    c.braking = prev - c.speed > MAX_SPEED * 0.025 * dt;
  }

  c.dist += c.speed * dt;
}

function updateCarLapProgress(room: RaceRoom, c: ServerCarState) {
  const done = c.dist >= 0 ? Math.trunc(c.dist / room.road.length) : 0;
  if (done > c.lapsDone) {
    c.lapsDone = done;
    const lapTime = room.raceTime - c.lapStart;
    c.lapStart = room.raceTime;
    if (lapTime > 3 && (c.bestLap === 0 || lapTime < c.bestLap)) {
      c.bestLap = lapTime;
    }
  }
  if (!c.finished && done >= room.laps) {
    c.finished = true;
    c.finishTime = room.raceTime;
  }
}

function buildRoomPayload(room: RaceRoom): RoomStatePayload {
  return {
    code: room.code,
    hostClientId: room.hostClientId,
    trackId: room.trackId,
    laps: room.laps,
    difficulty: room.difficulty,
    fillAi: room.fillAi,
    phase: room.phase,
    phaseTime: Number(room.phaseTime.toFixed(2)),
    raceTime: Number(room.raceTime.toFixed(2)),
    serverTimeMs: Date.now(),
    cars: room.cars.map((c) => ({
      slot: c.slot,
      clientId: c.clientId,
      name: c.name,
      platform: c.platform,
      driverLook: c.driverLook,
      ready: c.ready,
      dist: Math.round(c.dist * 10) / 10,
      x: Math.round(c.x * 1000) / 1000,
      speed: Math.round(c.speed),
      steerVis: Math.round(c.steerVis * 100) / 100,
      braking: c.braking,
      nitroOn: c.nitroOn,
      pitting: c.pitting,
      damage: Math.round(c.damage * 1000) / 1000,
      lapsDone: c.lapsDone,
      bestLap: Math.round(c.bestLap * 100) / 100,
      finishTime: Math.round(c.finishTime * 100) / 100,
      finished: c.finished,
      retired: c.retired,
      place: c.place,
      pingMs: c.pingMs,
    })),
  };
}

// 164-byte compact binary snapshot for Gamebuino AKA ESP32-S3 & PC SDL2 zero-malloc parsing
function buildBinarySnapshot(room: RaceRoom): Buffer {
  const buf = Buffer.alloc(4 + CAR_COUNT * AKA_BIN_PACKET_SIZE);
  const phaseMap: Record<RoomPhase, number> = { LOBBY: 0, COUNTDOWN: 1, RACING: 2, RESULTS: 3 };
  buf[0] = 0xa5;
  const lapsIdx = Math.max(0, [1, 2, 3, 5].indexOf(room.laps));
  buf[1] = (phaseMap[room.phase] & 0x03) | ((room.trackId & 0x0f) << 2) | ((lapsIdx & 0x03) << 6);   // bits 6-7: lap count index
  buf.writeUInt16LE(Math.min(65535, Math.round(room.raceTime * 100)), 2);

  for (let i = 0; i < CAR_COUNT; i++) {
    const c = room.cars[i];
    const off = 4 + i * AKA_BIN_PACKET_SIZE;
    let flags = 0;
    if (c.braking) flags |= 1;
    if (c.nitroOn) flags |= 2;
    if (c.pitting) flags |= 4;
    if (c.retired) flags |= 8;
    if (c.platform !== 'AI') flags |= 16;
    buf[off + 0] = AKA_BIN_MAGIC;
    buf[off + 1] = (i & 0x07) | (flags << 3);
    buf.writeFloatLE(c.dist, off + 2);
    buf.writeFloatLE(c.x, off + 6);
    buf.writeFloatLE(c.speed, off + 10);
    buf.writeInt16LE(Math.max(-1000, Math.min(1000, Math.round(c.steerVis * 1000))), off + 14);
    buf.writeUInt16LE(Math.max(0, Math.min(1000, Math.round(c.damage * 1000))), off + 16);
    buf.writeUInt8(c.place, off + 18);
    buf.writeUInt8(c.driverLook & 0x07, off + 19);
  }
  return buf;
}

function broadcastRoom(room: RaceRoom, eventType = 'room:state') {
  if (room.clients.size === 0) return;
  const jsonMsg = JSON.stringify({ type: eventType, payload: buildRoomPayload(room) });
  let binBuf: Buffer | null = null;

  for (const conn of room.clients.values()) {
    if (conn.ws.readyState !== WebSocket.OPEN) continue;
    if (conn.binaryMode) {
      if (!binBuf) binBuf = buildBinarySnapshot(room);
      conn.ws.send(binBuf, { binary: true });
    } else {
      conn.ws.send(jsonMsg);
    }
  }
}

function assignPlayerToRoom(
  conn: ClientConn,
  rawCode: string,
  name: string,
  platform: DevicePlatform,
  preferredLook?: number,
  spectator = false
) {
  // Leave previous room if switching
  if (conn.roomCode && conn.roomCode !== sanitizeCode(rawCode)) {
    removeClientFromRoom(conn);
  }

  const room = getOrCreateRoom(rawCode);
  room.lastActiveMs = Date.now();
  conn.roomCode = room.code;

  if (spectator) {
    // control page: joins the room (can configure and start the race) without taking a car
    conn.slot = -1;
    room.clients.set(conn.clientId, conn);
    if (!room.hostClientId || !room.clients.has(room.hostClientId)) room.hostClientId = conn.clientId;
    conn.ws.send(JSON.stringify({ type: 'room:joined', payload: { clientId: conn.clientId, slot: -1, room: buildRoomPayload(room) } }));
    broadcastRoom(room, 'room:state');
    return;
  }

  // Idempotent check: reclaim existing slot if same clientId reconnects
  let slot = room.cars.findIndex((c) => c.clientId === conn.clientId);
  if (slot === -1) {
    // Prefer player slot 7 first, then work upward so pole grid order feels natural
    for (let s = CAR_COUNT - 1; s >= 0; s--) {
      if (room.cars[s].platform === 'AI') {
        slot = s;
        break;
      }
    }
  }
  if (slot === -1) slot = CAR_COUNT - 1;

  conn.slot = slot;
  room.clients.set(conn.clientId, conn);
  if (!room.hostClientId || !room.clients.has(room.hostClientId)) {
    room.hostClientId = conn.clientId;
  }

  const car = room.cars[slot];
  car.clientId = conn.clientId;
  car.platform = platform;
  car.name = sanitizeName(name, platform === 'AKA' ? 'AKA-S3' : platform === 'PC' ? 'PC-SDL' : 'PILOTE');
  if (typeof preferredLook === 'number' && preferredLook >= 0 && preferredLook < CAR_COUNT) {
    car.driverLook = preferredLook;
  } else {
    car.driverLook = slot;
  }
  car.ready = true;
  car.retired = false;
  car.lastUpdateMs = Date.now();

  conn.ws.send(
    JSON.stringify({
      type: 'room:joined',
      payload: {
        clientId: conn.clientId,
        slot,
        room: buildRoomPayload(room),
      },
    })
  );
  if (conn.binaryMode) {
    // 6-byte welcome frame for binary clients (the Gamebuino AKA does not parse the JSON above):
    // 0xA6, slot, laps, difficulty, trackId, flags (bit0 = this client is the room host)
    conn.ws.send(Buffer.from([0xa6, slot, room.laps, room.difficulty, room.trackId, room.hostClientId === conn.clientId ? 1 : 0]), { binary: true });
  }
  broadcastRoom(room, 'room:state');
}

function removeClientFromRoom(conn: ClientConn) {
  if (!conn.roomCode) return;
  const room = rooms.get(conn.roomCode);
  if (!room) return;

  room.clients.delete(conn.clientId);
  const car = room.cars[conn.slot];
  if (car && car.clientId === conn.clientId) {
    car.clientId = `AI-${conn.slot}`;
    car.platform = 'AI';
    car.name = DRIVERS[conn.slot].name;
    car.driverLook = conn.slot;
    car.ready = true;
    car.retired = !room.fillAi;
    car.pingMs = 0;
  }

  if (room.hostClientId === conn.clientId) {
    const nextHost = room.clients.keys().next().value;
    room.hostClientId = nextHost || '';
  }

  if (room.clients.size === 0 && room.code !== 'AKA1') {
    room.phase = 'LOBBY';
    resetGridPositions(room);
  } else if (room.clients.size === 0 && room.code === 'AKA1') {
    room.phase = 'LOBBY';
    resetGridPositions(room);
  } else {
    broadcastRoom(room, 'room:state');
  }
}

function summarizeRoom(room: RaceRoom): RoomSummary {
  const humans = room.cars.filter((c) => c.platform !== 'AI');
  return {
    code: room.code,
    trackId: room.trackId,
    trackName: TRACK_DEFS[room.trackId]?.name || 'SUNSET COAST',
    laps: room.laps,
    fillAi: room.fillAi,
    phase: room.phase,
    humanCount: humans.length,
    totalCars: room.fillAi ? CAR_COUNT : humans.length,
    players: humans.map((h) => ({
      name: h.name,
      platform: h.platform as DevicePlatform,
      driverLook: h.driverLook,
      ready: h.ready,
    })),
  };
}

// 20 Hz Server-Authoritative Simulation Loop (50 ms tick)
const TICK_DT = 0.05;
setInterval(() => {
  const now = Date.now();
  for (const [code, room] of rooms.entries()) {
    if (room.clients.size === 0) {
      if (code !== 'AKA1' && now - room.lastActiveMs > 120_000) {
        rooms.delete(code);
      }
      continue;
    }

    room.lastActiveMs = now;

    if (room.phase === 'COUNTDOWN') {
      room.phaseTime += TICK_DT;
      if (room.phaseTime >= 4.0) {
        room.phase = 'RACING';
        room.phaseTime = 0;
        room.raceTime = 0;
        for (const c of room.cars) c.lapStart = 0;
      }
      broadcastRoom(room, 'race:snapshot');
    } else if (room.phase === 'RACING') {
      room.phaseTime += TICK_DT;
      room.raceTime += TICK_DT;

      for (const c of room.cars) {
        if (c.platform === 'AI') {
          if (room.fillAi) {
            updateServerAiCar(room, c, TICK_DT);
            updateCarLapProgress(room, c);
          }
        } else {
          // Extrapolate smoothly if a packet is slightly delayed, and validate laps server-side
          if (now - c.lastUpdateMs > 120 && !c.finished && !c.retired) {
            c.dist += c.speed * TICK_DT * 0.85;
          }
          updateCarLapProgress(room, c);
        }
      }

      computeRankings(room);

      // Check if all human players finished (or retired) or 15s grace timer after first human finish
      // a human that stopped sending for 8 s (closed tab, lost Wi-Fi) no longer holds the results back
      const humans = room.cars.filter((c) => c.platform !== 'AI' && (c.finished || c.retired || now - c.lastUpdateMs < 8000));
      const allHumansDone = humans.length > 0 && humans.every((h) => h.finished || h.retired);
      const anyHumanFinished = humans.some((h) => h.finished);

      if (allHumansDone || (anyHumanFinished && room.raceTime - Math.min(...humans.filter((h) => h.finished).map((h) => h.finishTime)) > 15)) {
        // Estimate remaining AI cars cleanly just like Race::estimateUnfinished()
        for (const c of room.cars) {
          if (!c.finished && (c.platform !== 'AI' || room.fillAi)) {
            const remain = Math.max(0, room.laps * room.road.length - c.dist);
            const v = Math.max(c.speed, MAX_SPEED * 0.6);
            c.finishTime = c.retired ? room.raceTime + 9999 : room.raceTime + remain / v;
            c.finished = true;
          }
        }
        computeRankings(room);
        room.phase = 'RESULTS';
        room.phaseTime = 0;
      }

      broadcastRoom(room, 'race:snapshot');
    } else if (room.phase === 'RESULTS') {
      room.phaseTime += TICK_DT;
      if (room.phaseTime < 1.0 || Math.trunc(room.phaseTime * 20) % 10 === 0) {
        broadcastRoom(room, 'room:state');
      }
    }
  }
}, 50);

async function startServer() {
  const app = express();
  app.use(express.json());
  // the control page lives on GitHub Pages (another origin): allow it to read the status endpoints
  app.use((_req, res, next) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    next();
  });
  app.get('/', (_req, res) => {
    res.json({ ok: true, server: 'AKA Turbo 3D Grand Prix Race Server', health: '/api/health', rooms: '/api/rooms', ws: '/ws/race' });
  });

  app.get('/api/health', (_req, res) => {
    res.json({
      ok: true,
      server: 'AKA Turbo 3D Grand Prix Race Server',
      tickHz: 20,
      protocolVersion: '1.2-aka',
      wsPath: '/ws/race',
      binaryPacketBytes: AKA_BIN_PACKET_SIZE,
      activeRooms: rooms.size,
    });
  });

  app.get('/api/rooms', (_req, res) => {
    const list: RoomSummary[] = [];
    for (const r of rooms.values()) {
      list.push(summarizeRoom(r));
    }
    res.json({ rooms: list });
  });

  const server = http.createServer(app);
  const wss = new WebSocketServer({ server, path: '/ws/race' });

  wss.on('connection', (ws, req) => {
    const url = new URL(req.url || '/ws/race', `http://${req.headers.host || 'localhost'}`);
    const queryClientId = url.searchParams.get('clientId');
    const clientId = queryClientId || `c-${Math.random().toString(36).slice(2, 10)}`;
    const binaryMode = url.searchParams.get('mode') === 'binary';

    const conn: ClientConn = {
      ws,
      clientId,
      roomCode: '',
      slot: CAR_COUNT - 1,
      binaryMode,
      pingMs: 12,
    };

    // Optional instant query-param join for Gamebuino AKA ESP32-S3 & PC C++ clients
    const autoRoom = url.searchParams.get('room');
    if (autoRoom) {
      const platParam = (url.searchParams.get('platform') || 'AKA').toUpperCase();
      const platform: DevicePlatform = platParam === 'PC' ? 'PC' : platParam === 'WEB' ? 'WEB' : 'AKA';
      const name = url.searchParams.get('name') || (platform === 'AKA' ? 'AKA-S3' : 'PC-RACER');
      const look = Number(url.searchParams.get('look') ?? -1);
      assignPlayerToRoom(conn, autoRoom, name, platform, look >= 0 ? look : undefined);
    }

    ws.on('message', (data, isBinary) => {
      try {
        // Fast path: 20-byte binary telemetry packet from Gamebuino AKA ESP32-S3 or PC SDL2
        if (isBinary && Buffer.isBuffer(data) && data.length >= AKA_BIN_PACKET_SIZE && data[0] === AKA_BIN_MAGIC) {
          const room = rooms.get(conn.roomCode);
          if (!room) return;
          const car = room.cars[conn.slot];
          if (!car || car.clientId !== conn.clientId) return;
          const flags = data[1] >> 3;
          car.dist = data.readFloatLE(2);
          car.x = Math.max(-3.5, Math.min(3.5, data.readFloatLE(6)));
          car.speed = Math.max(0, Math.min(MAX_SPEED * 1.45, data.readFloatLE(10)));
          car.steerVis = Math.max(-1, Math.min(1, data.readInt16LE(14) / 1000));
          car.damage = Math.max(0, Math.min(1, data.readUInt16LE(16) / 1000));
          car.braking = Boolean(flags & 1);
          car.nitroOn = Boolean(flags & 2);
          car.pitting = Boolean(flags & 4);
          car.retired = Boolean(flags & 8);
          car.lastUpdateMs = Date.now();
          return;
        }

        const msg = JSON.parse(data.toString());
        if (!msg || typeof msg.type !== 'string') return;

        if (msg.type === 'ping') {
          if (typeof msg.rtt === 'number' && conn.roomCode) {
            conn.pingMs = Math.max(1, Math.min(999, Math.round(msg.rtt)));
            const r = rooms.get(conn.roomCode);
            const c = r?.cars[conn.slot];
            if (c && c.clientId === conn.clientId) c.pingMs = conn.pingMs;
          }
          ws.send(JSON.stringify({ type: 'pong', clientSentAt: msg.clientSentAt, serverTimeMs: Date.now() }));
          return;
        }

        if (msg.type === 'room:join') {
          const p = msg.payload || {};
          const plat: DevicePlatform = p.platform === 'AKA' || p.platform === 'PC' ? p.platform : 'WEB';
          assignPlayerToRoom(conn, p.roomCode || 'AKA1', p.name || 'PILOTE', plat, p.driverLook, p.spectator === true);
          return;
        }

        const room = rooms.get(conn.roomCode);
        if (!room) return;

        if (msg.type === 'player:profile') {
          const p = msg.payload || {};
          const car = room.cars[conn.slot];
          if (car && car.clientId === conn.clientId) {
            if (typeof p.name === 'string') car.name = sanitizeName(p.name, car.name);
            if (typeof p.driverLook === 'number') car.driverLook = Math.max(0, Math.min(CAR_COUNT - 1, p.driverLook));
            if (p.platform === 'AKA' || p.platform === 'PC' || p.platform === 'WEB') car.platform = p.platform;
            if (typeof p.ready === 'boolean') car.ready = p.ready;
            broadcastRoom(room, 'room:state');
          }
          return;
        }

        if (msg.type === 'room:configure') {
          const p = msg.payload || {};
          if (room.phase === 'LOBBY' || room.phase === 'RESULTS') {
            if (typeof p.trackId === 'number') {
              room.trackId = Math.max(0, Math.min(TRACK_DEFS.length - 1, Math.trunc(p.trackId)));
              room.road = buildRoadGeometry(room.trackId);
            }
            if (typeof p.laps === 'number') {
              room.laps = [1, 2, 3, 5].includes(p.laps) ? p.laps : 3;
            }
            if (typeof p.difficulty === 'number') {
              room.difficulty = Math.max(0, Math.min(3, Math.trunc(p.difficulty)));
            }
            if (typeof p.fillAi === 'boolean') {
              room.fillAi = p.fillAi;
              for (const c of room.cars) {
                if (c.platform === 'AI') c.retired = !room.fillAi;
              }
            }
            broadcastRoom(room, 'room:state');
          }
          return;
        }

        if (msg.type === 'race:start') {
          if (room.phase === 'LOBBY' || room.phase === 'RESULTS') {
            resetGridPositions(room);
            room.phase = 'COUNTDOWN';
            room.phaseTime = 0;
            broadcastRoom(room, 'room:state');
          }
          return;
        }

        if (msg.type === 'race:lobby') {
          room.phase = 'LOBBY';
          resetGridPositions(room);
          broadcastRoom(room, 'room:state');
          return;
        }

        if (msg.type === 'car:telemetry') {
          const t = (msg.payload || {}) as ClientCarTelemetry;
          const car = room.cars[conn.slot];
          if (car && car.clientId === conn.clientId && (room.phase === 'RACING' || room.phase === 'COUNTDOWN')) {
            if (room.phase === 'RACING' && Number.isFinite(t.dist)) {
              car.dist = t.dist;
            }
            if (Number.isFinite(t.x)) car.x = Math.max(-3.5, Math.min(3.5, t.x));
            if (Number.isFinite(t.speed)) car.speed = Math.max(0, Math.min(MAX_SPEED * 1.45, t.speed));
            if (Number.isFinite(t.steerVis)) car.steerVis = Math.max(-1, Math.min(1, t.steerVis));
            car.braking = Boolean(t.braking);
            car.nitroOn = Boolean(t.nitroOn);
            car.pitting = Boolean(t.pitting);
            if (Number.isFinite(t.damage)) car.damage = Math.max(0, Math.min(1, t.damage));
            car.retired = Boolean(t.retired);
            car.lastUpdateMs = Date.now();
          }
        }
      } catch {
        // Ignore malformed packets safely
      }
    });

    ws.on('close', () => {
      removeClientFromRoom(conn);
    });
  });

  server.listen(PORT, '0.0.0.0', () => {
    console.log(`AKA Turbo 3D Race Server listening on http://0.0.0.0:${PORT}`);
  });
}

startServer();
