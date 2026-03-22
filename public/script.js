// Native SSE (Server-Sent Events) and Fetch API
// Completely removed MQTT Dependency!
let currentState = 'LOCKED';

// Auto-detect backend URL: If hosted on GitHub Pages or file:// fallback to Ngrok URL
let BACKEND_URL = '';
if (window.location.protocol === 'file:' || window.location.hostname.includes('github.io')) {
    BACKEND_URL = 'https://82ed-2409-40c4-184-cdb4-cd54-c2f8-19fe-7b61.ngrok-free.app';
}

// UI Elements
const iconWrapper = document.getElementById('icon-wrapper');
const mainHeading = document.getElementById('main-heading');
const mainSubtext = document.getElementById('main-subtext');
const btnAllow = document.getElementById('btn-allow');
const btnLock = document.getElementById('btn-lock');
const otpWrapper = document.getElementById('otp-wrapper');
const otpDigitsContainer = document.getElementById('otp-digits-container');
const logList = document.getElementById('log-list');
const mainPanel = document.getElementById('main-panel');
const devKeypad = document.getElementById('dev-keypad');

// Initialization
function init() {
    addLog('Connecting to Backend Server via SSE...', 'info');
    updateUI('LOCKED');
    
    // Connect to Server-Sent Events from local Node.js / Ngrok server
    const evtSource = new EventSource(`${BACKEND_URL}/api/events`);
    
    evtSource.onopen = function() {
        addLog('Connected to Backend Real-Time Stream', 'success');
    };
    
    evtSource.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'state') {
                const payload = data.payload;
                if (payload !== currentState) {
                    updateUI(payload);
                    
                    if (payload === 'SOMEONE_AT_DOOR') {
                        addLog('Visitor rang the doorbell! 🔔', 'warning');
                    } else if (payload === 'UNLOCKED') {
                        addLog('Visitor entered correct OTP. Door Unlocked 🔓', 'success');
                    } else if (payload === 'LOCKED') {
                        addLog('Door secured 🔒', 'info');
                    } else if (payload === 'OTP_GENERATED') {
                        addLog('ESP32 successfully dispatched OTP via SMS.', 'info');
                    }
                }
            }
        } catch(e) {
            console.error("Error parsing SSE data", e);
        }
    };

    evtSource.onerror = function(err) {
        addLog('SSE Connection lost! Retrying...', 'warning');
    };
}

// Map string state to CSS class segment
function getStateClass(s) {
    if (s === 'LOCKED') return 'state-locked';
    if (s === 'SOMEONE_AT_DOOR') return 'state-visitor';
    if (s === 'OTP_GENERATED') return 'state-otp';
    if (s === 'UNLOCKED') return 'state-unlocked';
    return 'state-locked';
}

function formatTime() {
    const now = new Date();
    let h = now.getHours();
    let m = now.getMinutes();
    let s = now.getSeconds();
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}

function addLog(msg, type = 'info') {
    const li = document.createElement('li');
    li.innerHTML = `<span>${msg}</span> <span class="time">${formatTime()}</span>`;
    if (type === 'warning') li.style.color = 'var(--warning)';
    if (type === 'success') li.style.color = 'var(--success)';
    logList.prepend(li);
}

// Update the entire UI based on string state
function updateUI(stateName) {
    currentState = stateName;
    
    // Reset Classes & Hidden
    mainPanel.className = 'glassmorphism card ' + getStateClass(stateName);
    btnAllow.classList.add('hidden');
    btnLock.classList.add('hidden');
    otpWrapper.classList.add('hidden');
    devKeypad.style.display = 'none';

    // Inject SVG
    const templateId = `svg-${getStateClass(stateName).split('-')[1]}`;
    const tpl = document.getElementById(templateId);
    if (tpl) {
        iconWrapper.innerHTML = '';
        iconWrapper.appendChild(tpl.content.cloneNode(true));
    }

    // Configure text and buttons
    switch (stateName) {
        case 'LOCKED':
            mainHeading.textContent = 'Secure & Locked';
            mainSubtext.textContent = 'Everything is quiet. No visitors detected.';
            break;

        case 'SOMEONE_AT_DOOR':
            mainHeading.textContent = 'Visitor at Door!';
            mainSubtext.textContent = 'Someone pressed the ESP doorbell. Would you like to grant access?';
            btnAllow.classList.remove('hidden');
            btnLock.classList.remove('hidden');
            break;

        case 'OTP_GENERATED':
            mainHeading.textContent = 'OTP Sent via SMS';
            mainSubtext.textContent = 'Awaiting visitor to enter the code on the keypad...';
            btnLock.classList.remove('hidden');
            otpWrapper.classList.remove('hidden');
            devKeypad.style.display = 'block';
            break;

        case 'UNLOCKED':
            mainHeading.textContent = 'Door Unlocked';
            mainSubtext.textContent = 'The visitor entered the correct OTP. The servo has engaged to open the door.';
            btnLock.classList.remove('hidden');
            break;
    }
}

// Helper to send commands to the Backend
async function sendCommand(cmdStr) {
    try {
        const res = await fetch(`${BACKEND_URL}/api/web-command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ command: cmdStr })
        });
        if (!res.ok) {
            addLog('Failed to send command to server.', 'warning');
            alert("Could not send command to the server! Make sure it is running.");
        }
    } catch(err) {
        addLog('Network Error sending command.', 'warning');
        console.error(err);
    }
}

// --- CLOUD ACTIONS ---

// Action: Generate OTP
function generateOTP() {
    addLog(`Sending authorize command to ESP32...`, 'info');
    sendCommand("GENERATE_OTP");
}

// Action: Lock Door (Resets State)
function lockDoor() {
    sendCommand("LOCK");
}

// Action: Simulate Doorbell (ESP Button Press)
function simulateDoorbell() {
    sendCommand("SIMULATE_BELL");
}

// Action: Simulate ESP Keypad Entry
function simulateESPKeypad() {
    const keypadInput = document.getElementById('sim-keypad-input').value;
    if (!keypadInput) return;
    
    // Instead of verifying on the web, send the keypad input straight to ESP32
    sendCommand("KEYPAD:" + keypadInput);
    document.getElementById('sim-keypad-input').value = '';
}

// Start app
window.onload = init;
