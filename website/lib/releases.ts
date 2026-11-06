// Published versions for the download links and the versions page. data/releases.json is kept by the /release skill
// (newest first); its installer fields are filled by deploy/record-release.ps1 once the installer is built.
import data from "@/data/releases.json";

// One published version: its changelog and the installer built for it.
export type Release = {
  version: string;
  date: string;
  changes: string[];
  commit?: string;
  file?: string;
  bytes?: number;
  sha256?: string;
};

// Only versions whose installer has been recorded: an entry still being prepared never shows a dead link.
export const releases: Release[] = (data as Release[]).filter((r) => r.file && r.sha256);

// The version the Download buttons offer.
export const latest: Release | undefined = releases[0];

// Where the server keeps every installer and its .sha256 file (never deleted, so old links keep working).
export function installerUrl(r: Release): string {
  return `/downloads/${r.file}`;
}

// Installer size for people, e.g. "4.5 MB".
export function formatSize(bytes = 0): string {
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

// Release date for people, e.g. "4 November 2026".
export function formatDate(iso: string): string {
  return new Date(`${iso}T00:00:00Z`).toLocaleDateString("en-GB", {
    day: "numeric",
    month: "long",
    year: "numeric",
    timeZone: "UTC",
  });
}
