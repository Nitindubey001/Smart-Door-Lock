const http = require('http');
const https = require('https'); // For external API calls
const fs = require('fs');
const path = require('path');

const PORT = 3000;

// Application State
let doorState = 'LOCKED';
let commandQueue = [];
let sseClients = [];
let otpAttempts = 0; // Tracks invalid OTP attempts

const OTP_API_KEY = "95a549d2e50908342cf9bdc3b769a142";
const OTP_PHONE = "918982207277";

const MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'text/javascript',
    '.svg': 'image/svg+xml',
    '.json': 'application/json'
};

function broadcastState(state) {
    doorState = state;
    const msg = `data: ${JSON.stringify({ type: 'state', payload: state })}\n\n`;
    sseClients.forEach(client => client.write(msg));
}

function requestOTP(callback) {
    const payload = JSON.stringify({
        data: {
            channel: "sms",
            sender: "896ee6b2-2a0b-4922-bbd9-39cafc99140b",
            phone: OTP_PHONE,
            template: "a7fe47ae-ccc8-4ffb-adaa-15d557704036",
            code_length: 4
        }
    });

    const options = {
        hostname: 'api.otp.dev',
        path: '/v1/verifications',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'accept': 'application/json',
            'X-OTP-Key': OTP_API_KEY,
            'Content-Length': Buffer.byteLength(payload)
        }
    };

    const req = https.request(options, res => {
        let data = '';
        res.on('data', chunk => data += chunk);
        res.on('end', () => {
            if (res.statusCode === 200 || res.statusCode === 201) {
                callback(true);
            } else {
                console.error("OTP Error:", data);
                callback(false);
            }
        });
    });
    req.on('error', e => {
        console.error("OTP Request failed:", e.message);
        callback(false);
    });
    req.write(payload);
    req.end();
}

function verifyOTP(otp, callback) {
    const options = {
        hostname: 'api.otp.dev',
        path: `/v1/verifications?code=${otp}&phone=${OTP_PHONE}`,
        method: 'GET',
        headers: {
            'accept': 'application/json',
            'X-OTP-Key': OTP_API_KEY
        }
    };

    const req = https.request(options, res => {
        let data = '';
        res.on('data', chunk => data += chunk);
        res.on('end', () => {
            if (res.statusCode === 200 && data.includes('message_id')) {
                callback(true);
            } else {
                console.error("Verify Error:", data);
                callback(false);
            }
        });
    });
    req.on('error', e => {
        console.error("Verify OTP Request failed:", e.message);
        callback(false);
    });
    req.end();
}

const server = http.createServer((req, res) => {
    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }

    const sendJson = (status, data) => { res.writeHead(status, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(data)); };
    const parseBody = callback => {
        let body = '';
        req.on('data', chunk => body += chunk.toString());
        req.on('end', () => { try { callback(body ? JSON.parse(body) : {}); } catch(e) { sendJson(400, { success: false, message: 'Invalid JSON' }); } });
    };

    const url = new URL(req.url, `http://${req.headers.host}`);
    const pathname = url.pathname;

    // 1. SSE Endpoint
    if (pathname === '/api/events' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', 'Connection': 'keep-alive' });
        res.write(`data: ${JSON.stringify({ type: 'state', payload: doorState })}\n\n`);
        sseClients.push(res);
        req.on('close', () => sseClients = sseClients.filter(c => c !== res));
        return;
    }
    
    // 2. Web App Sends a Command
    if (pathname === '/api/web-command' && req.method === 'POST') {
        return parseBody((body) => {
            if (!body.command) return sendJson(400, { success: false, message: 'Missing command' });
            
            const cmd = body.command;
            
            if (cmd === 'GENERATE_OTP') {
                console.log("[SERVER] Generating OTP via API...");
                requestOTP((success) => {
                    if (success) {
                        otpAttempts = 0; // Reset attempts whenever a new OTP is sent
                        commandQueue.push('WAITING_FOR_OTP'); // Alert ESP32 to read Serial
                        broadcastState('OTP_GENERATED');
                        sendJson(200, { success: true });
                    } else {
                        broadcastState('LOCKED');
                        sendJson(500, { success: false });
                    }
                });
            } else if (cmd.startsWith('KEYPAD:')) {
                const otpCode = cmd.split(':')[1];
                console.log("[SERVER] Verifying OTP:", otpCode);
                verifyOTP(otpCode, (success) => {
                    if (success) {
                        broadcastState('UNLOCKED');
                        commandQueue.push('UNLOCK'); // Tell ESP32 to unlock
                        sendJson(200, { success: true });
                        
                        // Auto lock the dashboard back after 5 seconds
                        setTimeout(() => { broadcastState('LOCKED'); }, 5000);
                    } else {
                        broadcastState('SOMEONE_AT_DOOR');
                        sendJson(401, { success: false });
                    }
                });
            } else {
                console.log("[SERVER] Queued raw command for ESP32:", cmd);
                commandQueue.push(cmd);
                sendJson(200, { success: true });
            }
        });
    }

    // 3. ESP32 Polls for Commands
    if (pathname === '/api/esp/poll' && req.method === 'GET') {
        if (commandQueue.length > 0) { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end(commandQueue.shift()); }
        else { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end("NONE"); }
        return;
    }

    // 4. ESP32 Publishes State Change
    if (pathname === '/api/esp/state' && req.method === 'POST') {
        return parseBody((body) => {
            if (body.state) {
                console.log("[SERVER] State changed by ESP32 to:", body.state);
                broadcastState(body.state);
                res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end("OK");
            } else {
                res.writeHead(400, { 'Content-Type': 'text/plain' }); res.end("Missing state");
            }
        });
    }

    // 5. ESP32 Verifies OTP
    if (pathname === '/api/esp/verify' && req.method === 'POST') {
        return parseBody((body) => {
            if (body.otp) {
                console.log("[SERVER] Verifying OTP from ESP:", body.otp);
                verifyOTP(body.otp, (success) => {
                    if (success) {
                        otpAttempts = 0; // Reset
                        broadcastState('UNLOCKED');
                        commandQueue.push('UNLOCK'); // Tell ESP32 to unlock
                        res.writeHead(200, { 'Content-Type': 'text/plain' });
                        res.end("VERIFIED");
                        
                        setTimeout(() => { broadcastState('LOCKED'); }, 5000);
                    } else {
                        otpAttempts++;
                        console.log(`[SERVER] Invalid OTP attempt ${otpAttempts}/3`);
                        
                        if (otpAttempts >= 3) {
                            broadcastState('LOCKED'); // Revert dashboard immediately
                            res.writeHead(401, { 'Content-Type': 'text/plain' });
                            res.end("MAX_TRIES");
                        } else {
                            // Stay in WAITING_FOR_OTP/OTP_GENERATED state
                            res.writeHead(401, { 'Content-Type': 'text/plain' });
                            res.end("INVALID");
                        }
                    }
                });
            } else {
                res.writeHead(400, { 'Content-Type': 'text/plain' });
                res.end("Missing otp");
            }
        });
    }

    // Static routing
    if (pathname.startsWith('/api/')) return sendJson(404, { success: false, message: 'API not found' });
    let safePath = pathname === '/' ? '/index.html' : pathname;
    const ext = path.extname(safePath) || '.html';
    const filePath = path.join(__dirname, 'public', safePath);
    fs.readFile(filePath, (err, content) => {
        if (err) { res.writeHead(404); res.end('404 Not Found'); } 
        else { res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'text/plain' }); res.end(content, 'utf-8'); }
    });
});

server.listen(PORT, () => {
    console.log(`Ngrok Local Server running at http://localhost:${PORT}`);
});
