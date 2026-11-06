import type { NextConfig } from "next";

// Static export: `npm run build` writes plain HTML/CSS/JS to out/, served by any web server.
const nextConfig: NextConfig = {
  output: "export",
  trailingSlash: true,
  images: { unoptimized: true },
};

export default nextConfig;
