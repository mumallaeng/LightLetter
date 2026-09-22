# Power RTL 및 입력 데이터

샘플링 주파수 **160 kHz**, FFT 크기 **128point** 기준입니다.
빈 간격은 `160000 / 128 = 1250 Hz`이며, `bin = 주파수 / 1250`입니다.

| 입력 파일 | 주파수 | 양의 주파수 bin | 대칭 bin | 통신 의미 |
|---|---:|---:|---:|---|
| `fft_input_10000hz.mem` | 10 kHz | 8 | 120 | BIT0: 데이터 0 |
| `fft_input_20000hz.mem` | 20 kHz | 16 | 112 | BIT1: 데이터 1 |
| `fft_input_25000hz.mem` | 25 kHz | 20 | 108 | SYNC: 동기 신호 |

각 파일은 Python으로 생성한 모사 ADC 데이터 128개입니다. 한 줄은 40비트 16진수이며 `[39:20]=signed Re`, `[19:0]=signed Im`입니다. 이 세 파일은 실수 사인파 입력이므로 Im은 0입니다. 실수 입력의 FFT에는 양·음 주파수 성분이 함께 나타나므로 대칭 bin에도 큰 Power가 나옵니다.

## Power 출력

`power.v`는 FFT 결과의 `Re² + Im²`를 계산하여 unsigned 40비트 `power_result`로 출력합니다. 유효 입력을 받는 상승 에지 이후 결과와 `fft_mag_valid`가 함께 갱신됩니다. 출력은 0/1/SYNC 판정값이 아니라 각 bin의 Power이며, 심볼 판정은 디코더가 담당합니다. 프레임 메모리와 10초 주기 제어는 포함하지 않습니다.

## 테스트

`tb_power_0922.v`의 `ADC_FILE`을 위 파일명으로 변경하여 시험합니다. 현재 기본값은 20 kHz입니다. TB는 ADC 입력으로 FFT 결과를 만든 뒤 Power 결과 128개와 출력 valid를 검사하고 CSV에 저장합니다. Power 검사의 기대값은 FFT 출력에서 계산하며, FFT 자체의 정답이나 심볼 판정은 검사하지 않습니다.

TB 실행에는 외부의 `butterfly.v`, `multiplier.v`, `twiddle_rom.v`, `twiddle_128_q14.mem`이 별도로 필요합니다. 이 브랜치에는 포함하지 않습니다.
