# cnn_cam Vivado 스냅샷 (2026-09-26)

CNN 가속기에 입력을 공급하는 카메라 캡처 설계임. Vivado 2020.2, Zybo Z7-20 (`xc7z020clg400-1`, board part `digilentinc.com:zybo-z7-20:part0:1.0`). Pcam 5C (OV5640)에서 받은 영상을 28x28 이진 프레임으로 만들어 CNN에 넘기고, 같은 영상을 HDMI로 미러링함.

최상위 HDL은 블록 디자인에서 생성되는 `design_1_wrapper`임. 외부 포트는 MIPI D-PHY 레인(`dphy_*`), 카메라 제어 핀(`cam_iic_*`, `cam_gpio_tri_io`), HDMI TMDS 페어(`TMDS_*`), 그리고 PS DDR과 fixed IO임.

## 신호 경로

```
OV5640 -> MIPI_D_PHY_RX -> MIPI_CSI_2_RX -> AXI_BayerToRGB -> AXI_GammaCorrection
       -> axis_broadcaster -+-> axi_vdma -> v_tc / v_axi4s_vid_out -> axi_dynclk -> rgb2dvi -> HDMI
                            +-> img_preprocess -> (28x28 이진 스트림, CNN 입력)
```

`img_preprocess`는 1280x720 RGB 스트림에서 가운데 정사각형(X=280..999)을 잘라 28x28로 서브샘플링하고 픽셀별로 임계값 처리를 함. `axi_gpio`의 `capture_req`가 한 프레임을 트리거함. 전처리 스트림에는 ILA가 붙어있음.

## 추적하는 파일

블록 디자인은 두 가지 형태로 들어있고 둘 중 아무거나 써도 됨.

| 파일 | 용도 |
|---|---|
| `design_1.tcl` | `write_bd_tcl` 익스포트. 네이티브 포맷에 의존하지 않고 BD를 재생성함. Vivado 2020.2를 고정하고, Pcam/HDMI IP 6개가 카탈로그에 없으면 어느 VLNV가 빠졌는지 찍고 멈춤 |
| `design_1/design_1.bd` | 같은 설계의 네이티브 형태. `ui/`(캔버스 배치)와 `ip/*/*.xci`(IP 설정)가 세트임. 이 폴더만 있어도 `add_files`로 바로 열림 |

그 외:

| 경로 | 내용 |
|---|---|
| `rtl/cnn_cam/img_preprocess.v` | 전처리 RTL. BD가 참조하는 실제 소스 |
| `tb/cnn_cam/tb_img_preprocess.sv` | 전처리 테스트벤치. 입력 자극 `.mem`은 용량이 커서 추적하지 않음 |
| `constraints/cnn_cam_zybo_z7_master.xdc` | 보드 제약 |
| `constraints/cnn_cam_mipi_timing_override.xdc` | MIPI 타이밍 오버라이드 |
| `hw/cnn_cam/cnn_cam_base.xsa` | Vivado -> Vitis 핸드오프 |
| `vitis/cnn_cam_platform/platform.tcl` | 플랫폼 재생성 스크립트 |
| `vitis/cnn_cam_app/src/` | 애플리케이션 코드 |

원칙은 하나임 — **추적 중인 다른 파일로부터 재생성할 수 없는 것만 남긴다.**

그래서 다음은 추적하지 않음. `.gen`, `.runs`, `.cache`, `.hw`, `.sim`, `.ip_user_files`, `.xpr`, 비트스트림. BD wrapper(`design_1_wrapper.v`)는 `make_wrapper`가 다시 만들고, 커밋해두면 BD를 고칠 때마다 충돌만 남음. `design_1.bda`는 `.bd`의 `addressing` 블록에서 다시 만들어지는 Address Editor 뷰임. `ui/`는 캔버스 좌표라 재생성이 안 되므로 남겨둠. BD 밖에 있던 프로젝트 레벨 IP 사본 3개는 BD 안의 같은 IP가 대체함.

## 재현

필요한 외부 IP는 수업에서 받은 Pcam/HDMI IP 하나뿐임. 원래 프로젝트는 프로젝트 폴더 옆의 `../zybo_IP/P-CAM_HDMI_IP`에 두고 썼음. `design_1.tcl`이 시작 전에 확인하는 6개가 이것들임:

```
digilentinc.com:user:AXI_BayerToRGB:1.0        digilentinc.com:ip:MIPI_D_PHY_RX:1.3
digilentinc.com:user:AXI_GammaCorrection:1.0   digilentinc.com:ip:axi_dynclk:1.2
digilentinc.com:ip:MIPI_CSI_2_RX:1.2          digilentinc.com:ip:rgb2dvi:1.4
```

나머지는 전부 표준 Xilinx IP라 카탈로그에 이미 있음. `bin_filter`는 이 BD에 쓰이지 않으므로 별도 IP repo가 필요 없음.

`img_preprocess`는 패키징된 IP가 아니라 RTL 모듈 참조임(`create_bd_cell -type module -reference`). 그래서 BD를 불러오기 **전에** `rtl/cnn_cam/img_preprocess.v`가 프로젝트에 들어가 있어야 함.

```tcl
create_project cnn_cam ./cnn_cam -part xc7z020clg400-1
set_property board_part digilentinc.com:zybo-z7-20:part0:1.0 [current_project]
set_property ip_repo_paths {<경로>/zybo_IP/P-CAM_HDMI_IP} [current_project]
update_ip_catalog

add_files -norecurse ../rtl/cnn_cam/img_preprocess.v
add_files -fileset constrs_1 {../constraints/cnn_cam_zybo_z7_master.xdc ../constraints/cnn_cam_mipi_timing_override.xdc}

# 아래 둘 중 하나
add_files -norecurse ../vivado/cnn_cam/design_1/design_1.bd   ;# 다이어그램 배치 유지
source ../vivado/cnn_cam/design_1.tcl                          ;# Vivado 버전 의존 적음

make_wrapper -files [get_files design_1.bd] -top
```

이후 생성된 wrapper를 프로젝트에 추가하고 top으로 지정한 뒤 비트스트림을 만들면 됨.

## 소프트웨어

`hw/cnn_cam/cnn_cam_base.xsa`가 소프트웨어 쪽이 빌드하는 대상임. 이걸로 플랫폼을 만들고 애플리케이션 소스를 붙임:

```
xsct vitis/cnn_cam_platform/platform.tcl   # -hw, -out 경로를 먼저 로컬에 맞게 고칠 것
```

그다음 그 플랫폼 위에 standalone 애플리케이션을 하나 만들고 `vitis/cnn_cam_app/src`를 연결하면 됨.

소스와 플랫폼 스크립트만 추적함. Eclipse `.project`/`.cproject`/`.prj`/`.sprj`, `platform.spr`, `lscript.ld`는 빠져있음 — 커스텀 include 경로나 define, 컴파일러 플래그가 하나도 없고 링커 스크립트도 기본값이라, Vitis가 앱을 만들 때 `Debug/`, `Release/`, `_ide/`, BSP와 함께 같은 내용으로 다시 씀.

`platform.tcl`에는 이 파일을 만든 장비의 절대경로(`D:/OndeviceAI2/CNN_CAM`)가 남아있음. 툴이 쓴 대로 두었으니 로컬에서 경로만 바꿔 쓸 것.

## 이름 규칙

Vivado 쪽은 프로젝트 이름에 의존하지 않음. `design_1.tcl`은 프로젝트 이름을 언급하지 않고, `.xci`에 남은 유일한 흔적은 출력 산출물을 어디에 썼는지 가리키는 `gen_directory`인데 Vivado가 새 프로젝트에 맞춰 덮어씀. 프로젝트 이름은 아무렇게나 지어도 됨.

Vitis 쪽은 이름 독립이 불가능함. Eclipse가 디렉터리 이름과 `.project`의 `<name>`이 같기를 요구하고, system project가 애플리케이션과 플랫폼을 이름으로 찾음. 여기서는 `cnn_cam_platform`, `cnn_cam_app`을 쓰고, 이름을 바꿀 때는 디렉터리와 기술자 파일들을 한꺼번에 바꿔야 함.

`cnn_cam_base.xsa` 안의 비트스트림은 여전히 `CNN_CAM_base.bit`로 들어있음. 툴이 내보낸 아카이브라 다시 패킹하지 않았고, Vitis는 파일 이름이 아니라 내용으로 플랫폼을 식별하므로 문제없음.
