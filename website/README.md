# Ironclad Defrag website

Static product site built with Next.js (App Router, `output: "export"`), managed with pnpm (not npm).

- `pnpm install` then `pnpm dev` for a local preview at http://localhost:3000
- `pnpm build` writes the static site to `out/`; `pnpm start` serves that export
- Texts and links live in `lib/site.ts`; styles in `app/globals.css`
- Publishing: the `/deploy` skill builds this folder and uploads `out/`
