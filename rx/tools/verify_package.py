from pathlib import Path
import hashlib,json,zipfile
r=Path(__file__).resolve().parents[1]
expected=json.loads((r/'SHA256SUMS.json').read_text())
bad=[]
for rel,digest in expected.items():
    f=r/rel
    if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest()!=digest:bad.append(rel)
with zipfile.ZipFile(r/'vivado/export/FFT_RX_FINAL.xsa') as z:
    bits=[n for n in z.namelist() if n.endswith('.bit')]
    assert len(bits)==1 and z.read(bits[0])==(r/'prebuilt/design_1_wrapper.bit').read_bytes()
elf=(r/'prebuilt/BFSK_RX_UART.elf').read_bytes()
assert b'fft_period_ms' in elf and b'rx_queue_dropped' in elf
if bad:raise SystemExit('Changed or missing files: '+', '.join(bad))
print('PASS:',len(expected),'checksums; XSA/BIT match; JSON+FFT ELF markers')
