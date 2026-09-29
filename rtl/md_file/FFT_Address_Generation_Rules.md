# 128-Point Radix-2 DIT FFT 주소 생성 규칙

## 1. 문서 목적

이 문서는 128-point Radix-2 DIT FFT에서 다음 주소를 생성하는 원리와 Verilog 구현 방법을 정리한다.

- 입력 데이터를 저장할 Bit-Reversed 주소
- 각 FFT stage에서 Butterfly 입력 A/B를 읽고 쓸 주소
- 각 Butterfly 연산에 사용할 Twiddle ROM 주소
- FFT 결과를 Natural Order로 출력할 주소

설계 조건은 다음과 같다.

| 항목 | 값 |
|---|---:|
| FFT 크기 `N` | 128 |
| FFT 방식 | Radix-2 DIT |
| Stage 수 | `log2(128) = 7` |
| Stage 범위 | 0~6 |
| Butterfly 수/Stage | 64 |
| 데이터 메모리 주소 | 7비트, 0~127 |
| Butterfly Count | 6비트, 0~63 |
| Twiddle ROM 주소 | 6비트, 0~63 |

---

## 2. Stage별 Butterfly 주소 관계

Radix-2 FFT에서 하나의 Butterfly는 두 데이터를 연산한다. Stage가 증가할수록 두 데이터의 주소 간격도 2배씩 증가한다.

Stage를 `s`라고 하면 두 주소의 간격 `gap`은 다음과 같다.

\[
gap=2^s
\]

| Stage | `gap` | 앞부분의 Butterfly 주소 쌍 |
|---:|---:|---|
| 0 | 1 | `(0,1)`, `(2,3)`, `(4,5)`, ... |
| 1 | 2 | `(0,2)`, `(1,3)`, `(4,6)`, `(5,7)`, ... |
| 2 | 4 | `(0,4)`, `(1,5)`, `(2,6)`, `(3,7)`, ... |
| 3 | 8 | `(0,8)`, `(1,9)`, ... |
| 4 | 16 | `(0,16)`, `(1,17)`, ... |
| 5 | 32 | `(0,32)`, `(1,33)`, ... |
| 6 | 64 | `(0,64)`, `(1,65)`, ... `(63,127)` |

Verilog에서는 2의 거듭제곱을 왼쪽 시프트로 생성한다.

```verilog
gap = 7'd1 << i_bf_stage;
```

---

## 3. Butterfly Count를 그룹 번호와 `j`로 분리

각 Stage에는 64개의 Butterfly가 있으므로 `i_bf_count`는 0부터 63까지 증가한다.

`i_bf_count`를 다음 두 값으로 나눈다.

- `group`: 현재 Butterfly가 속한 그룹 번호
- `j`: 해당 그룹 내부에서의 Butterfly 위치

수학적으로 다음과 같다.

\[
group=\left\lfloor\frac{count}{gap}\right\rfloor
\]

\[
j=count\bmod gap
\]

실제 Butterfly 주소는 다음과 같다.

\[
addr_A=group\times(2gap)+j
\]

\[
addr_B=addr_A+gap
\]

비트 구조로 표현하면 다음과 같다.

```text
count  = [ group ][ j ]
addr_A = [ group ][ 0 ][ j ]
addr_B = [ group ][ 1 ][ j ]
```

즉, `count`의 `group`과 `j` 사이에 각각 `0`과 `1`을 삽입하여 A/B 주소를 만든다.

---

## 4. `low_mask`를 사용하는 이유

`j = count % gap`을 구하기 위해 `low_mask`를 사용한다.

```verilog
low_mask = gap - 7'd1;
```

`gap`은 항상 2의 거듭제곱이므로 `gap-1`은 필요한 하위 비트가 모두 1인 값이 된다.

| Stage | `gap` | `low_mask` | 선택되는 `count` 비트 |
|---:|---:|---:|---|
| 0 | `0000001` | `0000000` | 없음, `j=0` |
| 1 | `0000010` | `0000001` | `count[0]` |
| 2 | `0000100` | `0000011` | `count[1:0]` |
| 3 | `0001000` | `0000111` | `count[2:0]` |
| 4 | `0010000` | `0001111` | `count[3:0]` |
| 5 | `0100000` | `0011111` | `count[4:0]` |
| 6 | `1000000` | `0111111` | `count[5:0]` |

따라서 다음 AND 연산으로 `j`를 구할 수 있다.

```verilog
j = count_ext & low_mask;
```

반대로 `~low_mask`를 사용하면 그룹에 해당하는 상위 비트만 남길 수 있다.

```verilog
group_part = count_ext & ~low_mask;
```

---

## 5. `count_ext`를 사용하는 이유

`i_bf_count`는 0~63을 표현하는 6비트 신호지만 데이터 메모리 주소는 0~127을 표현하는 7비트 신호다.

```verilog
count_ext = {1'b0, i_bf_count};
```

`count_ext`는 `i_bf_count`의 값을 변경하지 않고 비트 폭만 7비트로 확장한 값이다.

```text
i_bf_count =  101101  // 6비트, 45
count_ext   = 0101101  // 7비트, 45
```

시프트 연산 전에 7비트로 확장해야 최상위 주소 비트가 잘리지 않고 0~127 범위의 주소를 정상적으로 만들 수 있다.

---

## 6. A/B 주소의 Verilog 구현

A 주소는 `count`의 `group`과 `j` 사이에 0을 삽입하여 생성한다.

```verilog
addr_a_calc = ((count_ext & ~low_mask) << 1)
            |  (count_ext &  low_mask);
```

각 항의 의미는 다음과 같다.

```text
(count_ext & ~low_mask) << 1  → [ group ][ 0 ][ 0...0 ]
(count_ext &  low_mask)       → [   0   ][ 0 ][   j   ]
두 값을 OR                     → [ group ][ 0 ][   j   ]
```

B 주소는 A 주소에서 `gap`만큼 떨어져 있으므로 다음과 같이 계산한다.

```verilog
o_addr_a = addr_a_calc;
o_addr_b = addr_a_calc + gap;
```

`addr_a_calc | gap`도 동일한 결과를 만들지만, `+ gap`이 두 주소의 간격을 더 직관적으로 표현한다.

### Stage 2, Count 5 예시

```text
stage = 2
count = 5
gap   = 2^2 = 4

group = 5 / 4 = 1
j     = 5 % 4 = 1

addr_A = 1 × (2 × 4) + 1 = 9
addr_B = 9 + 4 = 13
```

비트 기준으로 보면 다음과 같다.

```text
count  = [0001][01]
addr_A = [0001][0][01] = 0001001 = 9
addr_B = [0001][1][01] = 0001101 = 13
```

---

## 7. Twiddle ROM 주소 생성

128-point FFT에서 각 Butterfly가 사용하는 Twiddle 지수는 다음과 같다.

\[
tw\_addr=j\times\frac{N}{2gap}
\]

`N=128`을 대입하면 다음과 같다.

\[
tw\_addr=j\times\frac{64}{gap}
\]

`gap=2^stage`이므로 다음처럼 바꿀 수 있다.

\[
tw\_addr=j\times2^{6-stage}
\]

2의 거듭제곱 곱셈은 왼쪽 시프트로 구현한다.

```verilog
twiddle_ext = (count_ext & low_mask)
            << (3'd6 - i_bf_stage);

o_tw_addr = twiddle_ext[5:0];
```

Stage 2, Count 5에서는 다음과 같다.

```text
j       = 1
tw_addr = 1 × (64/4)
        = 16
```

따라서 해당 Butterfly는 `W_128^16`을 사용한다.

---

## 8. `ST_LOAD`: Bit-Reversed 입력 저장

Radix-2 DIT 구조에서 최종 출력을 Natural Order로 얻으려면 입력을 Bit-Reversed 주소에 저장할 수 있다.

FFT Buffer에서 한 번에 두 샘플이 전달되므로 원래 입력 인덱스는 다음과 같다.

```text
입력 A: x[2p]
입력 B: x[2p+1]
```

각 7비트 인덱스를 뒤집어 저장 주소를 생성한다.

```verilog
o_addr_a = bit_reverse7({i_bf_count, 1'b0});
o_addr_b = bit_reverse7({i_bf_count, 1'b1});
```

예를 들어 `p=1`이라면 입력 인덱스 2와 3은 다음 주소에 저장된다.

```text
x[2] : 0000010 → Bit Reverse → 0100000 = 32
x[3] : 0000011 → Bit Reverse → 1100000 = 96
```

---

## 9. `ST_OUT`: Natural-Order 출력

FFT 연산이 끝난 뒤 메모리에는 결과가 Natural Order로 저장되어 있으므로 연속된 두 주소를 읽는다.

```verilog
o_addr_a = {i_bf_count, 1'b0}; // 2 × count
o_addr_b = {i_bf_count, 1'b1}; // 2 × count + 1
```

주소는 다음 순서로 출력된다.

```text
(0,1), (2,3), (4,5), ... (126,127)
```

---

## 10. 권장 Verilog 계산부

```verilog
reg [6:0] gap;
reg [6:0] low_mask;
reg [6:0] count_ext;
reg [6:0] addr_a_calc;
reg [6:0] twiddle_ext;

always @(*) begin
    o_addr_a   = 7'd0;
    o_addr_b   = 7'd0;
    o_tw_addr  = 6'd0;

    gap         = 7'd0;
    low_mask    = 7'd0;
    count_ext   = {1'b0, i_bf_count};
    addr_a_calc = 7'd0;
    twiddle_ext = 7'd0;

    case (i_sys_state)
        ST_LOAD: begin
            o_addr_a = bit_reverse7({i_bf_count, 1'b0});
            o_addr_b = bit_reverse7({i_bf_count, 1'b1});
        end

        ST_READ, ST_CALC, ST_WRITE: begin
            if (i_bf_stage <= 3'd6) begin
                gap      = 7'd1 << i_bf_stage;
                low_mask = gap - 7'd1;

                addr_a_calc = ((count_ext & ~low_mask) << 1)
                            |  (count_ext &  low_mask);

                o_addr_a = addr_a_calc;
                o_addr_b = addr_a_calc + gap;

                twiddle_ext = (count_ext & low_mask)
                            << (3'd6 - i_bf_stage);
                o_tw_addr = twiddle_ext[5:0];
            end
        end

        ST_OUT: begin
            o_addr_a = {i_bf_count, 1'b0};
            o_addr_b = {i_bf_count, 1'b1};
        end

        default: begin
            // IDLE/DONE: outputs remain zero.
        end
    endcase
end
```

---

## 11. 비트 연산으로 구현한 이유

수학식을 그대로 사용하면 나눗셈, 나머지, 곱셈으로 작성할 수 있다.

```verilog
group  = i_bf_count / gap;
j      = i_bf_count % gap;
addr_a = group * (2 * gap) + j;
addr_b = addr_a + gap;
```

하지만 이 설계의 모든 기준값은 2의 거듭제곱이므로 더 직접적인 비트 연산으로 표현할 수 있다.

| 수학적 의미 | Verilog 구현 |
|---|---|
| `2^stage` | `1 << stage` |
| `count % gap` | `count & (gap-1)` |
| `group` 부분 선택 | `count & ~(gap-1)` |
| `×2` | `<< 1` |
| `×2^(6-stage)` | `<< (6-stage)` |
| `addr_B = addr_A + gap` | `addr_a_calc + gap` |

따라서 이 구현은 FFT 주소 생성 수식을 FPGA의 조합 논리에 적합한 시프트, AND, OR, 덧셈 연산으로 변환한 것이다.

---

## 12. 설계 시 확인 사항

1. `i_bf_stage`는 `0~6` 범위에서만 사용한다.
2. `i_bf_count`는 각 Stage에서 `0~63`까지 순회한다.
3. Controller는 하나의 Butterfly가 `READ→CALC→WRITE`를 수행하는 동안 Stage와 Count를 유지해야 한다.
4. `ST_WRITE` 완료 후 다음 Butterfly Count로 증가해야 한다.
5. 합성 후 가변 시프트에 의해 생성된 MUX의 LUT 사용량과 타이밍을 확인한다.
6. 타이밍이 불리하면 동일한 규칙을 Stage별 `case`문으로 펼친 구현과 비교한다.

주소 생성 규칙의 핵심은 다음 한 줄로 요약할 수 있다.

```text
count = [group][j] → addr_A = [group][0][j], addr_B = [group][1][j]
```
