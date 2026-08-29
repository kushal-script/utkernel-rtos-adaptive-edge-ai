init
stm32h533re.dap dpreg 0 0x0000001E
stm32h533re.cpu arp_examine
halt
mww 0xE000E010 0
mww 0x40001000 0
catch { reg primask 1 }
set words [read_memory 0x0BF97000 32 2]
set sp [lindex $words 0]
set pc [lindex $words 1]
echo "vector: SP=[format 0x%08x $sp] PC=[format 0x%08x $pc]"
if {($sp & 0xFFF00000) != 0x20000000 || !($pc & 1)} {
    echo "BAD VECTOR, stopping"
    shutdown
}
reg msp $sp
reg pc $pc
resume
echo "JUMP OK"
shutdown
