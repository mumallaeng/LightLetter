# Build the CNN IP from tx/cnn/rtl so the RTL has a single source of truth.
#
# Copies the IP definition from tx/cnn/ip/cnn_ip_1.0 (component.xml, AXI wrapper,
# drivers, GUI) into the project, fills its src/ with tx/cnn/rtl/*.v and
# tx/cnn/rtl/mem/*.mem, and rebuilds the IP file list from that src/ folder.
# Usage: cnn_ip_from_rtl <repo tx dir> <output ip_repo dir>; returns the ip_repo dir.

proc cnn_ip_from_rtl {tx_root out_repo} {
    set template [file join $tx_root cnn ip cnn_ip_1.0]
    set ip [file join $out_repo cnn_ip_1.0]
    file delete -force $out_repo
    file mkdir $out_repo
    file copy $template $ip
    file delete -force [file join $ip src]
    file mkdir [file join $ip src]
    foreach f [concat [glob [file join $tx_root cnn rtl *.v]] [glob [file join $tx_root cnn rtl mem *.mem]]] {
        file copy $f [file join $ip src]
    }

    set xml [file join $ip component.xml]
    set core [ipx::open_ipxact_file $xml]
    foreach g {xilinx_verilogsynthesis xilinx_verilogbehavioralsimulation} {
        set fg [ipx::get_file_groups $g -of_objects $core]
        foreach f [ipx::get_files -of_objects $fg] {
            set name [get_property NAME $f]
            if {[string match src/* $name]} { ipx::remove_file $name $fg }
        }
        foreach name [lsort [glob -tails -directory $ip src/*]] {
            set type [expr {[file extension $name] eq ".mem" ? "mem" : "verilogSource"}]
            set_property type $type [ipx::add_file $name $fg]
        }
    }
    ipx::update_checksums $core
    ipx::check_integrity $core
    ipx::save_core $core
    ipx::unload_core $xml
    return $out_repo
}
