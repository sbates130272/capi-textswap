onerror {resume}
quietly WaveActivateNextPane {} 0

add wave -noupdate -r -group afu -radix hex /top/a0/*

TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {0 ns} 0}
quietly wave cursor active 0
configure wave -namecolwidth 300
configure wave -valuecolwidth 300
configure wave -justifyvalue left
configure wave -signalnamewidth 0
configure wave -snapdistance 10
configure wave -datasetprefix 0
configure wave -rowmargin 4
configure wave -childrowmargin 2
configure wave -gridoffset 0
configure wave -gridperiod 1
configure wave -griddelta 40
configure wave -timeline 0
configure wave -timelineunits ns
config wave -signalnamewidth 1
update
WaveRestoreZoom {0 ns} {1 us}
