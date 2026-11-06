// Abstract page backdrop: soft color fields under a faint grid of disk clusters. Purely decorative.
export default function Background() {
  return (
    <div className="backdrop" aria-hidden="true">
      <div className="blob blob-a" />
      <div className="blob blob-b" />
      <div className="blob blob-c" />
      <div className="blob blob-d" />
      <svg className="cluster-grid" width="100%" height="100%">
        <defs>
          <pattern id="clusters" width="22" height="22" patternUnits="userSpaceOnUse">
            <rect x="3" y="3" width="16" height="16" rx="3" />
          </pattern>
        </defs>
        <rect width="100%" height="100%" fill="url(#clusters)" />
      </svg>
    </div>
  );
}
