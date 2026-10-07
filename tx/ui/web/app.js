const $ = id => document.getElementById(id);
const store = { get(k, d) { try { return JSON.parse(localStorage.getItem(k)) ?? d; } catch { return d; } },
                set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch {} } };

let status = null, mode = store.get('mode', 'aruco');
const chosen = store.get('stages', {});          // { mode: [stage id, ...] }
const busy = new WeakSet();

const selected = () => chosen[mode] ?? (status ? status.stages[mode].map(s => s.id) : []);

function refresh(img, url) {                     // 이전 요청이 끝나기 전에는 다시 요청하지 않는다
  if (busy.has(img)) return;
  busy.add(img);
  const next = new Image();
  next.onload = () => { img.src = next.src; busy.delete(img); };
  next.onerror = () => busy.delete(img);
  next.src = `${url}${url.includes('?') ? '&' : '?'}t=${Date.now()}`;
}

function buildStages() {
  const list = status.stages[mode], on = new Set(selected());
  $('stages').innerHTML = '';
  for (const s of list) {
    const label = document.createElement('label');
    label.innerHTML = '<input type="checkbox"><span></span><small></small>';
    label.querySelector('input').checked = on.has(s.id);
    label.querySelector('input').dataset.id = s.id;
    label.querySelector('span').textContent = s.label;
    label.querySelector('small').textContent = s.desc;
    $('stages').append(label);
  }
  buildPanel();
}

function buildPanel() {
  const panel = $('panel'), on = selected(), list = status.stages[mode].filter(s => on.includes(s.id));
  panel.innerHTML = '';
  $('panelEmpty').hidden = list.length > 0;
  for (const s of list) {
    const row = document.createElement('div');
    row.className = 'stagerow';
    row.innerHTML = '<h3></h3>' + (mode === 'aruco' ? '<div class="cellhead">' + Array.from({ length: status.cells }, (_, k) => `<span>cell ${k}</span>`).join('') + '</div>' : '') + '<img>';
    row.querySelector('h3').textContent = s.label;
    row.dataset.id = s.id;
    row.classList.toggle('cellrow', mode === 'aruco');
    panel.append(row);
  }
}

function setSelection(ids) {
  chosen[mode] = ids;
  store.set('stages', chosen);
  for (const box of $('stages').querySelectorAll('input')) box.checked = ids.includes(box.dataset.id);
  buildPanel();
}

$('stages').addEventListener('change', () =>
  setSelection([...$('stages').querySelectorAll('input:checked')].map(b => b.dataset.id)));
$('all').onclick = () => setSelection(status.stages[mode].map(s => s.id));
$('none').onclick = () => setSelection([]);
for (const [id, key] of [['showFrame', 'showFrame'], ['guide', 'guide']]) {
  $(id).checked = store.get(key, true);
  $(id).onchange = () => store.set(key, $(id).checked);
}
for (const r of document.querySelectorAll('input[name=mode]')) {
  r.checked = r.value === mode;
  r.onchange = async () => {
    mode = r.value;
    store.set('mode', mode);
    await fetch('/api/mode', { method: 'POST', body: JSON.stringify({ mode }) });
    await poll();
    buildStages();
  };
}

$('save').onclick = async () => {
  const r = await fetch('/api/capture', { method: 'POST', body: '{}' });
  const j = await r.json();
  $('saveMsg').textContent = r.ok ? `${j.files.length}개 파일 저장: ${j.dir}` : '저장할 프레임이 없습니다.';
};
$('dump').onclick = async () => {
  $('cnnText').textContent = await (await fetch(`/api/cnn.txt?cell=${$('cell').value}`)).text();
};

// 브라우저가 캡처보드를 열어 프레임을 서버로 보낸다 (서버에 --device 가 없을 때)
let stream = null, sending = false, sender = null;

async function listCameras() {
  try { (await navigator.mediaDevices.getUserMedia({ video: true })).getTracks().forEach(t => t.stop()); } catch {}
  const devices = (await navigator.mediaDevices.enumerateDevices()).filter(d => d.kind === 'videoinput');
  const keep = $('cameraSelect').value;
  $('cameraSelect').innerHTML = '<option value="">기본 영상 장치</option>' +
    devices.map((d, i) => `<option value="${d.deviceId}">${d.label || '영상 장치 ' + (i + 1)}</option>`).join('');
  $('cameraSelect').value = keep;
}

async function cameraStart() {
  cameraStop();
  const video = { width: { ideal: 1280 }, height: { ideal: 720 }, frameRate: { ideal: 30 } };
  if ($('cameraSelect').value) video.deviceId = { exact: $('cameraSelect').value };
  try { stream = await navigator.mediaDevices.getUserMedia({ video }); }
  catch (e) { $('health').textContent = `영상 장치를 열 수 없습니다: ${e.message}`; return; }
  $('video').srcObject = stream;
  await $('video').play();
  const canvas = document.createElement('canvas');
  sender = setInterval(async () => {
    const v = $('video');
    if (sending || !v.videoWidth) return;
    sending = true;
    canvas.width = v.videoWidth; canvas.height = v.videoHeight;
    canvas.getContext('2d').drawImage(v, 0, 0);
    const blob = await new Promise(r => canvas.toBlob(r, 'image/jpeg', 0.92));
    try { await fetch('/api/frame', { method: 'POST', body: blob }); } catch {}
    sending = false;
  }, 66);
}

function cameraStop() {
  clearInterval(sender);
  if (stream) stream.getTracks().forEach(t => t.stop());
  stream = null;
}

$('cameraRefresh').onclick = listCameras;
$('cameraStart').onclick = cameraStart;
$('cameraStop').onclick = cameraStop;

async function poll() {
  const first = status === null;
  status = await (await fetch('/api/status')).json();
  if (status.mode !== mode) {                    // 서버가 새로 떴을 때 저장한 모드로 맞춘다
    await fetch('/api/mode', { method: 'POST', body: JSON.stringify({ mode }) });
    status = await (await fetch('/api/status')).json();
  }
  $('camControls').hidden = status.kind !== 'browser';
  if (first) {
    if (status.kind === 'browser') listCameras();
    $('cell').innerHTML = Array.from({ length: status.cells }, (_, k) => `<option value="${k}">${k}</option>`).join('');
    buildStages();
  }
  $('source').textContent = status.source;
  $('fps').textContent = `${status.fps} fps`;
  $('size').textContent = status.frame ? `${status.frame[0]}×${status.frame[1]}` : '—';
  const a = status.aruco;
  $('aruco').textContent = mode === 'center' ? '사용 안 함' : a ? a.rc : '계산 대기';
  $('aruco').className = a && a.rc === 'OK' ? 'ok' : a ? 'bad' : '';
  $('markers').textContent = a ? `${a.markers.length}/6 (${a.markers.join(', ') || '—'})` : '—';
  $('fit').textContent = a ? `${a.fit_px}px / ${a.ms}ms` : '—';
  $('health').textContent = status.error ?? '';
}

function tick() {
  if (!status) return;
  const frame = $('frame').parentElement;
  frame.hidden = !$('showFrame').checked;
  if ($('showFrame').checked) refresh($('frame'), `/api/frame.jpg?guide=${$('guide').checked ? 1 : 0}`);
  for (const row of $('panel').children) refresh(row.querySelector('img'), `/api/stage/${row.dataset.id}.png`);
}

poll().then(() => { setInterval(poll, 700); setInterval(tick, 250); });
