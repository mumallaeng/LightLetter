#!/usr/bin/env bash
# cnn_top RTL simulation (Vivado xsim) against the C golden model.
#
#   ./run_sim.sh                     4 runs: input valid 100 / 60 / 35 / 15 %, GATE 0 (cnn_done gate)
#   ./run_sim.sh all                 the 4 runs above + the same 4 with GATE 1 (frames overlap in the pipeline)
#   ./run_sim.sh one [VALID] [GATE] [BIAS_FIX] [SEED]    a single run, e.g.  ./run_sim.sh one 60 1
#   ./run_sim.sh wave [VALID] [GATE] a single run with a waveform database build/<name>.wdb (open in Vivado)
#   ./run_sim.sh vectors             regenerate vectors/ (FC / argmax golden) with gen_fc_golden.py
#   ./run_sim.sh img [N] [VALID]     test/<folder>/*.png, first N images per class (default 5) -> tb_cnn_top_img.v
#                                    (no test/ : the saved 70 images in vectors/img_stim.mem)
#                                    build/img_report.txt : label vs cnn_result (PASS/FAIL) + RTL vs integer model
#   ./run_sim.sh clean
#
# Every run writes build/<name>_report.txt (summary + per stage grids / tables) and build/<name>_trace.txt
# (every handshake with its sim time). <name> = v<VALID>_g<GATE>_b<BIAS_FIX>_s<SEED>.
# The .mem files are read by bare file name, so everything is copied into build/ and the sim runs there.
set -u
cd "$(dirname "$0")"
ROOT=../../..
RTL=$ROOT/rtl/cnn
BUILD=build

XVLOG=xvlog; XELAB=xelab; XSIM=xsim
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) XVLOG=xvlog.bat; XELAB=xelab.bat; XSIM=xsim.bat ;; esac

prepare() {
    mkdir -p $BUILD
    cp $RTL/mem/*.mem $BUILD/                                         # ROMs the RTL reads
    for f in ce1_stim ce1_out pool1_out ce2_out pool2_out conv2_weight conv1_bias_ce conv2_bias_ce; do
        cp $RTL/rtl_ref/$f.mem $BUILD/                                # golden up to pool_l2
    done
    for f in fc1_out fc2_out logit_out class_out; do
        [ -f vectors/$f.mem ] || { echo "vectors/$f.mem missing - run ./run_sim.sh vectors"; exit 1; }
        cp vectors/$f.mem $BUILD/                                     # FC / argmax golden
    done
    (cd $BUILD && $XVLOG --nolog ../tb_cnn_top.v ../tb_cnn_top_img.v ../$RTL/*.v > xvlog.txt 2>&1) ||
        { grep -E "ERROR|WARNING" $BUILD/xvlog.txt; echo "xvlog failed"; exit 1; }
}

# run VALID GATE BIAS_FIX SEED [wave]
run() {
    local v=$1 g=$2 b=$3 s=$4 wave=${5:-}
    local name=v${v}_g${g}_b${b}_s${s}
    local dbg="" tcl=run_all.tcl
    echo "run all; quit" > $BUILD/$tcl
    if [ -n "$wave" ]; then
        dbg="-debug typical"
        printf "log_wave -recursive *\nrun all\nquit\n" > $BUILD/wave.tcl; tcl=wave.tcl
    fi
    # xelab options go through a file: xelab.bat on Windows splits command-line arguments at '='
    printf "%s\n" "--nolog" $dbg "tb_cnn_top" "-s snap_$name" "--generic_top VALID_PCT=$v" \
        "--generic_top GATE=$g" "--generic_top BIAS_FIX=$b" "--generic_top SEED=$s" > $BUILD/xelab_$name.opt
    (cd $BUILD &&
        $XELAB -f xelab_$name.opt > xelab_$name.txt 2>&1 ||
            { grep -E "ERROR" xelab_$name.txt; echo "xelab failed"; exit 1; }
        $XSIM --nolog snap_$name -tclbatch $tcl ${wave:+-wdb $name.wdb} > xsim_$name.txt 2>&1
        mv -f tb_cnn_top_report.txt ${name}_report.txt 2>/dev/null
        mv -f tb_cnn_top_trace.txt ${name}_trace.txt 2>/dev/null
        sed -i "s/tb_cnn_top_trace\.txt/${name}_trace.txt/" ${name}_report.txt 2>/dev/null)
    local verdict
    verdict=$(grep -E "^\[(PASS|FAIL)\] cnn_top" $BUILD/xsim_$name.txt | tail -1)
    printf "%-18s %s\n" "$name" "${verdict:-[FAIL] no verdict - see $BUILD/xsim_$name.txt}"
    grep -E "^\[FAIL\]\[" $BUILD/xsim_$name.txt | head -5 | sed 's/^/    /'
    grep -E "frame [0-9] : cnn_done" $BUILD/xsim_$name.txt | sed 's/^ */    /'
}

# run_img N_PER_CLASS VALID
run_img() {
    local n_img
    n_img=$(python gen_img_vectors.py "$1" | tee /dev/stderr | sed -n 's/^N_IMG=//p')
    [ -n "$n_img" ] || { echo "gen_img_vectors.py failed"; exit 1; }
    printf "%s\n" "--nolog" "tb_cnn_top_img" "-s snap_img" "--generic_top N_IMG=$n_img" \
        "--generic_top VALID_PCT=$2" > $BUILD/xelab_img.opt
    (cd $BUILD &&
        $XELAB -f xelab_img.opt > xelab_img.txt 2>&1 ||
            { grep -E "ERROR" xelab_img.txt; echo "xelab failed"; exit 1; }
        echo "run all; quit" > run_all.tcl
        $XSIM --nolog snap_img -tclbatch run_all.tcl > xsim_img.txt 2>&1
        mv -f tb_cnn_top_img_report.txt img_report.txt 2>/dev/null)
    grep -E "^  img |accuracy|rtl==model :|^\[(PASS|FAIL)\]" $BUILD/xsim_img.txt
    echo "report: $BUILD/img_report.txt"
}

case "${1:-}" in
    ""|all)
        prepare
        for v in 100 60 35 15; do run $v 0 1 $v; done
        if [ "${1:-}" = all ]; then for v in 100 60 35 15; do run $v 1 1 $v; done; fi
        echo "reports: $BUILD/*_report.txt, traces: $BUILD/*_trace.txt"
        ;;
    one)   prepare; run "${2:-100}" "${3:-0}" "${4:-1}" "${5:-1}" ;;
    wave)  prepare; run "${2:-100}" "${3:-0}" 1 1 wave; echo "waveform: $BUILD/v${2:-100}_g${3:-0}_b1_s1.wdb" ;;
    vectors) python gen_fc_golden.py ;;
    img)   prepare; run_img "${2:-5}" "${3:-100}" ;;
    clean) rm -rf $BUILD ;;
    *) sed -n '2,16p' "$0"; exit 1 ;;
esac
