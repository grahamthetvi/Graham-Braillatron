const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const form = document.getElementById('pair-form');
const pairingCard = document.getElementById('pairing-card');
const displayCard = document.getElementById('display-card');
const keyboardCard = document.getElementById('keyboard-card');
const focusWarning = document.getElementById('focus-warning');

let ws = null;
let paired = false;

function updateFocusStatus() {
  if (!paired) {
    if (focusWarning) focusWarning.style.display = 'none';
    return;
  }
  if (document.hasFocus()) {
    if (focusWarning) focusWarning.style.display = 'none';
  } else {
    if (focusWarning) focusWarning.style.display = 'block';
  }
}

window.addEventListener('focus', updateFocusStatus);
window.addEventListener('blur', updateFocusStatus);
document.addEventListener('click', updateFocusStatus);

// JS key code -> Linux evdev keycode mapping
const KEY_MAP = {
  'KeyF': 33,        // dot_1
  'KeyD': 32,        // dot_2
  'KeyS': 31,        // dot_3
  'KeyJ': 36,        // dot_4
  'KeyK': 37,        // dot_5
  'KeyL': 38,        // dot_6
  'ArrowUp': 103,    // dpad_up
  'ArrowDown': 108,  // dpad_down
  'Backspace': 14,   // backspace
  'Enter': 28,       // enter
  'Backquote': 41,   // menu
  'Tab': 15,         // shift_tts
  'ControlRight': 126, // speech
  'OSRight': 126,     // speech
  'MetaRight': 126    // speech
};

// Map from Linux keycode back to button elements for highlighting
const BUTTONS_MAP = {};

function setStatus(text) {
  statusEl.textContent = text;
}

function drawFrame(buffer) {
  if (buffer.byteLength < 17) {
    return;
  }
  const view = new DataView(buffer);
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
  if (magic !== 'BRDF') {
    return;
  }
  const width = view.getUint16(9, true);
  const height = view.getUint16(11, true);
  const offset = 17;
  const imageData = ctx.createImageData(width, height);
  for (let i = 0; i < width * height; ++i) {
    const rgb565 = view.getUint16(offset + i * 2, true);
    const r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
    const g = ((rgb565 >> 5) & 0x3F) * 255 / 63;
    const b = (rgb565 & 0x1F) * 255 / 31;
    const px = i * 4;
    imageData.data[px] = r;
    imageData.data[px + 1] = g;
    imageData.data[px + 2] = b;
    imageData.data[px + 3] = 255;
  }
  canvas.width = width;
  canvas.height = height;
  ctx.putImageData(imageData, 0, 0);
}

function sendKeyEvent(keycode, pressed) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({
      type: pressed ? 'keydown' : 'keyup',
      key: keycode
    }));
  }
}

function initKeyboard() {
  // Discover all virtual buttons
  document.querySelectorAll('[data-keycode]').forEach(btn => {
    const code = parseInt(btn.getAttribute('data-keycode'), 10);
    if (!BUTTONS_MAP[code]) {
      BUTTONS_MAP[code] = [];
    }
    BUTTONS_MAP[code].push(btn);

    // Mouse events
    const press = (e) => {
      e.preventDefault();
      btn.classList.add('active');
      sendKeyEvent(code, true);
    };

    const release = (e) => {
      e.preventDefault();
      btn.classList.remove('active');
      sendKeyEvent(code, false);
    };

    btn.addEventListener('mousedown', press);
    btn.addEventListener('mouseup', release);
    btn.addEventListener('mouseleave', release);

    // Touch events
    btn.addEventListener('touchstart', press);
    btn.addEventListener('touchend', release);
  });

  // Physical keyboard events
  window.addEventListener('keydown', (event) => {
    // Only capture if paired and target is not an input box
    if (!paired || event.target.tagName === 'INPUT') {
      return;
    }
    if (event.repeat) {
      return;
    }

    const code = KEY_MAP[event.code];
    if (code !== undefined) {
      event.preventDefault();
      
      // Highlight UI buttons
      if (BUTTONS_MAP[code]) {
        BUTTONS_MAP[code].forEach(btn => btn.classList.add('active'));
      }
      
      sendKeyEvent(code, true);
    }
  });

  window.addEventListener('keyup', (event) => {
    if (!paired || event.target.tagName === 'INPUT') {
      return;
    }

    const code = KEY_MAP[event.code];
    if (code !== undefined) {
      event.preventDefault();
      
      // Un-highlight UI buttons
      if (BUTTONS_MAP[code]) {
        BUTTONS_MAP[code].forEach(btn => btn.classList.remove('active'));
      }
      
      sendKeyEvent(code, false);
    }
  });
}

function showPanels(isPaired) {
  paired = isPaired;
  if (isPaired) {
    pairingCard.style.display = 'none';
    displayCard.style.display = 'flex';
    keyboardCard.style.display = 'flex';
    displayCard.classList.add('active');
    updateFocusStatus();
    window.focus();
  } else {
    pairingCard.style.display = 'flex';
    displayCard.style.display = 'none';
    keyboardCard.style.display = 'none';
    displayCard.classList.remove('active');
    updateFocusStatus();
  }
}

function connectSocket() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${location.host}/ws/frame`);
  ws.binaryType = 'arraybuffer';
  
  ws.onopen = () => {
    setStatus('Connected. Web inputs active.');
    showPanels(true);
  };
  
  ws.onmessage = (event) => {
    if (typeof event.data === 'string') {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === 'speak') {
          speakText(msg.text);
        }
      } catch (err) {
        console.error('Failed to parse text message:', err);
      }
    } else {
      drawFrame(event.data);
    }
  };
  
  ws.onclose = () => {
    setStatus('Disconnected. Reconnecting in 3s…');
    setTimeout(connectSocket, 3000);
  };
  
  ws.onerror = () => {
    setStatus('WebSocket error.');
  };
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const code = document.getElementById('code').value;
  setStatus('Authenticating pairing code...');
  
  try {
    const response = await fetch('/api/pair', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code }),
    });
    
    if (!response.ok) {
      let message = 'Pairing failed. Check code and try again.';
      try {
        const result = await response.json();
        if (result.error === 'rate_limited') {
          message = 'Too many failed attempts. SSH: sudo systemctl restart braillatron-displayd, then braillatron-show-pairing-code';
        } else if (result.error === 'expired') {
          message = 'Pairing code expired. Generate a new code on the Pi.';
        } else if (result.error === 'invalid_code') {
          message = 'Invalid pairing code. Generate a fresh code and try again.';
        }
      } catch (err) {
        // keep default message
      }
      setStatus(message);
      return;
    }
    
    connectSocket();
  } catch (err) {
    setStatus('Error connecting to pairing service.');
  }
});

// Start initialization
initKeyboard();

// Check initial status
fetch('/api/status')
  .then((response) => {
    if (response.ok) {
      connectSocket();
    } else {
      showPanels(false);
    }
  })
  .catch(() => {
    showPanels(false);
  });

function speakText(text) {
  if ('speechSynthesis' in window) {
    window.speechSynthesis.cancel();
    const utterance = new SpeechSynthesisUtterance(text);
    window.speechSynthesis.speak(utterance);
  }
}
