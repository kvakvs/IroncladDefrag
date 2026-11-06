import type { Metadata } from "next";
import { formatDate, formatSize, installerUrl, releases } from "@/lib/releases";
import { site } from "@/lib/site";

export const metadata: Metadata = {
  title: `Downloads and changelog · ${site.name}`,
  description: `Every ${site.name} version with its installer, SHA-256 checksum and changelog.`,
};

// Every published version, newest first. Changelogs open on click (plain <details>, so no JavaScript is needed).
export default function Releases() {
  return (
    <>
      <header className="nav wrap">
        <a className="brand" href="/">
          <img src="/logo.png" alt="" width={28} height={28} />
          {site.name}
        </a>
        <nav>
          <a href="/#features">Features</a>
          <a href={site.sourceUrl}>Source</a>
        </nav>
      </header>

      <main className="wrap releases">
        <h1>Downloads</h1>
        <p className="section-lead left">
          Every version stays available. Installers are 64-bit, for Windows 10 and 11. Check a download against its
          SHA-256 with <code>Get-FileHash</code> in PowerShell.
        </p>

        {releases.length === 0 && <p>No versions have been published yet.</p>}

        <ol className="release-list">
          {releases.map((r, i) => (
            <li key={r.version} className="release panel" id={`v${r.version}`}>
              <div className="release-head">
                <div>
                  <h2>
                    Version {r.version}
                    {i === 0 && <span className="badge">Latest</span>}
                  </h2>
                  <p className="fine">
                    {formatDate(r.date)} · {formatSize(r.bytes)} · build {r.commit}
                  </p>
                </div>
                <a className={i === 0 ? "button" : "button ghost"} href={installerUrl(r)}>
                  Download {r.version}
                </a>
              </div>
              <p className="sha">
                SHA-256 <code>{r.sha256}</code>
              </p>
              <details open={i === 0}>
                <summary>Changelog</summary>
                <ul>
                  {r.changes.map((change) => (
                    <li key={change}>{change}</li>
                  ))}
                </ul>
              </details>
            </li>
          ))}
        </ol>
      </main>

      <footer className="wrap footer">
        <span>
          © {new Date().getFullYear()} {site.name}. Free and open source.
        </span>
        <a href={site.sourceUrl}>GitHub</a>
      </footer>
    </>
  );
}
