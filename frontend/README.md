# Frontend

The web interface is a single-page application built with [Preact](https://preactjs.com/) and [Bootstrap 5](https://getbootstrap.com/) (dark theme), bundled by [Vite](https://vite.dev/).

It provides an interface for managing characters — viewing and editing chat history and relationships. The app authenticates via a one-time password obtained in-game (`.chars web`), then connects to the server via REST API and WebSocket for real-time updates.

Make sure that you have node.js version 24 or higher before trying to build the frontend.

## Building

```bash
npm install
npm run build
```

The production build outputs to `frontend/dist/`. The C++ server serves these files when `PBC.HttpServerFrontendPath` is configured.

## Development

For live development with hot module replacement:

```bash
npm run dev
```

This starts Vite's dev server (default: `http://localhost:5173`) with a proxy that forwards `/api` and `/ws` requests to the C++ server. Configure the proxy targets via environment variables in a `.env` file (see `.env.example`).
