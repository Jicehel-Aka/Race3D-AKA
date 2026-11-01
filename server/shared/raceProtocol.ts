// Shared race constants, authentic Race3D-AKA track parser, and cross-platform multiplayer protocol
// Pure TypeScript types + const values (compatible with Node 22 native strip-types in server.ts)

export const SCREEN_W = 320;
export const SCREEN_H = 240;
export const HORIZON_Y = 112;
export const SEG_LEN = 200;
export const ROAD_W = 2000;
export const CAM_HEIGHT = 1000;
export const CAM_DEPTH = 0.8391; // 1 / tan(100deg / 2)
export const PLAYER_Z = CAM_HEIGHT * CAM_DEPTH;
export const CAR_COUNT = 8;
export const MAX_SPEED = SEG_LEN * 60; // 12000 units/s = 240 km/h
export const CAR_NORM_W = 700 / ROAD_W;

// Pit lane v1.2 geometry: separated right-side pit apron + 3 specialized bays
export const PIT_ENTRY_FIRST = 4;
export const PIT_WALL_FIRST = 10;
export const PIT_WALL_END = 38;
export const PIT_EXIT_END = 44;

// Lateral bounds of the separated v1.2 pit apron (road right edge is +1.0)
export const PIT_WALL_X = 1.04;
export const PIT_LANE_X0 = 1.12;
export const PIT_LANE_X1 = 1.96;

// Three distinct Pit Bays ("les différents pit stops")
export const PIT_BAY_NITRO = { start: 11, end: 18, label: 'N2O EXPRESS', maxSpeedKmh: 95 };
export const PIT_BAY_TYRES = { start: 19, end: 26, label: 'PNEUS & TRAIN', maxSpeedKmh: 38 };
export const PIT_BAY_ENGINE = { start: 27, end: 36, label: 'GARAGE MOTEUR', maxSpeedKmh: 30 };

// Legacy v1.1 pit lane constants (for Before/After comparison mode)
export const PIT_V11 = { first: 8, len: 22, box0: 14, box1: 26, x0: 0.42, x1: 0.96 };

export const THEME_COAST = 0;
export const THEME_ALPINE = 1;
export const THEME_CITY = 2;
export const THEME_TROPIC = 3;
export const THEME_GLACIER = 4;
export const THEME_HARBOR = 5;

export interface DriverInfo {
  id: number;
  name: string;
  body: [number, number, number];
  stripe: [number, number, number];
  hex: string;
  stripeHex: string;
  v11FrontBugSide: 'right' | 'left';
}

export const DRIVERS: DriverInfo[] = [
  { id: 0, name: 'VIPER', body: [30, 90, 220], stripe: [255, 214, 0], hex: '#1E5ADC', stripeHex: '#FFD600', v11FrontBugSide: 'right' },
  { id: 1, name: 'BLAZE', body: [250, 200, 20], stripe: [30, 30, 36], hex: '#FAC814', stripeHex: '#1E1E24', v11FrontBugSide: 'left' },
  { id: 2, name: 'NOVA', body: [40, 170, 80], stripe: [250, 250, 250], hex: '#28AA50', stripeHex: '#FAFAFA', v11FrontBugSide: 'right' },
  { id: 3, name: 'RAVEN', body: [70, 70, 82], stripe: [230, 180, 40], hex: '#464652', stripeHex: '#E6B428', v11FrontBugSide: 'left' },
  { id: 4, name: 'ACE', body: [240, 240, 244], stripe: [220, 30, 40], hex: '#F0F0F4', stripeHex: '#DC1E28', v11FrontBugSide: 'left' },
  { id: 5, name: 'ZED', body: [140, 50, 200], stripe: [255, 140, 0], hex: '#8C32C8', stripeHex: '#FF8C00', v11FrontBugSide: 'left' },
  { id: 6, name: 'KAI', body: [255, 120, 20], stripe: [30, 70, 200], hex: '#FF7814', stripeHex: '#1E46C8', v11FrontBugSide: 'left' },
  { id: 7, name: 'YOU', body: [220, 24, 36], stripe: [250, 250, 250], hex: '#DC1824', stripeHex: '#FAFAFA', v11FrontBugSide: 'right' },
];

export interface TrackDefinition {
  id: number;
  name: string;
  theme: number;
  musicTitle: string;
  bpm: number;
  seed: number;
  bgSeed: number;
  pitStyleFr: string;
  animHighlightsFr: string;
  script: string;
}

// Exact track scripts from Jicehel-Aka/Race3D-AKA main/game/tracks_data.cpp
export const TRACK_DEFS: TrackDefinition[] = [
  {
    id: 0,
    name: 'SUNSET COAST',
    theme: THEME_COAST,
    musicTitle: 'SUNSET DRIVE',
    bpm: 112,
    seed: 1234,
    bgSeed: 99,
    pitStyleFr: 'Paddock côtier palmiers & arche F1 néon rétro',
    animHighlightsFr: 'Soleil synthwave animé · Oiseaux marins en vol · Flashs tribunes · Drapeaux à damier',
    script: `straight SHORT
curve MEDIUM EASY
road SHORT MEDIUM SHORT 0 LOW
curve MEDIUM MED -LOW
straight MEDIUM
scurves MED
curve LONG EASY MED
road MEDIUM MEDIUM MEDIUM -EASY -MED
straight LONG
curve MEDIUM HARD
road SHORT SHORT SHORT 0 LOW
curve MEDIUM MED -LOW
curve SHORT -MED
curve MEDIUM MED
downhill MEDIUM
straight SHORT`,
  },
  {
    id: 1,
    name: 'ALPINE PASS',
    theme: THEME_ALPINE,
    musicTitle: 'SUMMIT RUSH',
    bpm: 138,
    seed: 1311,
    bgSeed: 130,
    pitStyleFr: 'Chalet mécanique alpin en bois & toit enneigé',
    animHighlightsFr: 'Flocons en haute altitude · Télécabine sur câble · Tunnel rocheux éclairé (SEGF_TUNNEL)',
    script: `straight SHORT
road MEDIUM MEDIUM MEDIUM 0 HIGH
curve MEDIUM HARD LOW
road SHORT SHORT SHORT 0 -MED
curve MEDIUM -MED MED
curve SHORT HARD
road LONG SHORT MEDIUM 0 -HIGH
scurves HARD
bumps
curve MEDIUM MED HIGH
curve MEDIUM HARD -MED
road MEDIUM MEDIUM SHORT -EASY LOW
curve SHORT HARD -LOW
downhill LONG
straight SHORT`,
  },
  {
    id: 2,
    name: 'NEON CITY',
    theme: THEME_CITY,
    musicTitle: 'NEON NIGHTS',
    bpm: 124,
    seed: 1388,
    bgSeed: 161,
    pitStyleFr: 'Box cyber-garage sous portique LED cyan/magenta',
    animHighlightsFr: 'Monorail express sur skyline · Fenêtres qui clignotent · Faisceaux laser · Chevrons >>> séquentiels',
    script: `straight MEDIUM
road 10 40 10 HARD 0
straight LONG
road 10 40 10 HARD 0
bumps
road 10 30 10 -HARD 0
road 10 50 10 HARD LOW
straight MEDIUM -LOW
road 10 40 10 HARD 0
scurves MED
straight LONG
road 10 40 10 HARD 0
road SHORT SHORT SHORT -MED 0
road SHORT SHORT SHORT HARD 0
downhill SHORT
straight MEDIUM`,
  },
  {
    id: 3,
    name: 'PALM BAY',
    theme: THEME_TROPIC,
    musicTitle: 'SEA BREEZE',
    bpm: 128,
    seed: 4217,
    bgSeed: 58,
    pitStyleFr: 'Tente de plage paddock turquoise & fanions tropicaux',
    animHighlightsFr: 'Vagues scintillantes animées · Voiliers qui tanguent · Palmes au vent · Fanions de plage',
    script: `straight MEDIUM
curve SHORT MED
road 10 10 10 -HARD 0
road 10 10 10 HARD 0
road 10 10 10 -HARD 0
road 10 10 10 HARD 0
straight SHORT LOW
curve MEDIUM HARD LOW
road SHORT MEDIUM SHORT 0 -MED
curve SHORT -EASY
curve MEDIUM MED
straight MEDIUM
road 15 15 15 HARD 0
road 15 15 15 -HARD 0
curve LONG EASY LOW
road SHORT SHORT SHORT -MED -LOW
road SHORT SHORT SHORT 0 MED
road SHORT SHORT SHORT 0 -MED
straight LONG
road 12 20 12 HARD 0
curve SHORT -MED
curve MEDIUM MED
curve SHORT HARD
curve MEDIUM -EASY
straight SHORT`,
  },
  {
    id: 4,
    name: 'GLACIER RUN',
    theme: THEME_GLACIER,
    musicTitle: 'POLAR LIGHTS',
    bpm: 96,
    seed: 7351,
    bgSeed: 212,
    pitStyleFr: 'Dôme polaire chauffé orange expédition arctique',
    animHighlightsFr: 'Aurores boréales ondulantes en temps réel · Cristaux polaires · Reflets sur parois de glace & tunnel',
    script: `straight MEDIUM
road MEDIUM SHORT MEDIUM 0 MED
curve MEDIUM EASY -LOW
curve SHORT MED
bumps
curve MEDIUM HARD MED
road SHORT SHORT SHORT -MED -HIGH
road SHORT MEDIUM SHORT 0 LOW
curve MEDIUM -MED
curve MEDIUM MED LOW
straight MEDIUM HIGH
curve MEDIUM HARD -MED
curve SHORT -HARD
curve SHORT HARD
road LONG SHORT MEDIUM EASY -HIGH
straight SHORT
downhill LONG
straight LONG`,
  },
  {
    id: 5,
    name: 'NIGHT HARBOR',
    theme: THEME_HARBOR,
    musicTitle: 'DOCK RUNNER',
    bpm: 118,
    seed: 9034,
    bgSeed: 305,
    pitStyleFr: 'Atelier conteneurs maritimes & portique de quai sodium',
    animHighlightsFr: 'Faisceau tournant du phare · Projecteurs du port · Balises rouges grues/cargos · Reflets sur eau',
    script: `straight LONG
curve MEDIUM MED
straight SHORT
road 12 30 12 HARD 0
straight MEDIUM LOW
road MEDIUM SHORT MEDIUM 0 -LOW
curve MEDIUM EASY
road MEDIUM SHORT MEDIUM 0 MED
straight SHORT
road MEDIUM SHORT MEDIUM 0 -MED
straight LONG
road 10 25 10 -HARD 0
road 10 40 10 HARD 0
curve LONG EASY
straight MEDIUM
curve MEDIUM MED
road SHORT SHORT SHORT -MED 0
road SHORT MEDIUM SHORT HARD 0
straight MEDIUM
road 8 8 8 -HARD 0
road 8 8 8 HARD 0
road 8 8 8 -HARD 0
straight SHORT`,
  },
];

export const SEGF_START = 1;
export const SEGF_TUNNEL = 2;
export const SEGF_ALT = 4;
export const SEGF_MID_PIT = 8;

export interface RoadSeg {
  curve: number;
  y1: number;
  y2: number;
  flags: number;
}

export interface ParsedRoad {
  id: number;
  name: string;
  theme: number;
  segs: RoadSeg[];
  length: number;
  maxY: number;
  mapX: Int8Array;
  mapY: Int8Array;
}

// Deterministic port of main/game/track.cpp Builder & Parser
export function buildRoadGeometry(trackId: number): ParsedRoad {
  const def = TRACK_DEFS[Math.max(0, Math.min(TRACK_DEFS.length - 1, trackId))];
  const segs: RoadSeg[] = [];

  const LEN_MAP: Record<string, number> = { NONE: 0, SHORT: 25, MEDIUM: 50, MED: 50, LONG: 100 };
  const CURVE_MAP: Record<string, number> = { EASY: 2, MED: 4, MEDIUM: 4, HARD: 6 };
  const HILL_MAP: Record<string, number> = { LOW: 20, MED: 40, MEDIUM: 40, HIGH: 60 };

  const lastY = () => (segs.length === 0 ? 0 : segs[segs.length - 1].y2);
  const easeIn = (a: number, b: number, p: number) => a + (b - a) * p * p;
  const easeOut = (a: number, b: number, p: number) => a + (b - a) * (1 - (1 - p) * (1 - p));
  const easeInOut = (a: number, b: number, p: number) => a + (b - a) * (-Math.cos(p * Math.PI) / 2 + 0.5);

  const addSeg = (curve: number, y: number) => {
    const y1 = lastY();
    segs.push({ curve, y1, y2: y, flags: 0 });
  };

  const addRoad = (enter: number, hold: number, leave: number, curve: number, hillY: number) => {
    const startY = lastY();
    const endY = startY + hillY * SEG_LEN;
    const total = enter + hold + leave;
    if (total <= 0) return;
    for (let n = 0; n < enter; n++) {
      addSeg(easeIn(0, curve, n / enter), easeInOut(startY, endY, n / total));
    }
    for (let n = 0; n < hold; n++) {
      addSeg(curve, easeInOut(startY, endY, (enter + n) / total));
    }
    for (let n = 0; n < leave; n++) {
      addSeg(easeInOut(curve, 0, n / leave), easeInOut(startY, endY, (enter + hold + n) / total));
    }
  };

  const parseVal = (raw: string | undefined, kind: 'len' | 'curve' | 'hill', fallback = 0): number => {
    if (!raw) return fallback;
    let s = raw.trim().toUpperCase();
    let sign = 1;
    if (s.startsWith('-')) {
      sign = -1;
      s = s.slice(1);
    } else if (s.startsWith('+')) {
      s = s.slice(1);
    }
    if (kind === 'len' && s in LEN_MAP) return sign * LEN_MAP[s];
    if (kind === 'curve' && s in CURVE_MAP) return sign * CURVE_MAP[s];
    if (kind === 'hill' && s in HILL_MAP) return sign * HILL_MAP[s];
    const num = Number(s);
    return Number.isFinite(num) ? sign * num : fallback;
  };

  const lines = def.script.split(/\r?\n|;/);
  for (const rawLine of lines) {
    const clean = rawLine.split('#')[0].trim();
    if (!clean) continue;
    const tok = clean.split(/[\s,]+/);
    const cmd = tok[0].toUpperCase();
    if (cmd === 'STRAIGHT') {
      const len = Math.max(0, Math.min(400, Math.trunc(parseVal(tok[1], 'len', 50))));
      const hill = Math.max(-200, Math.min(200, parseVal(tok[2], 'hill', 0)));
      addRoad(len, len, len, 0, hill);
    } else if (cmd === 'CURVE') {
      const len = Math.max(0, Math.min(400, Math.trunc(parseVal(tok[1], 'len', 50))));
      const c = Math.max(-12, Math.min(12, parseVal(tok[2], 'curve', 2)));
      const hill = Math.max(-200, Math.min(200, parseVal(tok[3], 'hill', 0)));
      addRoad(len, len, len, c, hill);
    } else if (cmd === 'ROAD') {
      const a = Math.max(0, Math.min(400, Math.trunc(parseVal(tok[1], 'len', 25))));
      const b = Math.max(0, Math.min(400, Math.trunc(parseVal(tok[2], 'len', 25))));
      const d = Math.max(0, Math.min(400, Math.trunc(parseVal(tok[3], 'len', 25))));
      const c = Math.max(-12, Math.min(12, parseVal(tok[4], 'curve', 0)));
      const hill = Math.max(-200, Math.min(200, parseVal(tok[5], 'hill', 0)));
      addRoad(a, b, d, c, hill);
    } else if (cmd === 'SCURVES') {
      const c = Math.max(-12, Math.min(12, parseVal(tok[1], 'curve', 4)));
      addRoad(50, 50, 50, -c, 0);
      addRoad(50, 50, 50, c, 0);
      addRoad(50, 50, 50, -c, 0);
      addRoad(50, 50, 50, c, 0);
    } else if (cmd === 'BUMPS') {
      addRoad(10, 10, 10, 0, 5);
      addRoad(10, 10, 10, 0, -2);
      addRoad(10, 10, 10, 0, -5);
      addRoad(10, 10, 10, 0, 8);
      addRoad(10, 10, 10, 0, 5);
      addRoad(10, 10, 10, 0, -7);
      addRoad(10, 10, 10, 0, 5);
      addRoad(10, 10, 10, 0, -2);
    } else if (cmd === 'DOWNHILL') {
      const num = Math.max(1, Math.min(400, Math.trunc(parseVal(tok[1], 'len', 50)))) * 3;
      const startY = lastY();
      for (let j = 0; j < num; j++) {
        addSeg(0, easeOut(startY, 0, (j + 1) / num));
      }
    }
  }

  if (segs.length > 0 && Math.abs(lastY()) > 20) {
    const num = 150;
    const startY = lastY();
    for (let j = 0; j < num; j++) {
      addSeg(0, easeInOut(startY, 0, (j + 1) / num));
    }
  }
  if (segs.length === 0) addRoad(50, 50, 50, 0, 0);

  let maxY = 0;
  for (const s of segs) if (s.y1 > maxY) maxY = s.y1;

  const n = segs.length;
  for (let i = 0; i < Math.min(3, n); i++) segs[i].flags |= SEGF_START;

  // Alternate ground & v1.2 Tunnel sections (activating track.h's SEGF_TUNNEL = 2!)
  for (let i = 0; i < n; i++) {
    const pct = Math.trunc((i * 100) / n);
    if (
      (def.theme === THEME_TROPIC && (pct < 28 || (pct >= 50 && pct < 72))) ||
      (def.theme === THEME_GLACIER && pct >= 25 && pct < 70) ||
      (def.theme === THEME_HARBOR && maxY > 300 && segs[i].y1 > 0.45 * maxY)
    ) {
      segs[i].flags |= SEGF_ALT;
    }

    // Tunnel galleries on Alpine Pass (mountain gallery), Neon City (expressway underpass), Glacier Run (ice cavern)
    if (
      (def.theme === THEME_ALPINE && pct >= 42 && pct < 48) ||
      (def.theme === THEME_CITY && pct >= 58 && pct < 64) ||
      (def.theme === THEME_GLACIER && pct >= 76 && pct < 81)
    ) {
      segs[i].flags |= SEGF_TUNNEL;
    }

    // Optional mid-lap emergency N2O/service strip at 50% lap on long tracks
    const mid = Math.trunc(n * 0.5);
    if (i >= mid && i < mid + 14) {
      segs[i].flags |= SEGF_MID_PIT;
    }
  }

  // Minimap 128-point closed loop (exact port of track.cpp minimap())
  const MAP_N = 128;
  let totalCurve = 0;
  for (const s of segs) totalCurve += s.curve;
  const k = Math.abs(totalCurve) > 1 ? (Math.PI * 2) / totalCurve : 0.003;
  const xs = new Float32Array(n + 1);
  const ys = new Float32Array(n + 1);
  let h = 0, x = 0, y = 0;
  for (let i = 0; i < n; i++) {
    xs[i] = x;
    ys[i] = y;
    h += segs[i].curve * k;
    x += Math.sin(h);
    y -= Math.cos(h);
  }
  xs[n] = x;
  ys[n] = y;
  let minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
  for (let i = 0; i <= n; i++) {
    xs[i] -= (xs[n] * i) / n;
    ys[i] -= (ys[n] * i) / n;
    if (xs[i] < minx) minx = xs[i];
    if (xs[i] > maxx) maxx = xs[i];
    if (ys[i] < miny) miny = ys[i];
    if (ys[i] > maxy) maxy = ys[i];
  }
  const sc = 54 / Math.max(1e-3, Math.max(maxx - minx, maxy - miny));
  const cx = (minx + maxx) / 2;
  const cy = (miny + maxy) / 2;
  const mapX = new Int8Array(MAP_N);
  const mapY = new Int8Array(MAP_N);
  for (let m = 0; m < MAP_N; m++) {
    const idx = Math.trunc((m * n) / MAP_N);
    mapX[m] = Math.round((xs[idx] - cx) * sc);
    mapY[m] = Math.round((ys[idx] - cy) * sc);
  }

  return {
    id: def.id,
    name: def.name,
    theme: def.theme,
    segs,
    length: n * SEG_LEN,
    maxY,
    mapX,
    mapY,
  };
}

// ============================================================================
// Cross-Platform Multiplayer Protocol (Web + PC SDL2 + Gamebuino AKA ESP32-S3)
// ============================================================================

export type DevicePlatform = 'AKA' | 'PC' | 'WEB';
export type RoomPhase = 'LOBBY' | 'COUNTDOWN' | 'RACING' | 'RESULTS';

export interface NetCarSnapshot {
  slot: number;
  clientId: string; // 'AI-0' .. 'AI-7' or player clientId
  name: string;
  platform: DevicePlatform | 'AI';
  driverLook: number;
  ready: boolean;
  dist: number;
  x: number;
  speed: number;
  steerVis: number;
  braking: boolean;
  nitroOn: boolean;
  pitting: boolean;
  damage: number;
  lapsDone: number;
  bestLap: number;
  finishTime: number;
  finished: boolean;
  retired: boolean;
  place: number;
  pingMs: number;
}

export interface RoomSummary {
  code: string;
  trackId: number;
  trackName: string;
  laps: number;
  fillAi: boolean;
  phase: RoomPhase;
  humanCount: number;
  totalCars: number;
  players: Array<{ name: string; platform: DevicePlatform; driverLook: number; ready: boolean }>;
}

export interface RoomStatePayload {
  code: string;
  hostClientId: string;
  trackId: number;
  laps: number;
  difficulty: number;
  fillAi: boolean;
  phase: RoomPhase;
  phaseTime: number;
  raceTime: number;
  serverTimeMs: number;
  cars: NetCarSnapshot[];
}

export interface ClientCarTelemetry {
  dist: number;
  x: number;
  speed: number;
  steerVis: number;
  braking: boolean;
  nitroOn: boolean;
  pitting: boolean;
  damage: number;
  retired: boolean;
}

// Binary 20-byte frame header for Gamebuino AKA ESP32-S3 & PC SDL2 low-overhead Wi-Fi sync:
// Byte 0:   0xA4 (magic)
// Byte 1:   slot (0..7) | (flags << 3): bit3=braking, bit4=nitroOn, bit5=pitting, bit6=retired
// Byte 2..5:   float32 LE dist
// Byte 6..9:   float32 LE x
// Byte 10..13: float32 LE speed
// Byte 14..15: int16 LE steerVis * 1000
// Byte 16..17: uint16 LE damage * 1000
// Byte 18..19: uint16 LE seq / ping token
export const AKA_BIN_MAGIC = 0xa4;
export const AKA_BIN_PACKET_SIZE = 20;
