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
    label.innerHTML = '<input type="checkbox"><span></span>';
    label.querySelector('input').checked = on.has(s.id);
    label.querySelector('input').dataset.id = s.id;
    label.querySelector('span').textContent = s.label;
    label.title = s.desc;
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
    const head = mode === 'aruco' ? '<div class="cellhead">' + Array.from({ length: status.cells }, (_, k) => `<span>cell ${k}</span>`).join('') + '</div>' : '';
    row.innerHTML = `<h3></h3><div class="stagebox">${head}<img></div>`;
    row.querySelector('h3').textContent = s.label;
    row.dataset.id = s.id;
    row.querySelector('img').onload = layoutStages;
    panel.append(row);
  }
}

/*
 * 첫 화면: 왼쪽 이미지 전처리 | 오른쪽 촬영 화면 + 보드 CNN 인식. 두 열을 창 아래 끝까지 맞춘다.
 * 단계 이미지는 비율을 지키며 키운다. 왼쪽 카드 높이에 3줄이 들어가게 하고, 폭은 카드를 넘지 않는다.
 * 4줄 이상이면 왼쪽 카드 안에서 스크롤한다.
 */
const ROWS_PER_SCREEN = 3, ROW_GAP = 8, CARD_PAD = 16, TOP_MIN = 480, TOP_MARGIN = 14, NARROW = 850;

function layoutTop() {
  const top = $('top'), y = top.getBoundingClientRect().top + window.scrollY;
  top.style.setProperty('--top-h', `${Math.max(TOP_MIN, window.innerHeight - y - TOP_MARGIN)}px`);
  layoutStages();
}

function layoutStages() {
  const panel = $('panel'), rows = [...panel.children];
  if (!rows.length) return;
  const card = panel.closest('.card');
  const fit = Math.min(rows.length, ROWS_PER_SCREEN);
  // 좁은 화면(한 열)은 카드 높이가 고정되지 않으므로 창 높이를 쓴다
  const box = window.innerWidth > NARROW ? card.clientHeight : window.innerHeight - 2 * TOP_MARGIN;
  const head = panel.getBoundingClientRect().top - card.getBoundingClientRect().top + card.scrollTop;  // 카드 제목 부분
  const perRow = (box - head - CARD_PAD - (fit - 1) * ROW_GAP) / fit;
  for (const row of rows) {
    const img = row.querySelector('img');
    if (!img.naturalWidth) continue;
    const label = row.offsetHeight - img.offsetHeight;                                  // 단계 이름 + cell 이름
    const s = Math.min(panel.clientWidth / img.naturalWidth, (perRow - label) / img.naturalHeight);
    img.style.width = `${Math.max(1, Math.floor(img.naturalWidth * s))}px`;
  }
}
window.addEventListener('resize', layoutTop);
layoutTop();

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
  $('camNote').hidden = status.kind === 'browser';
  $('camNote').textContent = status.kind === 'device'
    ? '서버가 --device 로 캡처보드를 직접 열고 있어서 브라우저로 켜는 버튼은 없습니다. 브라우저에서 켜려면 서버를 --device 없이 다시 실행하세요.'
    : '저장한 프레임(--image)을 보여 주는 중이라 캡처보드를 켜는 버튼은 없습니다.';
  if (first) {
    // 저장해 둔 선택에 없던 새 단계는 자동으로 켠다 (단계가 추가돼도 예전 브라우저에서 보이게)
    const known = store.get('knownStages', {});
    for (const [m, list] of Object.entries(status.stages)) {
      const fresh = list.map(x => x.id).filter(id => !(known[m] ?? []).includes(id));
      if (chosen[m] && known[m] && fresh.length) chosen[m] = [...chosen[m], ...fresh];
      known[m] = list.map(x => x.id);
    }
    store.set('stages', chosen);
    store.set('knownStages', known);
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

// 보드 결과 (server.py --uart). 마지막 캡처 하나만 보이고 다음 캡처가 오면 바뀐다.
// 인식 카드는 글자만, 자세한 값은 보드 로그 카드에 둔다.
function failText(r) {
  if (r.stage === 'aruco') return `crop 실패: ${r.rc_name ?? r.rc}`;
  if (r.stage === 'timeout') return `cell ${r.cell} 응답 없음 (timeout)`;
  return `cell ${r.cell} DMA 시작 실패`;
}

function boardDetail(r) {
  const lines = [`캡처${r.seq != null ? ' #' + r.seq : ''} · ${r.at} · ${r.ok ? '성공' : failText(r)}`];
  if (r.e2e_us != null) lines.push(`end-to-end ${(r.e2e_us / 1000).toFixed(1)} ms (요청 -> 마지막 칸 결과)`);
  if (r.cells.length) {
    lines.push('', 'cell  PL  CPU  PL time    PL=CPU');     // 둘 다 보드에서 계산한 결과
    for (const c of r.cells) {
      const us = c.pl_us == null ? '     -   ' : `${String(c.pl_us).padStart(6)} us`;
      const same = c.cpu == null ? '-' : c.cpu === c.pl ? 'O' : 'X';
      lines.push(`${String(c.cell).padEnd(4)}  ${c.pl_char}   ${c.cpu_char ?? '-'}    ${us}   ${same}`);
    }
    if (r.cells.some(c => c.cpu == null)) lines.push('', 'CPU · 시간은 tx_capture 줄을 보내는 새 펌웨어에서만 나옵니다.');
  }
  return lines.join('\n');
}

function showBoardLog(b) {
  $('boardDetail').textContent = b.last ? boardDetail(b.last) : '아직 캡처 결과가 없습니다.';
  const log = $('boardLog'), atEnd = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
  log.textContent = b.log.join('\n') || '아직 받은 줄이 없습니다.';
  if (atEnd) log.scrollTop = log.scrollHeight;  // 위로 올려 읽는 중이면 그대로 둔다
}

async function showBoard() {
  const b = await (await fetch('/api/board')).json();
  const box = $('board');
  $('boardCapture').disabled = !b.enabled || !b.connected;
  if (!b.enabled) {
    $('boardSrc').textContent = '사용 안 함';
    box.textContent = '서버를 --uart COMx 로 실행하면 보드 결과를 읽습니다.';
    return;
  }
  $('boardSrc').textContent = `${b.port} · ${b.connected ? '연결됨' : '연결 안 됨'}`;
  showBoardLog(b);
  const r = b.last;
  box.innerHTML = '';
  if (!r || !r.cells.length) {
    box.textContent = b.error ?? (r ? failText(r) : '보드 버튼 또는 위 버튼으로 캡처하세요.');
    return;
  }
  box.style.gridTemplateColumns = `repeat(${r.cells.length}, minmax(0, 1fr))`;  // 칸 수만큼 한 줄
  for (const c of r.cells) {                     // 칸 번호 + 글자만
    const d = document.createElement('div');
    d.className = 'predcell';
    d.innerHTML = '<small></small><b></b>';
    d.querySelector('small').textContent = `cell ${c.cell}`;
    d.querySelector('b').textContent = c.pl_char;
    box.append(d);
  }
}

$('boardCapture').onclick = () => fetch('/api/board/key', { method: 'POST', body: JSON.stringify({ key: 'c' }) });

function tick() {
  if (!status) return;
  const frame = $('frame').parentElement;
  frame.hidden = !$('showFrame').checked;
  if ($('showFrame').checked) refresh($('frame'), `/api/frame.jpg?guide=${$('guide').checked ? 1 : 0}`);
  for (const row of $('panel').children) refresh(row.querySelector('img'), `/api/stage/${row.dataset.id}.png`);
}

poll().then(() => { setInterval(poll, 700); setInterval(tick, 250); setInterval(showBoard, 500); });
