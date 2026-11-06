// A small stylised cluster map for the hero, like the app's drive map. Generated from a fixed seed so every build
// renders the same picture. Purely decorative.

const columns = 36;
const rows = 12;
const cell = 14;
const gap = 3;

// Cell kinds, matching classes in globals.css.
const kinds = ["free", "a", "b", "c", "d", "frag"] as const;
type Kind = (typeof kinds)[number];

// Small seeded random generator (mulberry32): same seed, same map.
function random(seed: number) {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Files come in runs of one color; free space and fragments break them up, thinning towards the end of the disk.
function layout(): Kind[] {
  const next = random(20261106);
  const cells: Kind[] = [];
  while (cells.length < columns * rows) {
    const depth = cells.length / (columns * rows);
    const roll = next();
    const kind: Kind =
      roll < 0.12 + depth * 0.45 ? "free" : roll < 0.2 + depth * 0.4 ? "frag" : kinds[1 + Math.floor(next() * 4)];
    const run = 1 + Math.floor(next() * (kind === "free" ? 5 : 7));
    for (let i = 0; i < run; i++) cells.push(kind);
  }
  return cells.slice(0, columns * rows);
}

export default function DiskMap() {
  const cells = layout();
  const width = columns * (cell + gap) - gap;
  const height = rows * (cell + gap) - gap;
  return (
    <svg className="disk-map" viewBox={`0 0 ${width} ${height}`} role="img" aria-label="Illustration of a drive map">
      {cells.map((kind, i) => {
        const x = (i % columns) * (cell + gap);
        const y = Math.floor(i / columns) * (cell + gap);
        // Every 23rd file cell pulses, like reads and writes on the live map.
        const pulse = kind !== "free" && i % 23 === 0;
        return (
          <rect
            key={i}
            x={x}
            y={y}
            width={cell}
            height={cell}
            rx={3}
            className={`cell cell-${kind}${pulse ? " pulse" : ""}`}
            style={pulse ? { animationDelay: `${(i % 7) * 0.45}s` } : undefined}
          />
        );
      })}
    </svg>
  );
}
