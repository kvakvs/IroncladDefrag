import DiskMap from "@/components/DiskMap";
import { features, site, stats, steps, strategies } from "@/lib/site";

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
          <a href={site.sourceUrl}>Source</a>
          <a className="button small" href={site.downloadUrl}>
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
              <a className="button" href={site.downloadUrl}>
                Download for Windows
              </a>
              <a className="button ghost" href={site.sourceUrl}>
                View source on GitHub
              </a>
            </div>
            <p className="fine">Version {site.version} · 64-bit · NTFS drives, HDD and SSD</p>
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
          <div className="actions center">
            <a className="button" href={site.downloadUrl}>
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
