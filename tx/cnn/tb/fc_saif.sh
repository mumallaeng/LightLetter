#!/usr/bin/env bash
# SAIF of fc_top for Vivado power analysis: tb_fc_power on the routed netlist from vivado/fc_synth.tcl, in xsim.
#
#   vivado -mode batch -source ../../vivado/fc_synth.tcl     (tx/vivado, once per branch)
#   ./fc_saif.sh VALID READY [SEED]                       writes ../../vivado/fc_synth/saif_v<VALID>_r<READY>.saif
#   vivado -mode batch -source ../../vivado/fc_power.tcl -tclargs <saif>
#
# The netlist is zero-delay (funcsim), so the SAIF holds toggle counts, not glitches.
set -u
cd "$(dirname "$0")"
SYN=$PWD/../../vivado/fc_synth
BUILD=build/saif
V=${1:-100}; R=${2:-80}; S=${3:-1}
NAME=v${V}_r${R}

XVLOG=xvlog; XELAB=xelab; XSIM=xsim
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) XVLOG=xvlog.bat; XELAB=xelab.bat; XSIM=xsim.bat ;; esac
[ -f $SYN/fc_funcsim.v ] || { echo "$SYN/fc_funcsim.v missing - run vivado/fc_synth.tcl first"; exit 1; }
GLBL="$(dirname "$(command -v $XVLOG)")/../data/verilog/src/glbl.v"
[ -f "$GLBL" ] || { echo "glbl.v not found at $GLBL"; exit 1; }

mkdir -p $BUILD
cp vectors/fc_stim.mem vectors/fc3_out.mem $BUILD/
printf "%s\n" "--nolog" "-debug" "typical" "tb_fc_power" "glbl" "-L unisims_ver" "-s snap_$NAME" > $BUILD/xelab_$NAME.opt
# only the fc_top instance goes into the SAIF, not the testbench
printf "%s\n" "open_saif saif_$NAME.saif" "log_saif [get_objects -r /tb_fc_power/u_fc_top/*]" "run all" "close_saif" "quit" > $BUILD/saif_$NAME.tcl

(cd $BUILD &&
    $XVLOG --nolog ../../tb_fc_power.v $SYN/fc_funcsim.v "$GLBL" > xvlog.txt 2>&1 ||
        { grep -E "ERROR" xvlog.txt; echo "xvlog failed"; exit 1; }
    $XELAB -f xelab_$NAME.opt > xelab_$NAME.txt 2>&1 ||
        { grep -E "ERROR" xelab_$NAME.txt; echo "xelab failed"; exit 1; }
    $XSIM --nolog snap_$NAME -testplusarg VALID$V -testplusarg READY$R -testplusarg SEED$S -tclbatch saif_$NAME.tcl > xsim_$NAME.txt 2>&1)
grep -E "^\[|FAIL" $BUILD/xsim_$NAME.txt || { echo "no verdict - see $BUILD/xsim_$NAME.txt"; exit 1; }
cp $BUILD/saif_$NAME.saif $SYN/ && echo "saif: $SYN/saif_$NAME.saif"
