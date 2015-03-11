########################################################################
##
## Copyright 2015 PMC-Sierra, Inc.
##
## Licensed under the Apache License, Version 2.0 (the "License"); you
## may not use this file except in compliance with the License. You may
## obtain a copy of the License at
## http://www.apache.org/licenses/LICENSE-2.0 Unless required by
## applicable law or agreed to in writing, software distributed under the
## License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
## CONDITIONS OF ANY KIND, either express or implied. See the License for
## the specific language governing permissions and limitations under the
## License.
##
########################################################################

########################################################################
##
##   Author: Logan Gunthorpe
##
##   Description:
##     Makefile which invokes Quartus to synthesize the FPGA
##
########################################################################


PART=5SGXMA7H2F35C2
PROJECT ?= capi-textswap
SOURCE_FILES = $(wildcard ../rtl/$(PROJECT)/*.vhdl) $(wildcard ../rtl/$(PROJECT)/*.vhd) $(wildcard ../rtl/$(PROJECT)/*.v)

export LM_LICENSE_FILE := $(LM_LICENSE_FILE):$(shell pwd)/../libs/psl_fpga/psl/psl_A000_license.dat

###################################################################
# Project Configuration:
# Specify the name of the design (project), the Quartus II Settings
# File (.qsf), and the list of source files used.
###################################################################

ASSIGNMENT_FILES = $(PROJECT).qpf $(PROJECT).qsf

###################################################################
# Main Targets #
# all: build everything
# clean: remove output files and database
###################################################################

default:rbf
all: rbf smart.log $(PROJECT).asm.rpt $(PROJECT).sta.rpt

clean:
	rm -rf *~ ./tcl/*~ *.rpt *.chg smart.log *.htm *.eqn *.pin *.sof *.pof db *.summary *.qpf *.qsf incremental_db *.smsg *.done *~ *.txt *.qxp *.bsf *.qarlog *.cmp *.v *.rbf nohup.out *.log hc_output reconfig_mif *.jdi *.qdf

map: smart.log $(PROJECT).map.rpt
fit: smart.log $(PROJECT).fit.rpt
asm: smart.log $(PROJECT).asm.rpt
sta: smart.log $(PROJECT).sta.rpt
qxp: smart.log $(PROJECT).qxp
rbf: smart.log $(PROJECT).rbf

smart: smart.log

###################################################################
# Executable Configuration
###################################################################
MAP_ARGS = --family=StratixV --part=$(PART)
FIT_ARGS = --part=$(PART)
ASM_ARGS =
STA_ARGS = --do_report_timing

###################################################################
# Target implementations
###################################################################
STAMP = echo done >

$(PROJECT).map.rpt: map.chg $(SOURCE_FILES) | $(ASSIGNMENT_FILES)
	quartus_map --64bit $(MAP_ARGS) $(PROJECT)
	$(STAMP) merge.chg

$(PROJECT).merge.rpt: merge.chg $(PROJECT).map.rpt
	quartus_cdb --64bit --merge $(PROJECT)
	$(STAMP) fit.chg

$(PROJECT).fit.rpt: fit.chg $(PROJECT).merge.rpt
	quartus_fit --64bit $(FIT_ARGS) $(PROJECT)
	$(STAMP) asm.chg $(STAMP) sta.chg

$(PROJECT).asm.rpt $(PROJECT).sof: asm.chg $(PROJECT).fit.rpt
	quartus_asm --64bit $(ASM_ARGS) $(PROJECT)

$(PROJECT).sta.rpt: sta.chg $(PROJECT).fit.rpt
	quartus_sta --64bit $(STA_ARGS) $(PROJECT)

$(PROJECT).rbf: $(PROJECT).asm.rpt $(PROJECT).sta.rpt
	quartus_cpf --64bit -c $(PROJECT).sof $(PROJECT).rbf

smart.log: $(ASSIGNMENT_FILES)
	quartus_sh --64bit --determine_smart_action $(PROJECT) > smart.log

###################################################################
# QXP File (uses scripts)
###################################################################

$(PROJECT).qxp: $(PROJECT).map.rpt
	./top_qxp_export.sh
	mv $(PROJECT).qxp ../Qxp/

###################################################################
# Project initialization
###################################################################

$(ASSIGNMENT_FILES): $(PROJECT).tcl ../libs/capi/fpga/*.tcl
	quartus_sh -t $(PROJECT).tcl
	quartus_sh --prepare $(PROJECT)
	#quartus_stp $(PROJECT) --enable --logic_analyzer_interface --lai_file=probes.lai

map.chg:
	$(STAMP) map.chg
fit.chg:
	$(STAMP) fit.chg
sta.chg:
	$(STAMP) sta.chg
asm.chg:
	$(STAMP) asm.chg
merge.chg:
	$(STAMP) merge.chg

program: $(PROJECT).sof
	quartus_pgm -c "USB-Blaster on cgy1-flash.pmc-sierra.internal" -m jtag -o S\;EPM2210 -o P\;$<

timing: $(PROJECT).sta.rpt

stats:
	altera_stats.py $(PROJECT)
