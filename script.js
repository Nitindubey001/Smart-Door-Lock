const API_URL = 'http://172.26.57.104:3000/api';

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

// State Management
let currentState = 'LOCKED'; // Init
let pollingInterval = null;

// Initialization
function init() {
    addLog('System started', 'info');
    updateUI('LOCKED');
    
    // Start polling the server for state changes
    pollingInterval = setInterval(pollServer, 2000);
}

// Fetch the state from ESP/Backend
async function pollServer() {
    try {
        const res = await fetch(`${API_URL}/status`);
        if (!res.ok) throw new Error('Network response was not ok');
        const data = await res.json();
        
        // Only update UI if state changes, otherwise we'd rerender too much
        if (data.state !== currentState) {
            updateUI(data.state, data.otp);
            
            // Auto logging based on state change
            if (data.state === 'SOMEONE_AT_DOOR') {
                addLog('Visitor rang the doorbell! 🔔', 'warning');
            } else if (data.state === 'UNLOCKED') {
                addLog('Visitor entered correct OTP. Door Unlocked 🔓', 'success');
            } else if (data.state === 'LOCKED') {
                addLog('Door secured 🔒', 'info');
            }
        }
    } catch (error) {
        console.error('Polling error:', error);
    }
}

// Update the entire UI based on string state
function updateUI(stateName, otpVal = null) {
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
            devKeypad.style.display = 'block'; // Show dev keypad input
            break;

        case 'UNLOCKED':
            mainHeading.textContent = 'Door Unlocked';
            mainSubtext.textContent = 'The visitor entered the correct OTP. The servo has engaged to open the door.';
            btnLock.classList.remove('hidden');
            break;
    }
}

// Map string state to CSS class segment
function getStateClass(s) {
    if (s === 'LOCKED') return 'state-locked';
    if (s === 'SOMEONE_AT_DOOR') return 'state-visitor';
    if (s === 'OTP_GENERATED') return 'state-otp';
    if (s === 'UNLOCKED') return 'state-unlocked';
    return 'state-locked';
}

// Format time
function formatTime() {
    const now = new Date();
    let h = now.getHours();
    let m = now.getMinutes();
    let s = now.getSeconds();
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}

// Add event to DOM log
function addLog(msg, type = 'info') {
    const li = document.createElement('li');
    li.innerHTML = `<span>${msg}</span> <span class="time">${formatTime()}</span>`;
    
    if (type === 'warning') li.style.color = 'var(--warning)';
    if (type === 'success') li.style.color = 'var(--success)';
    
    logList.prepend(li); // add to top
}

// --- API ACTIONS ---

// Action: Generate OTP
async function generateOTP() {
    try {
        const res = await fetch(`${API_URL}/generate-otp`, { method: 'POST' });
        const data = await res.json();
        if (data.success) {
            addLog(`Triggered OTP SMS for visitor (+91 8982207277)`, 'info');
            // We do NOT updateUI here immediately; the polling loop will catch the change and animate gracefully.
            // But we can trigger an immediate poll to speed it up.
            pollServer();
        } else {
            alert(data.message);
        }
    } catch (e) {
        console.error(e);
    }
}

// Action: Lock Door (Resets State)
async function lockDoor() {
    try {
        const res = await fetch(`${API_URL}/lock`, { method: 'POST' });
        const data = await res.json();
        if (data.success) {
            pollServer();
        }
    } catch (e) {
        console.error(e);
    }
}

// Action: Simulate Doorbell (ESP Button Press)
async function simulateDoorbell() {
    try {
        await fetch(`${API_URL}/doorbell`, { method: 'GET' });
        pollServer();
    } catch (e) {
        console.error(e);
    }
}

// Action: Simulate ESP Keypad Entry
async function simulateESPKeypad() {
    const keypadInput = document.getElementById('sim-keypad-input').value;
    if (!keypadInput) return;
    
    try {
        const res = await fetch(`${API_URL}/verify-otp`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ otp: keypadInput })
        });
        const data = await res.json();
        
        if (data.success) {
             pollServer();
             document.getElementById('sim-keypad-input').value = '';
        } else {
            addLog(`Failed OTP attempt via simulated keypad`, 'warning');
            alert('Invalid OTP!');
        }
    } catch (e) {
        console.error(e);
    }
}

// Start app
window.onload = init;
