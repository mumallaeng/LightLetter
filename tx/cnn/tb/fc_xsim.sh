#!/usr/bin/env bash
# fc_top frame cycles in Vivado xsim: tb_fc_power.v (ports only, logits checked against the golden vectors) with the
# stimulus conditions of `make -f sim.mk fc`. The same bench runs on every branch, so the cycle counts compare directly.
#
#   ./fc_xsim.sh                      the five conditions of sim.mk (valid% / ready% / seed)
#   ./fc_xsim.sh one VALID READY [SEED]
#   ./fc_xsim.sh seeds VALID READY N  seeds 1..N of one condition
#
# "frame 1 N cycles" is the cycle count from the first accepted input to the last logit of frame 1.
# Everything runs in build/xsim_fc, the .mem files are read by bare file name.
set -u
cd "$(dirname "$0")"
RTL=$PWD/../rtl
BUILD=build/xsim_fc

XVLOG=xvlog; XELAB=xelab; XSIM=xsim
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) XVLOG=xvlog.bat; XELAB=xelab.bat; XSIM=xsim.bat ;; esac

prepare() {
    mkdir -p $BUILD
    cp $RTL/mem/fc*.mem vectors/fc_stim.mem vectors/fc3_out.mem $BUILD/
    (cd $BUILD && $XVLOG --nolog ../../tb_fc_power.v $RTL/*.v > xvlog.txt 2>&1) ||
        { grep -E "ERROR" $BUILD/xvlog.txt; echo "xvlog failed"; exit 1; }
}

# run VALID READY SEED
run() {
    local name=v$1_r$2_s$3
    # xelab options go through a file: xelab.bat on Windows splits command-line arguments at '='
    printf "%s\n" "--nolog" "tb_fc_power" "-s snap_$name" "--generic_top VALID_PCT=$1" \
        "--generic_top READY_PCT=$2" "--generic_top SEED=$3" > $BUILD/xelab_$name.opt
    (cd $BUILD &&
        $XELAB -f xelab_$name.opt > xelab_$name.txt 2>&1 ||
            { grep -E "ERROR" xelab_$name.txt; echo "xelab failed"; exit 1; }
        echo "run all; quit" > run_all.tcl
        $XSIM --nolog snap_$name -tclbatch run_all.tcl > xsim_$name.txt 2>&1)
    grep -E "^\[|FAIL" $BUILD/xsim_$name.txt || echo "no verdict - see $BUILD/xsim_$name.txt"
}

case "${1:-}" in
    "")    prepare; run 100 80 1; run 75 35 2; run 40 60 3; run 100 100 4; run 30 15 5 ;;
    one)   prepare; run "${2:-100}" "${3:-80}" "${4:-1}" ;;
    seeds) prepare; for s in $(seq 1 "${4:-5}"); do run "${2:-100}" "${3:-80}" "$s"; done ;;
    *)     sed -n '2,8p' "$0"; exit 1 ;;
esac
