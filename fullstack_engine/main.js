const http = require('http');
const fs = require('fs');
const path = require('path');
const net = require('net');

const PORT = 8080;
const CPP_PORT = 9090;

let currentBundle = null;
const clients = new Set();

// Simple YAML parser to keep everything zero-dependency and fast
function parseYAML(yamlString) {
    const lines = yamlString.split('\n');
    let title = "Hexagen App";
    let theme = "default";
    const components = [];
    let currentComponent = null;

    for (let line of lines) {
        line = line.split('//')[0].split('#')[0].trim();
        if (!line) continue;

        if (line.startsWith('title:')) {
            title = line.substring(6).trim().replace(/^['"]|['"]$/g, '');
        } else if (line.startsWith('theme:')) {
            theme = line.substring(6).trim().replace(/^['"]|['"]$/g, '');
        } else if (line.startsWith('- type:')) {
            if (currentComponent) {
                components.push(currentComponent);
            }
            currentComponent = {
                type: line.substring(7).trim().replace(/^['"]|['"]$/g, '')
            };
        } else if (currentComponent) {
            const colonIdx = line.indexOf(':');
            if (colonIdx !== -1) {
                const key = line.substring(0, colonIdx).trim();
                const val = line.substring(colonIdx + 1).trim().replace(/^['"]|['"]$/g, '');
                currentComponent[key] = val;
            }
        }
    }
    if (currentComponent) {
        components.push(currentComponent);
    }

    return { title, theme, components };
}

function loadBundle() {
    try {
        const content = fs.readFileSync(path.join(__dirname, 'main.yaml'), 'utf8');
        currentBundle = parseYAML(content);
        console.log("✅ UI Bundle loaded successfully.");
    } catch (err) {
        console.error("❌ Error loading main.yaml:", err.message);
    }
}

// Watch main.yaml for changes
fs.watch(path.join(__dirname, 'main.yaml'), (eventType) => {
    if (eventType === 'change') {
        console.log("⚡ main.yaml changed! Reloading...");
        loadBundle();
        for (const client of clients) {
            client.write("data: reload\n\n");
        }
    }
});

loadBundle();

const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    if (req.url === '/' || req.url === '/index.html') {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        fs.createReadStream(path.join(__dirname, 'index.html')).pipe(res);
    } 
    else if (req.url === '/bundle') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(currentBundle));
    } 
    else if (req.url === '/events') {
        res.writeHead(200, {
            'Content-Type': 'text/event-stream',
            'Cache-Control': 'no-cache',
            'Connection': 'keep-alive'
        });

        clients.add(res);
        console.log(`🔌 SSE Client connected. Total: ${clients.size}`);

        req.on('close', () => {
            clients.delete(res);
            console.log(`🔌 SSE Client disconnected. Total: ${clients.size}`);
        });
    } 
    else if (req.url === '/execute' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => {
            body += chunk;
        });

        req.on('end', () => {
            console.log("📞 Forwarding request to C++ Core on port " + CPP_PORT + "...");

            const client = net.createConnection({ port: CPP_PORT, host: '127.0.0.1' }, () => {
                client.write(body);
            });

            client.on('data', (data) => {
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(data.toString());
                client.end();
            });

            client.on('error', (err) => {
                console.error("❌ C++ Core connection error:", err.message);
                res.writeHead(503, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({
                    status: "error",
                    message: "El Núcleo de C++ está inactivo. Levántalo con 'make run-core'."
                }));
            });
        });
    } 
    else {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('Not Found');
    }
});

server.listen(PORT, () => {
    console.log(`🚀 Node.js Orquestador dev server running at http://localhost:${PORT}`);
});
