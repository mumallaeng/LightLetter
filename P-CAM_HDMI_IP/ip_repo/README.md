# ip_repo — Zybo Z7-20 Pcam 5C 설계에 필요한 IP 모음

수집 2026-08-19 · Vivado 2022.2 대상

`Zybo-Z7-20 Pcam5C CAM-to-HDMI 구축 절차.md`의 최소 구성을 만드는 데 필요한
IP를 한 곳에 모은 것이다. **원본 두 곳(vivado-library, pcam 데모)을 오갈 필요
없이 이 폴더 하나만 IP Repository에 등록하면 된다.**

---

## 1. Vivado 등록 방법

`Settings - IP - Repository`에 **이 폴더 하나만** 추가한다.

```
D:\dev\FPGA_MyWorkSpace\Zybo_Z7-20\P-CAM_HDMI_Test\ip_repo
```

Vivado는 하위 폴더를 재귀적으로 훑으므로 `ip/`와 `if/`를 따로 등록하지 않아도
된다. Apply를 누르면 **IP 6개, 인터페이스 1개**가 발견되었다고 표시된다.
숫자가 다르면 경로를 다시 확인하고, 표시가 아예 없으면 다음 단계로 넘어가지
않는다.

---

## 2. 들어 있는 것

### 필수 IP 4개 — 이게 없으면 설계가 성립하지 않는다

| IP | VLNV | 역할 | 원본 |
|---|---|---|---|
| `MIPI_D_PHY_RX` | digilentinc.com:ip:MIPI_D_PHY_RX:**1.3** | MIPI 물리계층 역직렬화 | vivado-library |
| `MIPI_CSI_2_RX` | digilentinc.com:ip:MIPI_CSI_2_RX:**1.2** | CSI-2 패킷 파싱 → AXIS | vivado-library |
| `AXI_BayerToRGB` | digilentinc.com:user:AXI_BayerToRGB:**1.0** | 데모자이크 RAW10 → RGB888 | pcam 데모 `repo/local/ip` |
| `rgb2dvi` | digilentinc.com:ip:rgb2dvi:**1.4** | TMDS 직렬화 → HDMI TX | vivado-library |

버전은 Digilent 공식 프로젝트가 쓰는 것과 **정확히 일치**한다. 확인 완료.

`AXI_BayerToRGB`가 vivado-library에 없어서 두 곳을 오가야 했던 것이 이 폴더를
만든 이유다. MIPI CSI-2는 RAW10 베이어만 뱉으므로 이 IP를 뺄 수 없다.
(`MIPI_CSI_2_RX`에 RGB565 선택지가 있어 보이지만 HDL에 구현 분기가 없다.
구축 절차 문서 4.2절 참고)

### 선택 IP 2개 — 확장할 때만 쓴다

| IP | VLNV | 언제 쓰나 |
|---|---|---|
| `axi_dynclk` | digilentinc.com:ip:axi_dynclk:1.2 | **해상도 런타임 전환**이 필요할 때. 구축 절차 4.7절 |
| `AXI_GammaCorrection` | digilentinc.com:user:AXI_GammaCorrection:1.0 | 감마 보정을 되살릴 때. 구축 절차 14절 |

카탈로그에 떠 있어도 블록디자인에 넣지 않으면 아무 영향이 없다.

**`axi_dynclk`을 같이 넣어 둔 이유**가 있다. PZ7020에서 쓰던 바로 그 IP이고,
`PXL_CLK_O`와 `PXL_CLK_5X_O`를 둘 다 출력한다. 나중에 해상도 전환이 필요해지면
`clk_wiz`를 이것으로 갈아 끼우기만 하면 되고, **PZ7020의 `dynclk/` C 드라이버가
그대로 붙는다.**

### `if/tmds_v1_0` — 인터페이스 정의

`rgb2dvi`가 `digilentinc.com:interface:tmds_rtl:1.0`을 참조한다. 이게 없으면
IP는 카탈로그에 뜨는데 **TMDS 포트가 인터페이스로 묶이지 않고 개별 신호로
흩어져서** Make External이 지저분해진다.

MIPI IP 두 개는 확인해 보니 Xilinx 기본 인터페이스(`aximm`, `axis`,
`diff_clock`, `rx_mipi_ppi_if`)만 쓴다. 별도 정의가 필요 없다.

### `hdl/` — 최소 구성에서는 쓰지 않는다

```
DVIClocking.vhd
SyncAsync.vhd
SyncAsyncReset.vhd
```

**IP가 아니라 RTL 모듈이고, 최소 구성에는 넣지 않는다.** Vivado도 이 폴더를
무시한다(component.xml이 없으므로).

공식 구성으로 되돌릴 때만 프로젝트 소스로 추가한다. 왜 최소 구성에서 사라지는
지는 구축 절차 4.3절에 있다. 요약하면 `rgb2dvi`의 `kGenerateSerialClk = true`가
5배 클럭을 내부에서 만들어 주므로 밖에서 BUFIO/BUFR를 돌릴 물건이 필요 없다.

---

## 3. 용량이 34MB인 이유 — 지우지 말 것

`MIPI_D_PHY_RX/hdl`과 `MIPI_CSI_2_RX/hdl`이 각각 15MB가 넘는다. 실제 HDL이
아니라 **미리 생성해 둔 ILA 디버그 코어**다.

```
ila_scnn_refclk/  ila_sfen_refclk/  ila_sfen_rxclk/     (D-PHY)
ila_rxclk/  ila_rxclk_lane/  ila_vidclk/                (CSI-2)
```

각 `.xml`이 4.7MB쯤 된다. IP의 `kDebug` 파라미터를 `true`로 놓았을 때만
인스턴스화되므로, 우리 설정(`kDebug = false`)에서는 **비트스트림에 한 조각도
들어가지 않는다.**

그래도 **지우면 안 된다.** `component.xml`이 이 파일들을 참조하고 있어서,
없으면 IP 패키징 검증이 실패하고 카탈로그에 뜨지 않는다.

---

## 4. 원본 위치

나중에 갱신하거나 대조할 때를 위해 적어 둔다.

```
vivado-library
  D:\dev\FPGA_MyWorkSpace\Zybo_Z7-20\vivado-library-master\
    ip\MIPI_D_PHY_RX  ip\MIPI_CSI_2_RX  ip\rgb2dvi  ip\axi_dynclk
    if\tmds_v1_0

pcam 데모 (GitHub master)
  D:\dev\FPGA_MyWorkSpace\Zybo_Z7-20\Zybo-Z7-20-pcam-5c-master\
    Zybo-Z7-20-pcam-5c-master\
      repo\local\ip\AXI_BayerToRGB
      repo\local\ip\AXI_GammaCorrection
      src\hdl\*.vhd
```

> **주의.** GitHub의 `master` 브랜치는 **Vivado 2019.1** 기준이다. IP 소스
> 자체는 버전 중립이라 2022.2에서 재생성되므로 이 폴더의 IP는 그대로 써도
> 된다. 다만 `proj/*.xpr`이나 `src/bd/system.tcl`을 열 생각이라면
> **2022.1 릴리스를 따로 받는 편이 낫다.** master의 `repo/vivado-library`는
> 서브모듈이라 zip에 비어 있는 것도 확인했다.

---

## 5. 등록 후 확인

Vivado Tcl 콘솔에서 아래를 실행하면 6개가 나와야 한다.

```tcl
get_ipdefs -filter {VENDOR == digilentinc.com}
```

TMDS 인터페이스는 이렇게 확인한다.

```tcl
get_bus_definitions *tmds*
```
