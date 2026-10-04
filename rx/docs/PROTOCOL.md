# PS / UI 인터페이스

UART 115200/8N1, JSON 하나에 LF 하나(JSONL). 인터리브 없이 한 줄씩 전송합니다.

- rx: type, seq, frame_id, data(0..255), received_crc, crc_ok, overrun.
  UI는 crc_ok=true인 바이트를 문자로 표시합니다. 공백/CR/LF도 유지합니다.
  seq는 PS가 복사한 패킷의 번호이며 광송신 패킷 전체의 연속성을 뜻하지 않습니다.
- fft: type, seq, bins(128개의 unsigned 40비트 정수). 배열 index=bin입니다.
  UI 기본값은 DC 제외+선형 자동스케일이며 log10(1+power)도 선택할 수 있습니다.
  주파수 단위는 샘플링 주파수를 확정한 뒤 f=k*Fs/128로 변환해야 합니다.
- status: rx_init/fft_init 또는 누적 rx_queue_dropped/packet_overruns/
  rx_ack_timeouts/fft_timeouts. PL에서 CRC로 탈락한 모든 프레임 수를 보고하는 것은 아닙니다.

GPIO1 제어: bit0 캡처, bit1 해제, bit2 읽기, bits14:8 읽을 bin.
GPIO1 상태: bit0 ready, bit1 busy, bit2 스냅샷 유효, bit3 읽기 완료, bits14:8 반환 bin.
읽기 전 bit2를 낮추고 완료=0 확인 → bin과 bit2=1 쓰기 → 완료=1/반환bin 확인 →
GPIO2의 두 워드 읽기 → bit2 낮추기. 스냅샷 해제 전까지 저장값이 유지됩니다.
