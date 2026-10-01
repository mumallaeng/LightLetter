# RX version 1

FFT Power 입력부터 CRC 검증까지 연결한 RX RTL 버전이다. UI buffer와 실제 FFT RTL은 포함하지 않는다.

## 심볼 판정

bin8=BIT0, bin16=BIT1, bin20=SYNC. 가장 큰 Power가 두 번째로 큰 Power보다 20% 초과로 커야 유효하다. `10 * Pmax > 12 * Psecond`를 만족하지 않으면 INVALID다. 정확히 20% 차이, 동률 및 모두 0도 INVALID다. 비교 계산은 MAG_W+4비트로 확장한다. 별도의 최소 Power threshold는 없다.

## 검증 결과

160kS/s, 128-point rectangular FFT, 256 samples/symbol, signed 12bit ADC 모델에서 실제 파형의 FFT 전체 bin을 Vivado XSim RX Top에 입력했다. 실제 FFT IP 고정소수점과 아날로그 회로는 모델링하지 않았다.

- 정상 단일 패킷 시작 offset128개 × 초기 위상16개: 정현파2048/2048 복원, 사각파2040/2048 복원.
- 사각파의 8/2048 실패(0.390625%)는 이 시험 격자에서의 비율이며 실제 환경 또는 모든 임의 파형의 실패 확률이 아니다.
- 각 시작 조건에서 동일 패킷4개 연속 전송: 정현파8192/8192, 사각파8160/8192 복원. 정상 첫 수신 조건은 모두 뒤의 세 패킷도 복원했다.
- 대표 정상 위치의 정현파 및 사각파1000개 연속 전송은 각각1000/1000 복원했다.
- 정상 위치의32개 연속 전송 중 하나에 SFD/CRC/중간 SYNC 오류를 넣은 시험은 해당 패킷만 거절하고 다음 패킷부터 복원했다.
- 연속 시험 전체4115시나리오,22136패킷,1630866 FFT블록을 검증했다. frame_valid로 출력된 패킷의 payload 불일치 및 중복 출력은 없었다.

## 알려진 제한

최초 동기화가 잘못된 FFT 블록 선택 위치로 고정되면 간격 없는 다음 패킷도 반복 누락할 수 있다. 대표 실패 위치의1000개 전송은 모두 SFD 수신 중 INVALID/abort로 거절됐다. 이 버전은 모든 초기 위치에서의 수신을 보장하지 않는다.

SYNC_MIN_BLOCKS=6이므로 SYNC3심볼도 수용할 수 있다. 입력 단절 timeout과 최소 신호 세기 판정은 없다. UI에서는 decode_valid가 아니라 CRC 통과 결과인 frame_valid를 기준으로 데이터를 저장해야 한다.

원본 샘플과 상세 결과는 프로젝트 로컬 sim/rx_waveform_20pct_validation 및 sim/rx_continuous_20pct_validation에 보관한다. 연속 시험에 이전 시험의 기능 coverage100%를 재사용하지 않는다.
