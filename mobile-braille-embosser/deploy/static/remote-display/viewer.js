async function pair(code) {
  const res = await fetch('/api/pair', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code })
  });
  return res.ok;
}

function showViewer() {
  document.getElementById('login').style.display = 'none';
  document.getElementById('viewer').style.display = 'block';
  startStream();
}

document.getElementById('pair-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const code = document.getElementById('code').value.trim();
  const ok = await pair(code);
  if (!ok) {
    document.getElementById('error').textContent = 'Invalid or expired pairing code.';
    return;
  }
  showViewer();
});

document.getElementById('logout').addEventListener('click', async () => {
  await fetch('/api/logout', { method: 'POST' });
  location.reload();
});

async function checkSession() {
  const res = await fetch('/api/status');
  if (res.ok) {
    showViewer();
  }
}

function startStream() {
  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d');
  const ws = new WebSocket((location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws/frame');
  ws.binaryType = 'arraybuffer';
  ws.onmessage = (event) => {
    const view = new DataView(event.data);
    const width = view.getUint16(8, true);
    const height = view.getUint16(10, true);
    const offset = 16;
    const pixels = new Uint16Array(event.data, offset);
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const image = ctx.createImageData(width, height);
    for (let i = 0; i < pixels.length; ++i) {
      const rgb565 = pixels[i];
      const r = ((rgb565 >> 11) & 0x1f) * 255 / 31;
      const g = ((rgb565 >> 5) & 0x3f) * 255 / 63;
      const b = (rgb565 & 0x1f) * 255 / 31;
      const j = i * 4;
      image.data[j] = r;
      image.data[j + 1] = g;
      image.data[j + 2] = b;
      image.data[j + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
    document.getElementById('status').textContent = 'Live (' + width + 'x' + height + ')';
  };
  ws.onclose = () => {
    document.getElementById('status').textContent = 'Disconnected — reconnecting…';
    setTimeout(startStream, 2000);
  };
}

checkSession();
