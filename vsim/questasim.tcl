proc i  {} {
  if {[file isdirectory work]} {
  } else {
    vlib work
    vmap work work
  }
  if {[file isdirectory capi]} {
  } else {
    vlib capi
  }
}

proc r  {} {
  #compile vhdl files

  # libcapi
  eval {vcom -64 -just p  -work capi} [glob ../libs/capi/*/*.vhd]
  eval {vcom -64 -just e  -work capi} [glob ../libs/capi/*/*.vhd]
  eval {vcom -64 -skip pe -work capi} [glob ../libs/capi/*/*.vhd]

  # rtl
  eval {vcom -64 -just p}  [glob ../rtl/*.vhd]
  eval {vcom -64 -just e}  [glob ../rtl/*.vhd]
  eval {vcom -64 -skip pe} [glob ../rtl/*.vhd]

  #compile verilog files

  #rtl
  eval {vlog -64} [glob ../rtl/*.v]

  #toplevel
  eval {vlog -64 ../libs/pslse/afu_driver/verilog/top.v}
}


proc s  {} {
  vsim -t ns -novopt -c -pli ../libs/pslse/afu_driver/src/veriuser.sl +nowarnTSCALE work.top
  view wave
  radix h
  log * -r
  do wave.do
  view structure
  view signals
  view wave
  run -all
}

# auto recompile all and run sim
i
r
s

