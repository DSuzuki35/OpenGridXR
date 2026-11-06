import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The dev server needs ws/http access to 127.0.0.1 for hot reload. The packaged
// app loads dist/index.html from disk and must not reach local services.
const DEV_CONNECT = " ws://127.0.0.1:* http://127.0.0.1:*";
function productionCsp() {
  return {
    name: "opengrid-production-csp",
    apply: "build",
    transformIndexHtml(html) {
      if (!html.includes(DEV_CONNECT)) throw new Error("index.html CSP changed; update vite.config.mjs.");
      return html.replace(DEV_CONNECT, "");
    },
  };
}

export default defineConfig({
  base: "./",
  plugins: [react(), productionCsp()],
  server: { host: "127.0.0.1" },
});
