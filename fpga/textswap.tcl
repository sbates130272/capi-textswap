project_new textswap -overwrite

set_global_assignment -name TOP_LEVEL_ENTITY psl_fpga

set PSL_FPGA ../libs/psl_fpga
set LIBCAPI ../libs/capi

source $LIBCAPI/fpga/common.tcl
source $LIBCAPI/fpga/ibm_sources.tcl
source $LIBCAPI/fpga/pins.tcl
source $LIBCAPI/fpga/build_version.tcl
source $LIBCAPI/fpga/libcapi.tcl

foreach filename [glob ../rtl/*.vhd] {
    set_global_assignment -name VHDL_FILE $filename
}

foreach filename [glob ../rtl/*.v] {
    set_global_assignment -name SYSTEMVERILOG_FILE $filename
}
