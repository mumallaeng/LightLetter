#!/usr/bin/env bash
# ce_ctrl_l1 row-tlast test (Vivado xsim): conv_l1 with a 28x28 AXIS stream that has tlast at the end of every row.
#
#   ./run_sim.sh                    4 runs: valid/ready 100/100, 60/70, 35/50 with junk tlast while tvalid = 0,
#                                   and 100/100 with tlast = 0 while tvalid = 0
#   ./run_sim.sh one [VALID] [READY] [JUNK] [SEED] [FRAMES]
#   CE=path/to/ce_ctrl_l1.v ./run_sim.sh ...   compile another ce_ctrl_l1.v instead of tx/cnn/rtl/ce_ctrl_l1.v
#   ./run_sim.sh clean
#
# Each run writes build/<name>_report.txt and build/<name>_trace.txt, <name> = v<VALID>_r<READY>_j<JUNK>_s<SEED>.
set -u
cd "$(dirname "$0")"
CNN=../..
RTL=$CNN/rtl
BUILD=build
CE=${CE:-$RTL/ce_ctrl_l1.v}
CE=$(cd "$(dirname "$CE")" && pwd)/$(basename "$CE")

XVLOG=xvlog; XELAB=xelab; XSIM=xsim
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) XVLOG=xvlog.bat; XELAB=xelab.bat; XSIM=xsim.bat ;; esac

prepare() {
    mkdir -p $BUILD
    cp $RTL/rtl_ref/ce1_stim.mem $RTL/rtl_ref/ce1_out.mem $RTL/rtl_ref/conv1_bias_ce.mem $BUILD/
    local srcs
    srcs=$(ls $RTL/*.v | grep -v '/ce_ctrl_l1\.v$' | sed 's|^|../|')
    echo "ce_ctrl_l1: $CE"
    (cd $BUILD && $XVLOG --nolog ../tb_ce_ctrl_l1_tlast.v "$CE" $srcs > xvlog.txt 2>&1) ||
        { grep -E "ERROR" $BUILD/xvlog.txt; echo "xvlog failed"; exit 1; }
}

# run VALID READY JUNK SEED FRAMES
run() {
    local v=$1 r=$2 j=$3 s=$4 f=$5
    local name=v${v}_r${r}_j${j}_s${s}
    # xelab options go through a file: xelab.bat on Windows splits command-line arguments at '='
    printf "%s\n" "--nolog" "tb_ce_ctrl_l1_tlast" "-s snap_$name" "--generic_top VALID_PCT=$v" \
        "--generic_top READY_PCT=$r" "--generic_top IDLE_JUNK=$j" "--generic_top SEED=$s" \
        "--generic_top FRAMES=$f" > $BUILD/xelab_$name.opt
    (cd $BUILD &&
        $XELAB -f xelab_$name.opt > xelab_$name.txt 2>&1 ||
            { grep -E "ERROR" xelab_$name.txt; echo "xelab failed"; exit 1; }
        echo "run all; quit" > run_all.tcl
        $XSIM --nolog snap_$name -tclbatch run_all.tcl > xsim_$name.txt 2>&1
        mv -f tb_ce_ctrl_l1_tlast_report.txt ${name}_report.txt 2>/dev/null
        mv -f tb_ce_ctrl_l1_tlast_trace.txt ${name}_trace.txt 2>/dev/null)
    local verdict
    verdict=$(grep -E "^\[(PASS|FAIL)\] ce_ctrl_l1" $BUILD/xsim_$name.txt | tail -1)
    printf "%-16s %s\n" "$name" "${verdict:-[FAIL] no verdict - see $BUILD/xsim_$name.txt}"
    grep -E "^\[FAIL\]\[" $BUILD/xsim_$name.txt | head -4 | sed 's/^/    /'
    grep -E "^  frame [0-9]" $BUILD/xsim_$name.txt | sed 's/^ */    /'
}

case "${1:-}" in
    "")
        prepare
        run 100 100 1 1 3
        run 60 70 1 2 3
        run 35 50 1 3 3
        run 100 100 0 4 3
        echo "reports: $BUILD/*_report.txt, traces: $BUILD/*_trace.txt"
        ;;
    one)   prepare; run "${2:-100}" "${3:-100}" "${4:-1}" "${5:-1}" "${6:-3}" ;;
    clean) rm -rf $BUILD ;;
    *) sed -n '2,12p' "$0"; exit 1 ;;
esac
