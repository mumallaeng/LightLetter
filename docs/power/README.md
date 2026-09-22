# Power RTL 및 검증 자료

## 구성
- `rtl/power/power.v`: `power_top`, `power`, `power_mem` 세 모듈.
- `tb/power/tb_power_0.v`, `tb_power_1.v`, `tb_power_sync.v`: 10/20/25 kHz 모사 입력 기반 FFT 및 Power 기준값 비교.
- `tb/power/tb_power_result.v`: Power 출력 128개 수집 및 CSV 저장. 연산 정답·bin 순서·정확한 지연은 검사하지 않음.
- `tb/power/data`: Python으로 생성한 signed ADC 모사 데이터 128개. 실제 XADC 측정값이 아님.

원본은 D:/0922_power의 사용자 프로젝트이다. 빈 power_mem.v 및 Vivado 생성 파일은 제외했다. RTL의 동작은 변경하지 않았다.

## 인터페이스
| 신호 | 방향 | 폭 | 의미 |
|---|---|---|---|
| clk | 입력 | 1 | RX와 동일한 상승 에지 클럭 |
| rst | 입력 | 1 | 동기식 active-high reset |
| i_fft_core_data | 입력 | 40 | [39:20]=Re, [19:0]=Im, 각각 signed 20비트 정수 |
| i_fft_core_valid | 입력 | 1 | 수집 상태에서 1인 상승 에지마다 결과 하나 저장 |
| fft_mag | 출력 | 40 | Re²+Im², unsigned, 입력 순서대로 출력 |
| fft_mag_valid | 출력 | 1 | 1인 상승 에지에서 RX가 현재 값을 수신 |

128개 저장 완료 후 출력한다. 송신 및 마지막 수신 마무리 상태에서는 새 입력을 받지 않는다. ready 포트가 없으므로 상위에서 프레임 간격을 보장해야 한다. 마지막 RX 수신 에지 이후 valid가 0으로 내려가며, 다음 상승 에지부터 새 프레임 입력을 저장할 수 있다. 메모리와 fft_mag는 리셋하지 않으므로 valid=0인 값은 무시한다. 조합 제곱합 결과가 저장 에지의 setup/hold를 만족하도록 해야 한다.

## TB 실행 의존성
Power RTL 합성에는 Butterfly가 필요하지 않지만, 포함된 네 TB는 ADC→FFT→Power 흐름을 사용하므로 아래 파일이 필요하다.

기존 `butterfly` 브랜치의 `rtl/fft/`에서 별도 준비:
- butterfly.v (모듈 이름 butterfly)
- multiplier.v
- twiddle_rom.v
- twiddle_128_q14.mem

테스트용 Butterfly 소스는 이 커밋에 복사하지 않았다. 의존성은 별도 checkout에서 준비하고 Vivado Simulation Sources에 추가한다. 기존 `fft_input_10000hz_clean.mem`은 복소 입력이므로 이 폴더의 `fft_input_10000hz.mem` 대용으로 사용하지 않는다.

Vivado에서 power.v를 Design Sources에 추가하고 합성 Top을 power_top으로 설정한다. TB와 외부 의존성, data 폴더의 입력 파일 및 twiddle 계수 파일을 Simulation Sources에 추가한다. 실행할 TB를 Simulation Top으로 지정하고 `run all`을 실행한다. TB가 FFT 단계·주소·메모리 제어를 모사하며 실제 FFT Top이나 RX decoder는 포함하지 않는다.

## 검증 범위
- tb_power_0: 최대 bin 8, Power 128개 기준값 일치.
- tb_power_1: 최대 bin 16, Power 128개 기준값 일치.
- tb_power_sync: 최대 bin 20, Power 128개 기준값 일치.
- tb_power_result: X/Z 없는 결과 128개 수신, 초과 출력 관찰, power_result.csv 생성.

기존 Vivado 2020.2 시뮬레이션에서 위 시험을 통과했다. 수집 TB 파형에서 59,090ns 마지막 수신 시 received=0x80 및 valid 하강을 확인했다. 이 시간은 해당 TB 스케줄에 대한 결과이며 FPGA 타이밍 보장이나 실제 RX 디코딩 검증을 뜻하지 않는다.

2026-09-22 게시 전 확인: 이 브랜치의 RTL/TB와 D:/power의 butterfly·multiplier·twiddle_rom 및 계수로 Vivado 2020.2에서 네 TB를 재실행하여 모두 PASS를 확인했다. 외부 의존성은 본 커밋에 포함하지 않았다.
