const $=id=>document.getElementById(id);
const state={chars:[],sources:[],samples:JSON.parse(localStorage.getItem('lightletter-samples')||'[]'),bins:Array(128).fill(0),port:null,reader:null,stream:null,capture:null,count:0};

function showTab(){const n=location.hash==='#review'?'review':'receive';document.querySelectorAll('.tab,.view').forEach(e=>e.classList.remove('active'));document.querySelector(`.tab[data-tab="${n}"]`).classList.add('active');$(n).classList.add('active')}
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{location.hash=b.dataset.tab;showTab()});
window.onhashchange=showTab;showTab();

function drawBins(){
  const x=$('fftChart'),c=x.getContext('2d');
  const excludeDC=$('fftExcludeDC').checked,log=$('fftScale').value==='log';
  const plotted=state.bins.map((v,i)=>excludeDC&&i===0?0:v);
  const peak=Math.max(...plotted),map=v=>log?Math.log10(1+v):v;
  const maximum=Math.max(1,map(peak));
  const left=42,top=18,bottom=28,w=x.width-left-8,h=x.height-top-bottom;
  const rawSelected=Number($('fftBin').value);
  const selected=Number.isInteger(rawSelected)&&rawSelected>=0&&rawSelected<128?rawSelected:8;
  c.clearRect(0,0,x.width,x.height);
  c.strokeStyle='#b8c0c6';c.beginPath();c.moveTo(left,top);c.lineTo(left,top+h);c.lineTo(left+w,top+h);c.stroke();
  plotted.forEach((v,i)=>{
    if(excludeDC&&i===0)return;
    c.fillStyle=i===selected?'#1465a0':[8,16,20].includes(i)?'#268779':'#8f9aa4';
    const height=h*map(v)/maximum;
    c.fillRect(left+i*w/128,top+h-height,Math.max(1,w/128-1),height);
  });
  c.fillStyle='#56616a';c.font='11px Arial';c.textAlign='center';
  [0,32,64,96,127].forEach(i=>c.fillText(String(i),left+(i+0.5)*w/128,x.height-10));
  c.textAlign='left';c.fillText('0',12,top+h);c.fillText(log?'log10(1 + Power)':'Power',left,11);
  $('fftValue').textContent=`bin ${selected} · power ${state.bins[selected].toLocaleString('en-US')}${excludeDC&&selected===0?' (그래프에서 제외)':''}`;
  $('fftPeak').textContent=`표시 최대 power ${peak.toLocaleString('en-US')} · DC ${state.bins[0].toLocaleString('en-US')}${excludeDC?' 제외':' 포함'} · ${log?'로그':'선형'} 자동 스케일${peak===0?' · 표시 범위의 값이 모두 0입니다':''}`;
}
$('fftBin').oninput=()=>{const v=Number($('fftBin').value);if(Number.isInteger(v)&&v>=0&&v<128)drawBins()};
$('fftExcludeDC').onchange=drawBins;
$('fftScale').onchange=drawBins;
function render(){const word=state.chars.join('').replace(/\r\n?/g,'\n');$('word').textContent=word;$('reviewWord').textContent=word;$('count').textContent=`총 ${state.chars.length}개`;$('total').textContent=state.chars.length;$('latest').textContent=LightLetterProtocol.charLabel(state.chars.at(-1));$('latestSource').textContent=state.sources.at(-1)||'—';$('history').replaceChildren();if(!state.chars.length)$('history').innerHTML='<span class="muted">아직 수신된 문자가 없습니다.</span>';else state.chars.slice(-20).forEach((ch,i)=>{const d=document.createElement('div');d.className='item';d.textContent=LightLetterProtocol.charLabel(ch);if(ch.length&&ch.charCodeAt(0)<=32)d.classList.add('control-char');const s=document.createElement('small');s.textContent=state.chars.length-Math.min(20,state.chars.length)+i+1;d.append(s);$('history').append(d)});$('reviewHistory').replaceChildren(...Array.from($('history').children,e=>e.cloneNode(true)));$('samples').replaceChildren();if(!state.samples.length)$('samples').innerHTML='<span class="muted">저장된 후보가 없습니다.</span>';else state.samples.forEach(s=>{const d=document.createElement('div'),i=document.createElement('img');d.className='samplecard';i.src=s.png;i.alt='28×28 이미지';d.append(i,document.createTextNode(`${s.label} · 예측 ${s.predicted} · ${s.id}`));$('samples').append(d)});drawBins()}

function rtlPreview(video){const w=video.videoWidth,h=video.videoHeight,src=document.createElement('canvas');src.width=w;src.height=h;const sc=src.getContext('2d',{willReadFrequently:true});sc.drawImage(video,0,0,w,h);const input=sc.getImageData(0,0,w,h),side=Math.min(w,h),left=Math.floor((w-side)/2),top=Math.floor((h-side)/2),out=$('preview'),oc=out.getContext('2d'),image=oc.createImageData(28,28),pixels=[];for(let y=0;y<28;y++)for(let x=0;x<28;x++){const sx=left+Math.floor(((2*x+1)*side)/56),sy=top+Math.floor(((2*y+1)*side)/56),si=(sy*w+sx)*4,r=input.data[si],g=input.data[si+1],b=input.data[si+2],luma=(77*r+150*g+29*b+128)>>8,inv=255-luma,p=inv<100?0:inv,di=(y*28+x)*4;image.data[di]=image.data[di+1]=image.data[di+2]=p;image.data[di+3]=255;pixels.push(p)}oc.putImageData(image,0,0);return pixels}
async function capturePhoto(source='수동 미리보기'){const v=$('video');if(!state.stream||!v.videoWidth){$('reviewMessage').textContent=`${source} 이벤트를 받았지만 캡처보드 영상이 켜져 있지 않습니다.`;return}const pixels=rtlPreview(v),id=`capture-${String(++state.count).padStart(4,'0')}`;state.capture={id,pixels,png:$('preview').toDataURL('image/png'),time:new Date().toISOString(),source,preprocess:'rtl-center720-nearest28-luma-invert-threshold100'};$('captureId').value=id;$('predicted').value='';$('sampleStatus').textContent=`${source} · RTL 기준 28×28 미리보기 완료`;$('reviewMessage').textContent=`${id}: FPGA CNN 결과 대기 중`}

async function receive(raw){
  $('raw').textContent=JSON.stringify(raw);
  $('receivedAt').textContent=new Date().toLocaleString('ko-KR');
  const o=LightLetterProtocol.decodeEvent(raw);
  if(!o){$('health').textContent='형식 오류 또는 검증 실패 이벤트 제외';return}
  if(o.type==='status'){
    if(o.state==='ready')$('health').textContent=`보드 연결 · RX ${o.rx_init===0?'준비':'초기화 실패'} · FFT ${o.fft_init===0?'준비':'초기화 실패'}`;
    else $('health').textContent=`큐 유실 ${o.rx_queue_dropped||0} · 패킷 덮어쓰기 ${o.packet_overruns||0} · 수신 응답 지연 ${o.rx_ack_timeouts||0} · FFT 응답 지연 ${o.fft_timeouts||0}`;
    return;
  }
  if(o.type==='capture'){await capturePhoto('FPGA 버튼');return}
  if(o.type==='fft'){
    state.bins=o.bins;
    $('fftMeta').textContent=`스냅샷 ${o.seq ?? '—'} · ${new Date().toLocaleTimeString('ko-KR')} · 128 bins`;
    drawBins();return;
  }
  if(o.type==='rx'){
    state.chars.push(o.char);state.sources.push('UART/광통신 RX');
    if(o.overrun)$('health').textContent='PL 패킷 덮어쓰기 감지';
    render();return;
  }
  if(o.type==='recognition'){
    const ch=typeof o.char==='string'?o.char.toUpperCase():Number.isInteger(o.class_id)&&o.class_id>=0&&o.class_id<26?String.fromCharCode(65+o.class_id):'';
    if(!/^[A-Z]$/.test(ch)||o.crc_ok===false)return;
    state.chars.push(ch);state.sources.push('UART/FPGA CNN');
    if(state.capture){state.capture.predicted=ch;$('predicted').value=ch;$('sampleStatus').textContent=`FPGA CNN 예측: ${ch}`}render();
  }
}

$('connect').onclick=async()=>{
  if(!('serial' in navigator)){alert('Chrome/Edge의 Web Serial이 필요합니다.');return}
  if(state.port)return;
  $('connect').disabled=true;
  try{
    state.port=await navigator.serial.requestPort();
    await state.port.open({baudRate:115200});
    $('serialStatus').textContent='연결됨';
    state.reader=state.port.readable.getReader();
    const decoder=new TextDecoder(),lines=new LightLetterProtocol.Lines();
    while(state.reader){
      const {value,done}=await state.reader.read();if(done)break;
      for(const line of lines.push(decoder.decode(value,{stream:true}))){
        if(!line.startsWith('{')){$('raw').textContent=line.slice(0,500);continue}
        try{await receive(JSON.parse(line))}catch(e){$('raw').textContent=`JSON 오류: ${e.message}`}
      }
    }
  }catch(e){$('raw').textContent=String(e)}
  finally{
    state.reader?.releaseLock();state.reader=null;
    try{await state.port?.close()}catch{}
    state.port=null;$('serialStatus').textContent='연결 안 됨';$('connect').disabled=false;
  }
};
$('disconnect').onclick=async()=>{try{await state.reader?.cancel()}catch{}};
$('undo').onclick=()=>{state.chars.pop();state.sources.pop();render()};$('clear').onclick=()=>{state.chars=[];state.sources=[];render()};

async function refreshCameras(){const old=$('cameraSelect').value,devices=(await navigator.mediaDevices.enumerateDevices()).filter(d=>d.kind==='videoinput');$('cameraSelect').replaceChildren(new Option('기본 영상 장치',''));devices.forEach((d,i)=>$('cameraSelect').add(new Option(d.label||`영상 장치 ${i+1}`,d.deviceId)));if(devices.some(d=>d.deviceId===old))$('cameraSelect').value=old}
async function startCamera(){if(state.stream)return;const id=$('cameraSelect').value;state.stream=await navigator.mediaDevices.getUserMedia({video:id?{deviceId:{exact:id},width:{ideal:1280},height:{ideal:720}}:{width:{ideal:1280},height:{ideal:720}},audio:false});const v=$('video');v.srcObject=state.stream;v.style.display='block';v.onloadedmetadata=()=>{v.closest('.camera').style.aspectRatio=`${v.videoWidth}/${v.videoHeight}`;$('cameraEmpty').style.display='none'};await v.play();await refreshCameras()}
function stopCamera(){state.stream?.getTracks().forEach(t=>t.stop());state.stream=null;$('video').srcObject=null;$('video').style.display='none';$('cameraEmpty').style.display='block'}
$('cameraStart').onclick=()=>startCamera().catch(e=>$('reviewMessage').textContent=`캡처보드 연결 실패: ${e.message}`);$('cameraStop').onclick=stopCamera;$('cameraRefresh').onclick=()=>refreshCameras().catch(()=>{});$('cameraSelect').onchange=async()=>{if(state.stream){stopCamera();await startCamera()}};$('capture').onclick=()=>capturePhoto();refreshCameras().catch(()=>{});

$('saveLabel').onclick=()=>{const label=$('label').value.trim().toUpperCase(),c=state.capture;if(!c||!c.predicted){$('reviewMessage').textContent='FPGA CNN 결과가 있는 캡처가 필요합니다.';return}if(!/^[A-Z]$/.test(label)||label===c.predicted){$('reviewMessage').textContent='예측과 다른 대문자 A–Z를 입력하세요.';return}if(state.samples.some(s=>s.id===c.id))return;state.samples.push({...c,label,predictionSource:'uart-fpga-cnn'});localStorage.setItem('lightletter-samples',JSON.stringify(state.samples));render()};
function download(name,blob){const u=URL.createObjectURL(blob),a=document.createElement('a');a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),1000)}
$('export').onclick=()=>download('lightletter-misclassifications.json',new Blob([JSON.stringify({schema:'lightletter-v1',samples:state.samples},null,2)],{type:'application/json'}));$('exportPng').onclick=()=>{if(state.capture)fetch(state.capture.png).then(r=>r.blob()).then(b=>download(`${state.capture.id}-28x28.png`,b))};
render();
