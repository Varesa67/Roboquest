import { randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";
import { createServer } from "node:https";
import { WebSocket, WebSocketServer } from "ws";

function requiredEnvironment(name) {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Set the ${name} environment variable before starting the bridge.`);
  }
  return value;
}

function parseUrl(name, value, protocol) {
  let url;
  try {
    url = new URL(value);
  } catch {
    throw new Error(`${name} must be a valid URL.`);
  }
  if (url.protocol !== protocol || !url.hostname) {
    throw new Error(`${name} must use ${protocol}// and include a hostname.`);
  }
  return url;
}

const unoUrl = parseUrl(
  "UNO_WS_URL",
  requiredEnvironment("UNO_WS_URL"),
  "ws:",
);
const pagesOriginUrl = parseUrl(
  "GITHUB_PAGES_ORIGIN",
  requiredEnvironment("GITHUB_PAGES_ORIGIN"),
  "https:",
);
const allowedOrigin = pagesOriginUrl.origin;
const cert = readFileSync(requiredEnvironment("TLS_CERT_PATH"));
const key = readFileSync(requiredEnvironment("TLS_KEY_PATH"));
const port = Number(process.env.PORT || 8443);

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error("PORT must be an integer between 1 and 65535.");
}

const token = randomBytes(32).toString("hex");
const httpsServer = createServer({ cert, key }, (request, response) => {
  if (request.method === "GET" && request.url === "/health") {
    response.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("UNO R4 local WSS bridge is running.\n");
    return;
  }
  response.writeHead(404);
  response.end();
});
const webSocketServer = new WebSocketServer({ noServer: true, maxPayload: 25 });

httpsServer.on("upgrade", (request, socket, head) => {
  const reject = (status, message) => {
    if (!socket.destroyed) {
      socket.end(
        `HTTP/1.1 ${status} ${message}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`,
      );
    }
  };

  let requestUrl;
  try {
    requestUrl = new URL(request.url, "https://127.0.0.1");
  } catch {
    reject(400, "Bad Request");
    return;
  }

  if (requestUrl.pathname !== "/") {
    reject(404, "Not Found");
    return;
  }
  if (request.headers.origin !== allowedOrigin) {
    reject(403, "Forbidden");
    return;
  }
  if (requestUrl.searchParams.get("token") !== token) {
    reject(401, "Unauthorized");
    return;
  }

  let settled = false;
  const upstream = new WebSocket(unoUrl);
  const timeout = setTimeout(() => fail(504, "Gateway Timeout"), 10000);
  const fail = (status, message) => {
    if (settled) return;
    settled = true;
    clearTimeout(timeout);
    upstream.terminate();
    reject(status, message);
  };

  socket.once("close", () => {
    if (settled) return;
    settled = true;
    clearTimeout(timeout);
    upstream.close();
  });
  socket.once("error", () => {
    if (!settled) {
      settled = true;
      clearTimeout(timeout);
      upstream.terminate();
    }
  });
  upstream.once("open", () => {
    if (settled || socket.destroyed) {
      upstream.close();
      return;
    }
    settled = true;
    clearTimeout(timeout);
    webSocketServer.handleUpgrade(request, socket, head, (browserSocket) => {
      browserSocket.on("message", (data, isBinary) => {
        const message = data.toString();
        if (isBinary || !/^SIGN:[A-Za-z][A-Za-z -]{0,19}$/.test(message)) {
          browserSocket.close(1008, "Only valid SIGN messages are accepted.");
          return;
        }
        if (upstream.readyState === WebSocket.OPEN) {
          upstream.send(message);
        }
      });
      browserSocket.on("close", () => {
        if (upstream.readyState === WebSocket.OPEN) upstream.close();
        else upstream.terminate();
      });
      browserSocket.on("error", () => upstream.terminate());

      upstream.on("message", (data, isBinaryMessage) => {
        if (browserSocket.readyState === WebSocket.OPEN) {
          browserSocket.send(data, { binary: isBinaryMessage });
        }
      });
      upstream.once("close", () => {
        if (browserSocket.readyState === WebSocket.OPEN) {
          browserSocket.close(1011, "The Uno disconnected.");
        }
      });
      upstream.on("error", () => {
        if (browserSocket.readyState === WebSocket.OPEN) {
          browserSocket.close(1011, "Could not reach the Uno.");
        }
      });
    });
  });
  upstream.once("error", () => fail(502, "Bad Gateway"));
  upstream.once("close", () => fail(502, "Bad Gateway"));
});

httpsServer.listen(port, "127.0.0.1", () => {
  console.log(`[Bridge] Listening on https://127.0.0.1:${port}/`);
  console.log(`[Bridge] Allowed website origin: ${allowedOrigin}`);
  console.log(`[Bridge] Uno WebSocket: ${unoUrl.href}`);
  console.log(`[Bridge] Enter this URL in the website: wss://127.0.0.1:${port}/?token=${token}`);
});
