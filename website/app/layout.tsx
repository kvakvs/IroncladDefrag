import type { Metadata } from "next";
import { Inter, Space_Grotesk } from "next/font/google";
import Background from "@/components/Background";
import { site } from "@/lib/site";
import "./globals.css";

// Body text font.
const inter = Inter({ subsets: ["latin"], variable: "--font-body" });
// Heading font, a little more character than the body.
const grotesk = Space_Grotesk({ subsets: ["latin"], variable: "--font-head" });

export const metadata: Metadata = {
  title: `${site.name}: ${site.tagline}`,
  description: site.pitch,
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className={`${inter.variable} ${grotesk.variable}`}>
      <body>
        <Background />
        {children}
      </body>
    </html>
  );
}
