"""보드 전력 측정 (tx_fpga 'p' 메뉴) 로그 수집 + 분석.

    python power_meas_capture.py --port COM5               # 'p' 를 보내고 끝날 때까지 기록
    python power_meas_capture.py --port COM5 --vin 5.04    # DMM 으로 잰 입력 전압 반영
    python power_meas_capture.py --parse power_xxx.log     # 저장된 로그만 다시 분석

보드 쪽 (src/power_meas/power_meas.c) 은 상태를
    WFI -> BUSY -> CPU_CNN -> WFI -> PL_CNN -> PL_CNN_WFI -> WFI
순서로 라운드마다 반복하고 RUN / TRC / SUM 줄을 UART 로 보낸다.

여기서는 raw16 평균에서 전류/전력을 다시 계산한다 (--vin 반영). 증가분 dP 는
각 RUN 의 바로 앞뒤 WFI 평균을 기준으로 잡아 온도/레귤레이터 드리프트를 상쇄한다.
측정값은 보드 전체 입력 전력이다 (PS 또는 PL 단독 전력이 아님).

pyserial 필요 : pip install pyserial
"""
import argparse
import csv
import statistics
import sys
import time
from pathlib import Path

STATES = ["WFI", "BUSY", "CPU_CNN", "PL_CNN", "PL_CNN_WFI"]
RUN_COLS = ["round", "state", "t_ms", "samples", "raw16_avg", "raw16_std", "raw16_min",
            "raw16_max", "current_mA", "power_mW", "infer", "time_us", "us_per_infer",
            "cls_err"]


def raw_to_amp(raw16):
    """Zybo Z7 RM 1.4 : I = ((X * 244 / 3900) - 0.8) / 52, X = raw16 / 16 (12-bit)."""
    x = raw16 / 16.0
    return ((x * 244.0 / 3900.0) - 0.8) / 52.0


def capture(port, baud, log_path, timeout_s):
    import serial

    with serial.Serial(port, baud, timeout=1) as ser, open(log_path, "w", encoding="utf-8") as log:
        ser.reset_input_buffer()
        ser.write(b"p")
        t0 = time.time()
        done = False
        print(f"capturing {port} -> {log_path} (Ctrl+C to stop)")
        while time.time() - t0 < timeout_s:
            line = ser.readline().decode("ascii", errors="replace").rstrip("\r\n")
            if not line:
                continue
            log.write(line + "\n")
            log.flush()
            if not line.startswith("TRC,"):
                print(line)
            if line.startswith("power: done") or "aborted" in line:
                done = True
                break
        if not done:
            print("WARNING: timeout before 'power: done'", file=sys.stderr)


def parse(log_path):
    cfg, runs, trace = {}, [], []
    for line in Path(log_path).read_text(encoding="utf-8", errors="replace").splitlines():
        f = line.strip().split(",")
        if f[0] == "CFG":
            for kv in f[1:]:
                k, _, v = kv.partition("=")
                cfg[k] = v
        elif f[0] == "RUN" and len(f) == len(RUN_COLS) + 1:
            r = dict(zip(RUN_COLS, f[1:]))
            for k in RUN_COLS:
                if k != "state":
                    r[k] = float(r[k])
            runs.append(r)
        elif f[0] == "TRC" and len(f) == 5:
            trace.append({"round": int(f[1]), "state": f[2], "t_ms": int(f[3]),
                          "raw16_avg": float(f[4])})
    return cfg, runs, trace


def analyse(cfg, runs, vin):
    for r in runs:
        r["P_mW"] = raw_to_amp(r["raw16_avg"]) * vin * 1e3

    # 인접 idle 기준 증가분 : 앞뒤 WFI 의 평균 (한쪽만 있으면 그쪽)
    idx_wfi = [i for i, r in enumerate(runs) if r["state"] == "WFI"]
    for i, r in enumerate(runs):
        prev = [j for j in idx_wfi if j < i]
        nxt = [j for j in idx_wfi if j > i]
        ref = [runs[j]["P_mW"] for j in ([prev[-1]] if prev else []) + ([nxt[0]] if nxt else [])]
        r["dP_mW"] = r["P_mW"] - statistics.mean(ref) if ref and r["state"] != "WFI" else 0.0
        r["E_uJ"] = r["P_mW"] * r["us_per_infer"] / 1e3 if r["infer"] else None
        r["dE_uJ"] = r["dP_mW"] * r["us_per_infer"] / 1e3 if r["infer"] else None

    def ms(vals):
        vals = [v for v in vals if v is not None]
        if not vals:
            return None, None
        return statistics.mean(vals), (statistics.stdev(vals) if len(vals) > 1 else 0.0)

    summary = {}
    for st in STATES:
        rs = [r for r in runs if r["state"] == st]
        if not rs:
            continue
        summary[st] = {
            "runs": len(rs),
            "P": ms([r["P_mW"] for r in rs]),
            "dP": ms([r["dP_mW"] for r in rs]),
            "us": ms([r["us_per_infer"] if r["infer"] else None for r in rs]),
            "E": ms([r["E_uJ"] for r in rs]),
            "dE": ms([r["dE_uJ"] for r in rs]),
            "cls_err": int(sum(r["cls_err"] for r in rs)),
        }
    return summary


def fmt(pair, nd=2):
    m, s = pair
    return "-" if m is None else f"{m:.{nd}f} ± {s:.{nd}f}"


def report(cfg, summary, vin):
    print()
    print(f"Vin = {vin:.3f} V, CPU = {int(cfg.get('cpu_hz', 0)) / 1e6:.1f} MHz, "
          f"PL done = {cfg.get('pl_done', '?')}, class CPU/PL = {cfg.get('cpu_cls')}/{cfg.get('pl_cls')}")
    print("값은 라운드 간 평균 ± 표준편차. dP / dE 는 인접 WFI 대비 증가분 (board-level).")
    hdr = f"{'state':<11} {'runs':>4} {'P [mW]':>18} {'dP [mW]':>16} {'t/inf [us]':>18} " \
          f"{'E/inf [uJ]':>16} {'dE/inf [uJ]':>16} {'err':>4}"
    print(hdr)
    print("-" * len(hdr))
    for st, s in summary.items():
        print(f"{st:<11} {s['runs']:>4} {fmt(s['P']):>18} {fmt(s['dP']):>16} {fmt(s['us']):>18} "
              f"{fmt(s['E'], 1):>16} {fmt(s['dE'], 1):>16} {s['cls_err']:>4}")

    cpu = summary.get("CPU_CNN")
    if not cpu or cpu["us"][0] is None:
        return
    print()
    for st in ("PL_CNN", "PL_CNN_WFI"):
        pl = summary.get(st)
        if not pl or pl["us"][0] is None:
            continue
        sp = cpu["us"][0] / pl["us"][0]
        er = cpu["E"][0] / pl["E"][0]
        der = cpu["dE"][0] / pl["dE"][0] if pl["dE"][0] and pl["dE"][0] > 0 else float("nan")
        print(f"CPU_CNN vs {st:<10}: speedup x{sp:.2f}, board energy x{er:.2f} "
              f"({(1 - 1 / er) * 100:.1f}% less), incremental energy x{der:.2f}")


def save_csv(path, rows, cols):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="보드 UART (예: COM5)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--parse", help="저장된 로그만 분석")
    ap.add_argument("--vin", type=float, help="DMM 으로 잰 보드 입력 전압 [V] (없으면 보드 CFG 값)")
    ap.add_argument("--timeout", type=float, default=900, help="수집 최대 시간 [s]")
    ap.add_argument("--out", default="power_logs", help="로그/CSV 저장 폴더")
    a = ap.parse_args()

    if a.parse:
        log_path = Path(a.parse)
    elif a.port:
        out = Path(a.out)
        out.mkdir(exist_ok=True)
        log_path = out / time.strftime("power_%Y%m%d_%H%M%S.log")
        capture(a.port, a.baud, log_path, a.timeout)
    else:
        ap.error("--port 또는 --parse 가 필요합니다")

    cfg, runs, trace = parse(log_path)
    if not runs:
        print("RUN 줄이 없습니다. 로그를 확인하세요.", file=sys.stderr)
        return 1
    vin = a.vin if a.vin else int(cfg.get("vin_mV", 5000)) / 1000.0
    summary = analyse(cfg, runs, vin)
    report(cfg, summary, vin)

    stem = log_path.with_suffix("")
    save_csv(f"{stem}_runs.csv", runs, RUN_COLS + ["P_mW", "dP_mW", "E_uJ", "dE_uJ"])
    for t in trace:
        t["P_mW"] = raw_to_amp(t["raw16_avg"]) * vin * 1e3
    save_csv(f"{stem}_trace.csv", trace, ["round", "state", "t_ms", "raw16_avg", "P_mW"])
    print(f"\nsaved {stem}_runs.csv, {stem}_trace.csv")
    return 0


if __name__ == "__main__":
    sys.exit(main())
