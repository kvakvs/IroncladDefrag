// Site-wide text and links, kept in one place so pages stay layout-only.

export const site = {
  name: "Ironclad Defrag",
  tagline: "Fast, free and open source defragmentation for Windows 10 and 11",
  pitch:
    "For people who like to see exactly where their files live, and decide exactly what moves. " +
    "Every cluster on the map, every move planned in the open, nothing touched until you say so.",
  sourceUrl: "https://github.com/kvakvs/IroncladDefrag",
  // The versions page: every installer with its checksum and changelog (see lib/releases.ts).
  releasesUrl: "/releases/",
};

// Headline numbers, measured on the project's own benchmarks.
export const stats = [
  { value: "0.5 s", label: "to read a million files straight from the MFT" },
  { value: "120 fps", label: "zooming from a whole 30 TB volume down to one cluster" },
  { value: "20 M", label: "files planned on a 30 TB volume in under a minute" },
  { value: "0", label: "files moved that don't need to be" },
];

// One card per feature: short, benefit first.
export const features = [
  {
    icon: "⚡",
    title: "Analysis in seconds, not coffee breaks",
    body: "Reads the NTFS master file table directly, many times faster than asking Windows file by file. Come back later and the USN journal catches the map up in seconds.",
  },
  {
    icon: "🗺️",
    title: "A map of every single cluster",
    body: "Zoom smoothly from the whole disk to a single cluster. Hover for the file, its fragments and its age; click to select; Ctrl+F to jump to any file.",
  },
  {
    icon: "🎨",
    title: "See your disk the way you think",
    body: "Color the map by fragmentation, age, size or file class: source code, photos, videos, archives, logs and more. Colorblind-safe palettes, custom colors, light and dark themes.",
  },
  {
    icon: "🧭",
    title: "Strategies, not just “defrag”",
    body: "Hot files to the fast zone, cold files to the back, folders clustered together, sorted by type, size or last access. Presets for development, photos, video and archives, or write your own.",
  },
  {
    icon: "🪶",
    title: "Moves as little as possible",
    body: "The planner works out the minimum that has to move and keeps everything else in place. Choose Fast, Moderate or Maximum effort and see the cost before you commit.",
  },
  {
    icon: "🔍",
    title: "Review the plan before anything moves",
    body: "Every planned move is shown on the map and listed in the Plan tab. Nothing is written until you confirm, and you can pause or cancel at any time.",
  },
  {
    icon: "🛡️",
    title: "Safe by design",
    body: "Files are moved only through Windows' own defragmentation API and re-checked right before each move. Every step is logged, so an interrupted run resumes exactly where it stopped.",
  },
  {
    icon: "💿",
    title: "Kind to hard drives",
    body: "Moves are ordered by where the disk head already is, so a hard drive spends its time reading and writing, not seeking. Shadow copy aware placement keeps restore points from ballooning.",
  },
  {
    icon: "✨",
    title: "Watch it work",
    body: "Reads and writes pulse on the live map as they happen, and the Log tab tells you what was moved, skipped and why.",
  },
  {
    icon: "🔋",
    title: "SSD-aware",
    body: "Solid-state drives are recognised and never defragmented. They get a one-click TRIM instead, with the last TRIM date and trimmed bytes on show.",
  },
  {
    icon: "🎯",
    title: "Defragment just this file",
    body: "Found that one fragmented database or VM image? Pick it on the map, press Ctrl+D, and it's back in one piece without planning the whole drive.",
  },
  {
    icon: "⌨️",
    title: "Scriptable command line",
    body: "icd-cli analyzes, plans and executes from scripts, with JSON reports you can feed into your own tools. Decide per drive whether Windows' own scheduled optimization may touch it.",
  },
];

// The four steps of the workflow, in the order the app walks you through them.
export const steps = [
  { title: "Analyze", body: "Scan a drive and get its full map and a fragmentation summary." },
  { title: "Choose a strategy", body: "Pick a preset or your own zones, plus how hard to try." },
  { title: "Review the plan", body: "See every move and how many bytes it costs, on the map." },
  { title: "Execute", body: "Watch it run live, then pause, cancel or resume at any time." },
];

// Strategy names shown as chips.
export const strategies = [
  "Defragment files",
  "Hot zone",
  "Cold zone",
  "Directory clustering",
  "Type segregation",
  "Size placement",
  "Access-time sorting",
  "Free space consolidation",
  "Read-ahead packing",
];
