const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;

// Application State
let doorState = 'LOCKED';
let currentOtp = null;

const MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'text/javascript',
    '.svg': 'image/svg+xml',
    '.json': 'application/json'
};

const OTP_API_KEY = '95a549d2e50908342cf9bdc3b769a142';
const OTP_PHONE = '918982207277';

const server = http.createServer((req, res) => {
    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    // Helper for sending JSON
    const sendJson = (status, data) => {
        res.writeHead(status, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(data));
    };

    // Body parser helper
    const parseBody = (callback) => {
        let body = '';
        req.on('data', chunk => {
            body += chunk.toString();
        });
        req.on('end', () => {
            try {
                callback(body ? JSON.parse(body) : {});
            } catch (e) {
                sendJson(400, { success: false, message: 'Invalid JSON' });
            }
        });
    };

    // Routing
    const url = new URL(req.url, `http://${req.headers.host}`);
    const pathname = url.pathname;

    // --- API ENDPOINTS ---
    if (pathname.startsWith('/api/')) {
        if (pathname === '/api/doorbell' && req.method === 'GET') {
            doorState = 'SOMEONE_AT_DOOR';
            currentOtp = null;
            return sendJson(200, { success: true, message: 'Doorbell triggered.' });
        }
        
        if (pathname === '/api/status' && req.method === 'GET') {
            return sendJson(200, { 
                state: doorState, 
                otp: doorState === 'OTP_GENERATED' ? currentOtp : null 
            });
        }
        
        if (pathname === '/api/generate-otp' && req.method === 'POST') {
            if (doorState === 'SOMEONE_AT_DOOR' || doorState === 'LOCKED') {
                return fetch('https://api.otp.dev/v1/verifications', {
                    method: 'POST',
                    headers: {
                        'X-OTP-Key': OTP_API_KEY,
                        'accept': 'application/json',
                        'content-type': 'application/json'
                    },
                    body: JSON.stringify({
                        data: {
                            channel: "sms",
                            sender: "896ee6b2-2a0b-4922-bbd9-39cafc99140b", 
                            phone: OTP_PHONE,
                            template: "a7fe47ae-ccc8-4ffb-adaa-15d557704036",
                            code_length: 4
                        }
                    })
                })
                .then(res => res.json())
                .then(apiRes => {
                    doorState = 'OTP_GENERATED';
                    currentOtp = null; // We no longer know the real OTP!
                    sendJson(200, { success: true, message: 'OTP sent via SMS API.' });
                })
                .catch(err => {
                    sendJson(500, { success: false, message: 'External API Error' });
                });
            } else {
                return sendJson(400, { success: false, message: 'Cannot generate OTP.' });
            }
        }
        
        if (pathname === '/api/verify-otp' && req.method === 'POST') {
            return parseBody((body) => {
                const otp = body.otp || url.searchParams.get('otp');
                if (doorState === 'OTP_GENERATED' && otp) {
                    fetch(`https://api.otp.dev/v1/verifications?code=${otp}&phone=${OTP_PHONE}`, {
                        method: 'GET',
                        headers: {
                            'X-OTP-Key': OTP_API_KEY,
                            'accept': 'application/json'
                        }
                    })
                    .then(res => res.json())
                    .then(apiRes => {
                        if (apiRes && apiRes.data && apiRes.data.length > 0) {
                            doorState = 'UNLOCKED';
                            currentOtp = null;
                            sendJson(200, { success: true, message: 'Unlocked.' });
                        } else {
                            sendJson(401, { success: false, message: 'Invalid OTP.' });
                        }
                    })
                    .catch(err => {
                        sendJson(500, { success: false, message: 'External API Error' });
                    });
                } else {
                    return sendJson(401, { success: false, message: 'No OTP generated or missing.' });
                }
            });
        }

        if (pathname === '/api/lock' && req.method === 'POST') {
            doorState = 'LOCKED';
            currentOtp = null;
            return sendJson(200, { success: true, message: 'Locked.' });
        }

        return sendJson(404, { success: false, message: 'API not found' });
    }

    // --- STATIC FILES ---
    let safePath = pathname === '/' ? '/index.html' : pathname;
    const ext = path.extname(safePath) || '.html';
    const filePath = path.join(__dirname, 'public', safePath);

    fs.readFile(filePath, (err, content) => {
        if (err) {
            if (err.code === 'ENOENT') {
                res.writeHead(404);
                res.end('404 Not Found');
            } else {
                res.writeHead(500);
                res.end('Server Error: ' + err.code);
            }
        } else {
            res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'text/plain' });
            res.end(content, 'utf-8');
        }
    });
});

server.listen(PORT, () => {
    console.log(`Smart Door Server running at http://localhost:${PORT}`);
});
