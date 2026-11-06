import DiskMap from "@/components/DiskMap";
import { formatDate, formatSize, installerUrl, latest } from "@/lib/releases";
import { features, site, stats, steps, strategies } from "@/lib/site";

// The Download buttons fetch the latest installer; before the first release they open the versions page.
const downloadUrl = latest ? installerUrl(latest) : site.releasesUrl;

export default function Home() {
  return (
    <>
      <header className="nav wrap">
        <a className="brand" href="#top">
          <img src="/logo.png" alt="" width={28} height={28} />
          {site.name}
        </a>
        <nav>
          <a href="#features">Features</a>
          <a href="#how">How it works</a>
          <a href={site.releasesUrl}>Versions</a>
          <a href={site.sourceUrl}>Source</a>
          <a className="button small" href={downloadUrl}>
            Download
          </a>
        </nav>
      </header>

      <main id="top">
        <section className="hero wrap">
          <div className="hero-text">
            <p className="eyebrow">Free · Open source · Windows 10 &amp; 11</p>
            <h1>
              Defragment with your eyes <span className="accent">wide open</span>.
            </h1>
            <p className="lead">
              {site.name} is a fast, free and open source defragmentation tool for Windows, for those who love great
              observability and control over their files.
            </p>
            <div className="actions">
              <a className="button" href={downloadUrl}>
                Download for Windows
              </a>
              <a className="button ghost" href={site.sourceUrl}>
                View source on GitHub
              </a>
            </div>
            <p className="fine">
              {latest && (
                <>
                  Version {latest.version} · {formatSize(latest.bytes)} · 64-bit ·{" "}
                </>
              )}
              <a href={site.releasesUrl}>All versions and changelog</a>
            </p>
          </div>
          <div className="hero-map panel">
            <DiskMap />
            <div className="legend">
              <span className="dot cell-a" /> Files
              <span className="dot cell-frag" /> Fragmented
              <span className="dot cell-free" /> Free
            </div>
          </div>
        </section>

        <section className="stats wrap" aria-label="Key numbers">
          {stats.map((s) => (
            <div key={s.label} className="stat panel">
              <strong>{s.value}</strong>
              <span>{s.label}</span>
            </div>
          ))}
        </section>

        <section id="features" className="wrap section">
          <h2>Everything on the map. Nothing moved by surprise.</h2>
          <p className="section-lead">{site.pitch}</p>
          <div className="features">
            {features.map((f) => (
              <article key={f.title} className="feature panel">
                <span className="feature-icon" aria-hidden="true">
                  {f.icon}
                </span>
                <h3>{f.title}</h3>
                <p>{f.body}</p>
              </article>
            ))}
          </div>
        </section>

        <section id="how" className="wrap section">
          <h2>Four steps, every one of them visible</h2>
          <ol className="steps">
            {steps.map((s, i) => (
              <li key={s.title} className="panel">
                <span className="step-number">{i + 1}</span>
                <h3>{s.title}</h3>
                <p>{s.body}</p>
              </li>
            ))}
          </ol>
          <div className="chips" aria-label="Move strategies">
            {strategies.map((name) => (
              <span key={name} className="chip">
                {name}
              </span>
            ))}
          </div>
        </section>

        <section className="wrap section">
          <div className="cta panel">
          <h2>Take a look at your drive</h2>
          <p>Analysis only reads, so the first look costs nothing. You decide what happens next.</p>
          {latest && (
            <p className="fine">
              Version {latest.version}, released {formatDate(latest.date)}. <a href={site.releasesUrl}>Changelog and older versions</a>
            </p>
          )}
          <div className="actions center">
            <a className="button" href={downloadUrl}>
              Download for Windows
            </a>
            <a className="button ghost" href={site.sourceUrl}>
              Star on GitHub
            </a>
          </div>
          </div>
        </section>
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
