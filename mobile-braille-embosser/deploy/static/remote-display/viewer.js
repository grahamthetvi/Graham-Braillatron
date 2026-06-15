const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const form = document.getElementById('pair-form');
let ws = null;

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

function connectSocket() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${location.host}/ws/frame`);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => setStatus('Connected. Waiting for frames…');
  ws.onmessage = (event) => drawFrame(event.data);
  ws.onclose = () => {
    setStatus('Disconnected. Reconnecting in 3s…');
    setTimeout(connectSocket, 3000);
  };
  ws.onerror = () => setStatus('WebSocket error.');
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  const code = document.getElementById('code').value;
  const response = await fetch('/api/pair', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code }),
  });
  if (!response.ok) {
    setStatus('Pairing failed. Check the code and try again.');
    return;
  }
  form.hidden = true;
  connectSocket();
});

fetch('/api/status')
  .then((response) => {
    if (response.ok) {
      form.hidden = true;
      connectSocket();
    }
  })
  .catch(() => {});
