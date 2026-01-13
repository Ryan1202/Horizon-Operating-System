## 英特尔手册中列出的实模式下可用的指令及已实现的指令（部分指令未验证是否正确执行）：

The following instructions make up the core instruction set for the 8086 processor. If backwards compatibility to the Intel 286 and Intel 8086 processors is required, only these instructions should be used in a new program written to run in real-address mode.
 
  - Move (MOV) instructions that move operands between general-purpose registers, segment registers, and between memory and general-purpose registers.
 - [x] MOV

 - The exchange (XCHG) instruction.
 - [x] XCHG

 - Load segment register instructions LDS and LES.
 - [x] LDS
 - [x] LES

 - Arithmetic instructions ADD, ADC, SUB, SBB, MUL, IMUL, DIV, IDIV, INC, DEC, CMP, and NEG.
 - [x] ADD
 - [x] ADC
 - [x] SUB
 - [x] SBB
 - [x] MUL
 - [x] IMUL
 - [x] DIV
 - [x] IDIV
 - [x] INC
 - [x] DEC
 - [x] CMP
 - [x] NEG

 - Logical instructions AND, OR, XOR, and NOT.
 - [x] AND
 - [x] OR
 - [x] XOR
 - [x] NOT

 - Decimal instructions DAA, DAS, AAA, AAS, AAM, and AAD.
 - [x] DAA
 - [x] DAS
 - [x] AAA
 - [x] AAS
 - [x] AAM
 - [x] AAD

 - Stack instructions PUSH and POP (to general-purpose registers and segment registers).
 - [x] PUSH
 - [x] POP

 - Type conversion instructions CWD, CDQ, CBW, and CWDE.
 - [x] CWD
 - [x] CDQ
 - [x] CBW
 - [x] CWDE

 - Shift and rotate instructions SAL, SHL, SHR, SAR, ROL, ROR, RCL, and RCR.
 - [x] SAL
 - [x] SHL
 - [x] SHR
 - [x] SAR
 - [x] ROL
 - [x] ROR
 - [x] RCL
 - [x] RCR

 - TEST instruction.
 - [x] TEST

 - Control instructions JMP, Jcc, CALL, RET, LOOP, LOOPE, and LOOPNE.
 - [x] JMP
 - [x] Jcc
 - [x] CALL
 - [x] RET
 - [x] LOOP
 - [x] LOOPE
 - [x] LOOPNE

 - Interrupt instructions INT n, INTO, and IRET.
 - [x] INT n
 - [x] INTO
 - [x] IRET

 - EFLAGS control instructions STC, CLC, CMC, CLD, STD, LAHF, SAHF, PUSHF, and POPF.
 - [x] STC
 - [x] CLC
 - [x] CMC
 - [x] CLD
 - [x] STD
 - [x] LAHF
 - [x] SAHF
 - [x] PUSHF
 - [x] POPF

 - I/O instructions IN, INS, OUT, and OUTS.
 - [x] IN
 - [x] INS
 - [x] OUT
 - [x] OUTS

 - Load effective address (LEA) instruction, and translate (XLATB) instruction.
 - [x] LEA
 - [x] XLATB

 - LOCK prefix.
 - [ ] LOCK

 - Repeat prefixes REP, REPE, REPZ, REPNE, and REPNZ.
 - [x] REP
 - [x] REPE
 - [x] REPZ
 - [x] REPNE
 - [x] REPNZ

 - Processor halt (HLT) instruction.
 - [x] HLT

 - No operation (NOP) instruction.
 - [x] NOP

The following instructions, added to later IA-32 processors (some in the Intel 286 processor and the remainder in the Intel386 processor), can be executed in real-address mode, if backwards compatibility to the Intel 8086 processor is not required.

 - Move (MOV) instructions that operate on the control and debug registers.
 - [ ] MOV

 - Load segment register instructions LSS, LFS, and LGS.
 - [x] LSS
 - [x] LFS
 - [x] LGS

 - Generalized multiply instructions and multiply immediate data.
 - [x]

 - Shift and rotate by immediate counts.
 - [x]

 - Stack instructions PUSHA, PUSHAD, POPA, POPAD, and PUSH immediate data.
 - [x] PUSHA
 - [x] PUSHAD
 - [x] POPA
 - [x] POPAD
 - [x] PUSH

 - Move with sign extension instructions MOVSX and MOVZX.
 - [x] MOVSX
 - [x] MOVZX

 - Long-displacement Jcc instructions.
 - [x] Jcc

 - Exchange instructions CMPXCHG, CMPXCHG8B, and XADD. 
 - [x] CMPXCHG
 - [ ] CMPXCHG8B
 - [x] XADD

 - String instructions MOVS, CMPS, SCAS, LODS, and STOS. 
 - [x] MOVS
 - [x] CMPS
 - [x] SCAS
 - [x] LODS
 - [x] STOS

 - Bit test and bit scan instructions BT, BTS, BTR, BTC, BSF, and BSR; the byte-set-on condition instruction SETcc; and the byte swap (BSWAP) instruction.
 - [x] BT
 - [x] BTS
 - [x] BTR
 - [x] BTC
 - [x] BSF
 - [x] BSR
 - [x] SETcc
 - [x] BSWAP

 - Double shift instructions SHLD and SHRD.
 - [x] SHLD
 - [x] SHRD

 - EFLAGS control instructions PUSHF and POPF.
 - [x] PUSHF
 - [x] POPF

 - ENTER and LEAVE control instructions.
 - [x] ENTER
 - [x] LEAVE

 - BOUND instruction.
 - [x] BOUND

 - CPU identification (CPUID) instruction.
 - [ ] CPUID

 - System instructions CLTS, INVD, WINVD, INVLPG, LGDT, SGDT, LIDT, SIDT, LMSW, SMSW, RDMSR, WRMSR, RDTSC, and RDPMC.
 - [ ] CLTS
 - [ ] INVD
 - [ ] WINVD
 - [ ] INVLPG
 - [ ] LGDT
 - [ ] SGDT
 - [ ] LIDT
 - [ ] SIDT
 - [ ] LMSW
 - [ ] SMSW
 - [ ] RDMSR
 - [ ] WRMSR
 - [ ] RDTSC
 - [ ] RDPMC

---

### （也许？）遥远的未来可能的改进方向：采用转译而非模拟的方式执行