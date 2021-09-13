/**
 * Copyright (c) 2020 Mark Owen https://www.quinapalus.com .
 *
 * Raspberry Pi (Trading) Ltd (Licensor) hereby grants to you a non-exclusive license to use the software solely on a
 * Raspberry Pi RP2040 device. No other use is permitted under the terms of this license.
 *
 * This software is also available from the copyright owner under GPLv2 licence.
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

.syntax unified
.cpu cortex-m0plus
.thumb

@ exported symbols

.global mufp_lib_start
.global mufp_fadd
.global mufp_fsub
.global mufp_fmul
.global mufp_fdiv
.global mufp_fcmp_combined
.global mufp_fcmp_fast
.global mufp_fcmp_fast_flags
.global mufp_fsqrt
.global mufp_float2int
.global mufp_float2fix
.global mufp_float2uint
.global mufp_float2ufix
.global mufp_int2float
.global mufp_fix2float
.global mufp_uint2float
.global mufp_ufix2float
.global mufp_int642float
.global mufp_fix642float
.global mufp_uint642float
.global mufp_ufix642float
.global mufp_fcos
.global mufp_fsin
.global mufp_fsincos
.global mufp_ftan
.global mufp_fatan2
.global mufp_fexp
.global mufp_fln
.global mufp_lib_end

#ifndef USE_HW_DIV
.equ use_hw_div,0
#else
.equ use_hw_div,1
#endif
.equ IOPORT       ,0xd0000000
.equ DIV_UDIVIDEND,0x00000060
.equ DIV_UDIVISOR ,0x00000064
.equ DIV_QUOTIENT ,0x00000070
.equ DIV_CSR      ,0x00000078

mufp_lib_start:

@ exchange r0<->r1, r2<->r3
xchxy:
 push {r0,r2,r14}
 mov r0,r1
 mov r2,r3
 pop {r1,r3,r15}

@ IEEE single in r0-> signed (two's complemennt) mantissa in r0 9Q23 (24 significant bits), signed exponent (bias removed) in r2
@ trashes r4; zero, denormal -> mantissa=+/-1, exponent=-380; Inf, NaN -> mantissa=+/-1, exponent=+640
unpackx:
 lsrs r2,r0,#23 @ save exponent and sign
 lsls r0,#9     @ extract mantissa
 lsrs r0,#9
 movs r4,#1
 lsls r4,#23
 orrs r0,r4     @ reinstate implied leading 1
 cmp r2,#255    @ test sign bit
 uxtb r2,r2     @ clear it
 bls 1f         @ branch on positive
 rsbs r0,#0     @ negate mantissa
1:
 subs r2,#1
 cmp r2,#254    @ zero/denormal/Inf/NaN?
 bhs 2f
 subs r2,#126   @ remove exponent bias: can now be -126..+127
 bx r14

2:              @ here with special-case values
 cmp r0,#0
 mov r0,r4      @ set mantissa to +1
 bpl 3f
 rsbs r0,#0     @ zero/denormal/Inf/NaN: mantissa=+/-1
3:
 subs r2,#126   @ zero/denormal: exponent -> -127; Inf, NaN: exponent -> 128
 lsls r2,#2     @ zero/denormal: exponent -> -508; Inf, NaN: exponent -> 512
 adds r2,#128   @ zero/denormal: exponent -> -380; Inf, NaN: exponent -> 640
 bx r14

@ normalise and pack signed mantissa in r0 nominally 3Q29, signed exponent in r2-> IEEE single in r0
@ trashes r4, preserves r1,r3
@ r5: "sticky bits", must be zero iff all result bits below r0 are zero for correct rounding
packx:
 lsrs r4,r0,#31 @ save sign bit
 lsls r4,r4,#31 @ sign now in b31
 bpl 2f         @ skip if positive
 cmp r5,#0
 beq 11f
 adds r0,#1     @ fiddle carry in to following rsb if sticky bits are non-zero
11:
 rsbs r0,#0     @ can now treat r0 as unsigned
packx0:
 bmi 3f         @ catch r0=0x80000000 case
2:
 subs r2,#1     @ normalisation loop
 adds r0,r0
 beq 1f         @ zero? special case
 bpl 2b         @ normalise so leading "1" in bit 31
3:
 adds r2,#129   @ (mis-)offset exponent
 bne 12f        @ special case: highest denormal can round to lowest normal
 adds r0,#0x80  @ in special case, need to add 256 to r0 for rounding
 bcs 4f         @ tripped carry? then have leading 1 in C as required
12:
 adds r0,#0x80  @ rounding
 bcs 4f         @ tripped carry? then have leading 1 in C as required (and result is even so can ignore sticky bits)
 cmp r5,#0
 beq 7f         @ sticky bits zero?
8:
 lsls r0,#1     @ remove leading 1
9:
 subs r2,#1     @ compensate exponent on this path
4:
 cmp r2,#254
 bge 5f         @ overflow?
 adds r2,#1     @ correct exponent offset
 ble 10f        @ denormal/underflow?
 lsrs r0,#9     @ align mantissa
 lsls r2,#23    @ align exponent
 orrs r0,r2     @ assemble exponent and mantissa
6:
 orrs r0,r4     @ apply sign
1:
 bx r14

5:
 movs r0,#0xff  @ create infinity
 lsls r0,#23
 b 6b

10:
 movs r0,#0     @ create zero
 bx r14

7:              @ sticky bit rounding case
 lsls r5,r0,#24 @ check bottom 8 bits of r0
 bne 8b         @ in rounding-tie case?
 lsrs r0,#9     @ ensure even result
 lsls r0,#10
 b 9b

.align 2
.ltorg

@ signed multiply r0 1Q23 by r1 4Q23, result in r0 7Q25, sticky bits in r5
@ trashes r3,r4
mul0:
 uxth r3,r0      @ Q23
 asrs r4,r1,#16  @ Q7
 muls r3,r4      @ L*H, Q30 signed
 asrs r4,r0,#16  @ Q7
 uxth r5,r1      @ Q23
 muls r4,r5      @ H*L, Q30 signed
 adds r3,r4      @ sum of middle partial products
 uxth r4,r0
 muls r4,r5      @ L*L, Q46 unsigned
 lsls r5,r4,#16  @ initialise sticky bits from low half of low partial product
 lsrs r4,#16     @ Q25
 adds r3,r4      @ add high half of low partial product to sum of middle partial products
                 @ (cannot generate carry by limits on input arguments)
 asrs r0,#16     @ Q7
 asrs r1,#16     @ Q7
 muls r0,r1      @ H*H, Q14 signed
 lsls r0,#11     @ high partial product Q25
 lsls r1,r3,#27  @ sticky
 orrs r5,r1      @ collect further sticky bits
 asrs r1,r3,#5   @ middle partial products Q25
 adds r0,r1      @ final result
 bx r14

.thumb_func
mufp_fcmp_combined:
@ *****
@ WARNING: this code is required by the wrapper functions to preserve R3
@ *****
 lsls r2,r0,#1
 lsrs r2,#24
 beq 1f
 cmp r2,#0xff
 bne 2f
1:
 lsrs r0,#23     @ clear mantissa if NaN or denormal
 lsls r0,#23
2:
 lsls r2,r1,#1
 lsrs r2,#24
 beq 1f
 cmp r2,#0xff
 bne 2f
1:
 lsrs r1,#23     @ clear mantissa if NaN or denormal
 lsls r1,#23
2:
.thumb_func
mufp_fcmp_fast_flags:
.thumb_func
mufp_fcmp_fast:
 movs r2,#1      @ initialise result
 eors r1,r0
 bmi 4f          @ opposite signs? then can proceed on basis of sign of x
 eors r1,r0      @ restore y
 bpl 1f
 rsbs r2,#0      @ both negative? flip comparison
1:
 cmp r0,r1
 bgt 2f
 blt 3f
5:
 movs r2,#0
3:
 rsbs r2,#0
2:
 subs r0,r2,#0
 bx r14
4:
 orrs r1,r0
 adds r1,r1
 beq 5b
 cmp r0,#0
 bge 2b
 b 3b

@ convert float to signed int, rounding towards -Inf, clamping
.thumb_func
mufp_float2int:
 movs r1,#0      @ fall through

@ convert float in r0 to signed fixed point in r0, clamping
.thumb_func
mufp_float2fix:
 push {r4,r14}
 bl unpackx
 movs r3,r2
 adds r3,#130
 bmi 6f          @ -0?
 add r2,r1       @ incorporate binary point position into exponent
 subs r2,#23     @ r2 is now amount of left shift required
 blt 1f          @ requires right shift?
 cmp r2,#7       @ overflow?
 ble 4f
3:               @ overflow
 asrs r1,r0,#31  @ +ve:0 -ve:0xffffffff
 mvns r1,r1      @ +ve:0xffffffff -ve:0
 movs r0,#1
 lsls r0,#31
5:
 eors r0,r1      @ +ve:0x7fffffff -ve:0x80000000 (unsigned path: 0xffffffff)
 pop {r4,r15}
1:
 rsbs r2,#0      @ right shift for r0, >0
 cmp r2,#32
 blt 2f          @ more than 32 bits of right shift?
 movs r2,#32
2:
 asrs r0,r0,r2
 pop {r4,r15}
6:
 movs r0,#0
 pop {r4,r15}

@ unsigned version
.thumb_func
mufp_float2uint:
 movs r1,#0      @ fall through
.thumb_func
mufp_float2ufix:
 push {r4,r14}
 bl unpackx
 add r2,r1       @ incorporate binary point position into exponent
 movs r1,r0
 bmi 5b          @ negative? return zero
 subs r2,#23     @ r2 is now amount of left shift required
 blt 1b          @ requires right shift?
 mvns r1,r0      @ ready to return 0xffffffff
 cmp r2,#8       @ overflow?
 bgt 5b
4:
 lsls r0,r0,r2   @ result fits, left shifted
 pop {r4,r15}


@ convert uint64 to float, rounding
.thumb_func
mufp_uint642float:
 movs r2,#0       @ fall through

@ convert unsigned 64-bit fix to float, rounding; number of r0:r1 bits after point in r2
.thumb_func
mufp_ufix642float:
 push {r4,r5,r14}
 cmp r1,#0
 bpl 3f          @ positive? we can use signed code
 lsls r5,r1,#31  @ contribution to sticky bits
 orrs r5,r0
 lsrs r0,r1,#1
 subs r2,#1
 b 4f

@ convert int64 to float, rounding
.thumb_func
mufp_int642float:
 movs r2,#0       @ fall through

@ convert signed 64-bit fix to float, rounding; number of r0:r1 bits after point in r2
.thumb_func
mufp_fix642float:
 push {r4,r5,r14}
3:
 movs r5,r0
 orrs r5,r1
 beq ret_pop45   @ zero? return +0
 asrs r5,r1,#31  @ sign bits
2:
 asrs r4,r1,#24  @ try shifting 7 bits at a time
 cmp r4,r5
 bne 1f          @ next shift will overflow?
 lsls r1,#7
 lsrs r4,r0,#25
 orrs r1,r4
 lsls r0,#7
 adds r2,#7
 b 2b
1:
 movs r5,r0
 movs r0,r1
4:
 rsbs r2,#0
 adds r2,#32+29
 b packret

@ convert signed int to float, rounding
.thumb_func
mufp_int2float:
 movs r1,#0      @ fall through

@ convert signed fix to float, rounding; number of r0 bits after point in r1
.thumb_func
mufp_fix2float:
 push {r4,r5,r14}
1:
 movs r2,#29
 subs r2,r1      @ fix exponent
packretns:       @ pack and return, sticky bits=0
 movs r5,#0
packret:         @ common return point: "pack and return"
 bl packx
ret_pop45:
 pop {r4,r5,r15}


@ unsigned version
.thumb_func
mufp_uint2float:
 movs r1,#0      @ fall through
.thumb_func
mufp_ufix2float:
 push {r4,r5,r14}
 cmp r0,#0
 bge 1b          @ treat <2^31 as signed
 movs r2,#30
 subs r2,r1      @ fix exponent
 lsls r5,r0,#31  @ one sticky bit
 lsrs r0,#1
 b packret

@ All the scientific functions are implemented using the CORDIC algorithm. For notation,
@ details not explained in the comments below, and a good overall survey see
@ "50 Years of CORDIC: Algorithms, Architectures, and Applications" by Meher et al.,
@ IEEE Transactions on Circuits and Systems Part I, Volume 56 Issue 9.

@ Register use:
@ r0: x
@ r1: y
@ r2: z/omega
@ r3: coefficient pointer
@ r4,r12: m
@ r5: i (shift)

cordic_start: @ initialisation
 movs r5,#0   @ initial shift=0
 mov r12,r4
 b 5f

cordic_vstep: @ one step of algorithm in vector mode
 cmp r1,#0    @ check sign of y
 bgt 4f
 b 1f
cordic_rstep: @ one step of algorithm in rotation mode
 cmp r2,#0    @ check sign of angle
 bge 1f
4:
 subs r1,r6   @ negative rotation: y=y-(x>>i)
 rsbs r7,#0
 adds r2,r4   @ accumulate angle
 b 2f
1:
 adds r1,r6   @ positive rotation: y=y+(x>>i)
 subs r2,r4   @ accumulate angle
2:
 mov r4,r12
 muls r7,r4   @ apply sign from m
 subs r0,r7   @ finish rotation: x=x{+/-}(y>>i)
5:
 ldmia r3!,{r4} @ fetch next angle from table and bump pointer
 lsrs r4,#1   @ repeated angle?
 bcs 3f
 adds r5,#1   @ adjust shift if not
3:
 mov r6,r0
 asrs r6,r5   @ x>>i
 mov r7,r1
 asrs r7,r5   @ y>>i
 lsrs r4,#1   @ shift end flag into carry
 bx r14

@ CORDIC rotation mode
cordic_rot:
 push {r6,r7,r14}
 bl cordic_start   @ initialise
1:
 bl cordic_rstep
 bcc 1b            @ step until table finished
 asrs r6,r0,#14    @ remaining small rotations can be linearised: see IV.B of paper referenced above
 asrs r7,r1,#14
 asrs r2,#3
 muls r6,r2        @ all remaining CORDIC steps in a multiplication
 muls r7,r2
 mov r4,r12
 muls r7,r4
 asrs r6,#12
 asrs r7,#12
 subs r0,r7        @ x=x{+/-}(yz>>k)
 adds r1,r6        @ y=y+(xz>>k)
cordic_exit:
 pop {r6,r7,r15}

@ CORDIC vector mode
cordic_vec:
 push {r6,r7,r14}
 bl cordic_start   @ initialise
1:
 bl cordic_vstep
 bcc 1b            @ step until table finished
4:
 cmp r1,#0         @ continue as in cordic_vstep but without using table; x is not affected as y is small
 bgt 2f            @ check sign of y
 adds r1,r6        @ positive rotation: y=y+(x>>i)
 subs r2,r4        @ accumulate angle
 b 3f
2:
 subs r1,r6        @ negative rotation: y=y-(x>>i)
 adds r2,r4        @ accumulate angle
3:
 asrs r6,#1
 asrs r4,#1        @ next "table entry"
 bne 4b
 b cordic_exit

.thumb_func
mufp_fsin:
.thumb_func
mufp_fsincos:         @ calculate sin and cos using CORDIC rotation method
 push {r4,r5,r14}
 movs r1,#24
 bl mufp_float2fix    @ range reduction by repeated subtraction/addition in fixed point
 ldr r4,pi_q29
 lsrs r4,#4          @ 2pi Q24
1:
 subs r0,r4
 bge 1b
1:
 adds r0,r4
 bmi 1b              @ now in range 0..2pi
 lsls r2,r0,#2       @ z Q26
 lsls r5,r4,#1       @ pi Q26 (r4=pi/2 Q26)
 ldr r0,=#0x136e9db4 @ initialise CORDIC x,y with scaling
 movs r1,#0
1:
 cmp r2,r4           @ >pi/2?
 blt 2f
 subs r2,r5          @ reduce range to -pi/2..pi/2
 rsbs r0,#0          @ rotate vector by pi
 b 1b
2:
 lsls r2,#3          @ Q29
 adr r3,tab_cc       @ circular coefficients
 movs r4,#1          @ m=1
 bl cordic_rot
 adds r1,#9          @ fiddle factor to make sin(0)==0
 movs r2,#0          @ exponents to zero
 movs r3,#0
 movs r5,#0          @ no sticky bits
 bl clampx
 bl packx            @ pack cosine
 bl xchxy
 bl clampx
 b packretns         @ pack sine

.thumb_func
mufp_fcos:
 push {r14}
 bl mufp_fsin
 mov r0,r1           @ extract cosine result
 pop {r15}

@ force r0 to lie in range [-1,1] Q29
clampx:
 movs r4,#1
 lsls r4,#29
 cmp r0,r4
 bgt 1f
 rsbs r4,#0
 cmp r0,r4
 ble 1f
 bx r14
1:
 movs r0,r4
 bx r14

.thumb_func
mufp_ftan:
 push {r4,r5,r6,r14}
 bl mufp_fsin         @ sine in r0/r2, cosine in r1/r3
 b fdiv_n             @ sin/cos

.thumb_func
mufp_fexp:            @ calculate cosh and sinh using rotation method; add to obtain exp
 push {r4,r5,r14}
 movs r1,#24
 bl mufp_float2fix    @ Q24: covers entire valid input range
 asrs r1,r0,#16      @ Q8
 ldr r2,=#5909       @ log_2(e) Q12
 muls r1,r2          @ estimate exponent of result Q20
 asrs r1,#19         @ Q1
 adds r1,#1          @ rounding
 asrs r1,#1          @ rounded estimate of exponent of result
 push {r1}           @ save for later
 lsls r2,r0,#5       @ Q29
 ldr r0,=#0x162e42ff @ ln(2) Q29
 muls r1,r0          @ accurate contribution of estimated exponent
 subs r2,r1          @ residual to be exponentiated, approximately -.5..+.5 Q29
 ldr r0,=#0x2c9e15ca @ initialise CORDIC x,y with scaling
 movs r1,#0
 adr r3,tab_ch       @ hyperbolic coefficients
 mvns r4,r1          @ m=-1
 bl cordic_rot       @ calculate cosh and sinh
 add r0,r1           @ exp=cosh+sinh
 pop {r2}            @ recover exponent
 b packretns         @ pack result

@ keep the old square root function in case we need to save space in future
@ and because it is used by fln
.thumb_func
mufp_fsqrt_ln:       @ calculate sqrt and ln using vector method
 push {r4,r5,r14}
 bl unpackx
 movs r1,r0          @ -ve argument?
 bmi 3f              @ return -Inf, -Inf
 ldr r1,=#0x0593C2B9 @ scale factor for CORDIC
 bl mul0             @ Q29
 asrs r1,r2,#1       @ halve exponent
 bcc 1f
 adds r1,#1          @ was odd: add 1 and shift mantissa
 asrs r0,#1
1:
 push {r1}           @ save exponent/2 for later
 mov r1,r0
 ldr r3,=#0x0593C2B9 @ re-use constant
 lsls r3,#2
 adds r0,r3          @ "a+1"
 subs r1,r3          @ "a-1"
 movs r2,#0
 adr r3,tab_ch       @ hyperbolic coefficients
 mvns r4,r2          @ m=-1
 bl cordic_vec
 mov r1,r2           @ keep ln result
 pop {r2}            @ retrieve exponent/2
2:
 movs r3,r2
 b packretns         @ pack sqrt result

3:
 movs r2,#255
 b 2b

.thumb_func
mufp_fln:
 push {r4,r5,r14}
 bl mufp_fsqrt_ln        @ get unpacked ln in r1/r3; exponent has been halved
 cmp r3,#70              @ ln(Inf)?
 bgt 2f                  @ return Inf
 rsbs r3,#0
 cmp r3,#70
 bgt 1f                  @ ln(0)? return -Inf
3:
 ldr r0,=#0x0162e430     @ ln(4) Q24
 muls r0,r3              @ contribution from negated, halved exponent
 adds r1,#8              @ round result of ln
 asrs r1,#4              @ Q24
 subs r0,r1,r0           @ add in contribution from (negated) exponent
 movs r2,#5              @ pack expects Q29
 b packretns
1:
 mvns r0,r0              @ make result -Inf
2:
 movs r2,#255
 b packretns

.thumb_func
mufp_fatan2:
 push {r4,r5,r14}

@ unpack arguments and shift one down to have common exponent
 bl unpackx
 bl xchxy
 bl unpackx
 lsls r0,r0,#5  @ Q28
 lsls r1,r1,#5  @ Q28
 adds r4,r2,r3  @ this is -760 if both arguments are 0 and at least -380-126=-506 otherwise
 asrs r4,#9
 adds r4,#1
 bmi 2f         @ force y to 0 proper, so result will be zero
 subs r4,r2,r3  @ calculate shift
 bge 1f         @ ex>=ey?
 rsbs r4,#0     @ make shift positive
 asrs r0,r4
 cmp r4,#28
 blo 3f
 asrs r0,#31
 b 3f
1:
 asrs r1,r4
 cmp r4,#28
 blo 3f
2:
@ here |x|>>|y| or both x and y are ±0
 cmp r0,#0
 bge 4f         @ x positive, return signed 0
 ldr r0,pi_q29  @ x negative, return +/- pi
 asrs r1,#31
 eors r0,r1
 b 7f
4:
 asrs r0,r1,#31
 b 7f
3:
 movs r2,#0              @ initial angle
 cmp r0,#0               @ x negative
 bge 5f
 rsbs r0,#0              @ rotate to 1st/4th quadrants
 rsbs r1,#0
 ldr r2,pi_q29           @ pi Q29
5:
 adr r3,tab_cc           @ circular coefficients
 movs r4,#1              @ m=1
 bl cordic_vec           @ also produces magnitude (with scaling factor 1.646760119), which is discarded
 mov r0,r2               @ result here is -pi/2..3pi/2 Q29
@ asrs r2,#29
@ subs r0,r2
 ldr r2,pi_q29           @ pi Q29
 adds r4,r0,r2           @ attempt to fix -3pi/2..-pi case
 bcs 6f                  @ -pi/2..0? leave result as is
 subs r4,r0,r2           @ <pi? leave as is
 bmi 6f
 subs r0,r4,r2           @ >pi: take off 2pi
6:
 subs r0,#1              @ fiddle factor so atan2(0,1)==0
7:
 movs r2,#0              @ exponent for pack
 b packretns

.align 2
.ltorg

@ first entry in following table is pi Q29
pi_q29:
@ circular CORDIC coefficients: atan(2^-i), b0=flag for preventing shift, b1=flag for end of table
tab_cc:
.word 0x1921fb54*4+1     @ no shift before first iteration
.word 0x0ed63383*4+0
.word 0x07d6dd7e*4+0
.word 0x03fab753*4+0
.word 0x01ff55bb*4+0
.word 0x00ffeaae*4+0
.word 0x007ffd55*4+0
.word 0x003fffab*4+0
.word 0x001ffff5*4+0
.word 0x000fffff*4+0
.word 0x0007ffff*4+0
.word 0x00040000*4+0
.word 0x00020000*4+0+2   @ +2 marks end

@ hyperbolic CORDIC coefficients: atanh(2^-i), flags as above
tab_ch:
.word 0x1193ea7b*4+0
.word 0x1193ea7b*4+1   @ repeat i=1
.word 0x082c577d*4+0
.word 0x04056247*4+0
.word 0x0200ab11*4+0
.word 0x0200ab11*4+1   @ repeat i=4
.word 0x01001559*4+0
.word 0x008002ab*4+0
.word 0x00400055*4+0
.word 0x0020000b*4+0
.word 0x00100001*4+0
.word 0x00080001*4+0
.word 0x00040000*4+0
.word 0x00020000*4+0
.word 0x00020000*4+1+2 @ repeat i=12

.align 2
.thumb_func
mufp_fsub:
 ldr r2,=#0x80000000
 eors r1,r2    @ flip sign on second argument
@ drop into fadd, on .align2:ed boundary

.thumb_func
mufp_fadd:
 push {r4,r5,r6,r14}
 asrs r4,r0,#31
 lsls r2,r0,#1
 lsrs r2,#24     @ x exponent
 beq fa_xe0
 cmp r2,#255
 beq fa_xe255
fa_xe:
 asrs r5,r1,#31
 lsls r3,r1,#1
 lsrs r3,#24     @ y exponent
 beq fa_ye0
 cmp r3,#255
 beq fa_ye255
fa_ye:
 ldr r6,=#0x007fffff
 ands r0,r0,r6   @ extract mantissa bits
 ands r1,r1,r6
 adds r6,#1      @ r6=0x00800000
 orrs r0,r0,r6   @ set implied 1
 orrs r1,r1,r6
 eors r0,r0,r4   @ complement...
 eors r1,r1,r5
 subs r0,r0,r4   @ ... and add 1 if sign bit is set: 2's complement
 subs r1,r1,r5
 subs r5,r3,r2   @ ye-xe
 subs r4,r2,r3   @ xe-ye
 bmi fa_ygtx
@ here xe>=ye
 cmp r4,#30
 bge fa_xmgty    @ xe much greater than ye?
 adds r5,#32
 movs r3,r2      @ save exponent
@ here y in r1 must be shifted down r4 places to align with x in r0
 movs r2,r1
 lsls r2,r2,r5   @ keep the bits we will shift off the bottom of r1
 asrs r1,r1,r4
 b fa_0

fa_ymgtx:
 movs r2,#0      @ result is just y
 movs r0,r1
 b fa_1
fa_xmgty:
 movs r3,r2      @ result is just x
 movs r2,#0
 b fa_1

fa_ygtx:
@ here ye>xe
 cmp r5,#30
 bge fa_ymgtx    @ ye much greater than xe?
 adds r4,#32
@ here x in r0 must be shifted down r5 places to align with y in r1
 movs r2,r0
 lsls r2,r2,r4   @ keep the bits we will shift off the bottom of r0
 asrs r0,r0,r5
fa_0:
 adds r0,r1      @ result is now in r0:r2, possibly highly denormalised or zero; exponent in r3
 beq fa_9        @ if zero, inputs must have been of identical magnitude and opposite sign, so return +0
fa_1:
 lsrs r1,r0,#31  @ sign bit
 beq fa_8
 mvns r0,r0
 rsbs r2,r2,#0
 bne fa_8
 adds r0,#1
fa_8:
 adds r6,r6
@ r6=0x01000000
 cmp r0,r6
 bhs fa_2
fa_3:
 adds r2,r2      @ normalisation loop
 adcs r0,r0
 subs r3,#1      @ adjust exponent
 cmp r0,r6
 blo fa_3
fa_2:
@ here r0:r2 is the result mantissa 0x01000000<=r0<0x02000000, r3 the exponent, and r1 the sign bit
 lsrs r0,#1
 bcc fa_4
@ rounding bits here are 1:r2
 adds r0,#1      @ round up
 cmp r2,#0
 beq fa_5        @ sticky bits all zero?
fa_4:
 cmp r3,#254
 bhs fa_6        @ exponent too large or negative?
 lsls r1,#31     @ pack everything
 add r0,r1
 lsls r3,#23
 add r0,r3
fa_end:
 pop {r4,r5,r6,r15}

fa_9:
 cmp r2,#0       @ result zero?
 beq fa_end      @ return +0
 b fa_1

fa_5:
 lsrs r0,#1
 lsls r0,#1      @ round to even
 b fa_4

fa_6:
 bge fa_7
@ underflow
@ can handle denormals here
 lsls r0,r1,#31  @ result is signed zero
 pop {r4,r5,r6,r15}
fa_7:
@ overflow
 lsls r0,r1,#8
 adds r0,#255
 lsls r0,#23     @ result is signed infinity
 pop {r4,r5,r6,r15}


fa_xe0:
@ can handle denormals here
 subs r2,#32
 adds r2,r4       @ exponent -32 for +Inf, -33 for -Inf
 b fa_xe

fa_xe255:
@ can handle NaNs here
 lsls r2,#8
 add r2,r2,r4 @ exponent ~64k for +Inf, ~64k-1 for -Inf
 b fa_xe

fa_ye0:
@ can handle denormals here
 subs r3,#32
 adds r3,r5       @ exponent -32 for +Inf, -33 for -Inf
 b fa_ye

fa_ye255:
@ can handle NaNs here
 lsls r3,#8
 add r3,r3,r5 @ exponent ~64k for +Inf, ~64k-1 for -Inf
 b fa_ye


.align 2
.thumb_func
mufp_fmul:
 push {r7,r14}
 mov r2,r0
 eors r2,r1       @ sign of result
 lsrs r2,#31
 lsls r2,#31
 mov r14,r2
 lsls r0,#1
 lsls r1,#1
 lsrs r2,r0,#24 @ xe
 beq fm_xe0
 cmp r2,#255
 beq fm_xe255
fm_xe:
 lsrs r3,r1,#24 @ ye
 beq fm_ye0
 cmp r3,#255
 beq fm_ye255
fm_ye:
 adds r7,r2,r3  @ exponent of result (will possibly be incremented)
 subs r7,#128   @ adjust bias for packing
 lsls r0,#8     @ x mantissa
 lsls r1,#8       @ y mantissa
 lsrs r0,#9
 lsrs r1,#9

 adds r2,r0,r1    @ for later
 mov r12,r2
 lsrs r2,r0,#7    @ x[22..7] Q16
 lsrs r3,r1,#7    @ y[22..7] Q16
 muls r2,r2,r3    @ result [45..14] Q32: never an overestimate and worst case error is 2*(2^7-1)*(2^23-2^7)+(2^7-1)^2 = 2130690049 < 2^31
 muls r0,r0,r1    @ result [31..0] Q46
 lsrs r2,#18      @ result [45..32] Q14
 bcc 1f
 cmp r0,#0
 bmi 1f
 adds r2,#1       @ fix error in r2
1:
 lsls r3,r0,#9    @ bits off bottom of result
 lsrs r0,#23      @ Q23
 lsls r2,#9
 adds r0,r2       @ cut'n'shut
 add r0,r12       @ implied 1*(x+y) to compensate for no insertion of implied 1s
@ result-1 in r3:r0 Q23+32, i.e., in range [0,3)

 lsrs r1,r0,#23
 bne fm_0        @ branch if we need to shift down one place
@ here 1<=result<2
 cmp r7,#254
 bhs fm_3a       @ catches both underflow and overflow
 lsls r3,#1       @ sticky bits at top of R3, rounding bit in carry
 bcc fm_1        @ no rounding
 beq fm_2        @ rounding tie?
 adds r0,#1       @ round up
fm_1:
 adds r7,#1       @ for implied 1
 lsls r7,#23     @ pack result
 add r0,r7
 add r0,r14
 pop {r7,r15}
fm_2:            @ rounding tie
 adds r0,#1
fm_3:
 lsrs r0,#1
 lsls r0,#1      @ clear bottom bit
 b fm_1

@ here 1<=result-1<3
fm_0:
 adds r7,#1      @ increment exponent
 cmp r7,#254
 bhs fm_3b       @ catches both underflow and overflow
 lsrs r0,#1      @ shift mantissa down
 bcc fm_1a        @ no rounding
 adds r0,#1      @ assume we will round up
 cmp r3,#0        @ sticky bits
 beq fm_3c        @ rounding tie?
fm_1a:
 adds r7,r7
 adds r7,#1       @ for implied 1
 lsls r7,#22      @ pack result
 add r0,r7
 add r0,r14
 pop {r7,r15}

fm_3c:
 lsrs r0,#1
 lsls r0,#1       @ clear bottom bit
 b fm_1a

fm_xe0:
 subs r2,#16
fm_xe255:
 lsls r2,#8
 b fm_xe
fm_ye0:
 subs r3,#16
fm_ye255:
 lsls r3,#8
 b fm_ye

@ here the result is under- or overflowing
fm_3b:
 bge fm_4        @ branch on overflow
@ trap case where result is denormal 0x007fffff + 0.5ulp or more
 adds r7,#1      @ exponent=-1?
 bne fm_5
@ corrected mantissa will be >= 3.FFFFFC (0x1fffffe Q23)
@ so r0 >= 2.FFFFFC (0x17ffffe Q23)
 adds r0,#2
 lsrs r0,#23
 cmp r0,#3
 bne fm_5
 b fm_6

fm_3a:
 bge fm_4        @ branch on overflow
@ trap case where result is denormal 0x007fffff + 0.5ulp or more
 adds r7,#1      @ exponent=-1?
 bne fm_5
 adds r0,#1      @ mantissa=0xffffff (i.e., r0=0x7fffff)?
 lsrs r0,#23
 beq fm_5
fm_6:
 movs r0,#1      @ return smallest normal
 lsls r0,#23
 add r0,r14
 pop {r7,r15}

fm_5:
 mov r0,r14
 pop {r7,r15}
fm_4:
 movs r0,#0xff
 lsls r0,#23
 add r0,r14
 pop {r7,r15}

@ This version of the division algorithm uses external divider hardware to estimate the
@ reciprocal of the divisor to about 14 bits; then a multiplication step to get a first
@ quotient estimate; then the remainder based on this estimate is used to calculate a
@ correction to the quotient. The result is good to about 27 bits and so we only need
@ to calculate the exact remainder when close to a rounding boundary.
.align 2
.thumb_func
mufp_fdiv:
 push {r4,r5,r6,r14}
fdiv_n:
 movs r4,#1
 rsbs r6,r4,#0 @ r6=0xffffffff
 lsls r4,#23   @ implied 1 position
 lsls r2,r1,#9 @ clear out sign and exponent
 lsrs r2,r2,#9
 orrs r2,r2,r4 @ divisor mantissa Q23 with implied 1
 lsrs r3,r2,#7 @ divisor Q16

@ here we want to do an unsigned division of R6 by R3 and put the result in R5, though we don't need it for a bit

.if use_hw_div

 movs r5,#IOPORT>>24
 lsls r5,#24
 str r6,[r5,#DIV_UDIVIDEND]
 str r3,[r5,#DIV_UDIVISOR]

.else

@ this is not beautiful; could be replaced by better code that uses knowledge of divisor range
 push {r0,r1,r2}
 mov r0,r6
 mov r1,r3
 bl __aeabi_uidiv
 mov r5,r0
 pop {r0,r1,r2}

.endif

@ here
@ r0=packed dividend
@ r1=packed divisor
@ r2=divisor mantissa Q23
@ r4=1<<23
@ r5=reciprocal estimate Q16 OR base address of IOPORT space

 lsrs r6,r0,#23
 uxtb r3,r6        @ dividend exponent
 lsls r0,#9
 lsrs r0,#9
 orrs r0,r0,r4     @ dividend mantissa Q23

 lsrs r1,#23
 eors r6,r1        @ sign of result in bit 8
 lsrs r6,#8
 lsls r6,#31       @ sign of result in bit 31, other bits clear

.if use_hw_div
@ the above code takes long enough to guarantee the result is ready
 ldr r5,[r5,#DIV_QUOTIENT]
.endif

 uxtb r1,r1        @ divisor exponent
 cmp r1,#0
 beq retinf
 cmp r1,#255
 beq 20f           @ divisor is infinite
 cmp r3,#0
 beq retzero
 cmp r3,#255
 beq retinf
 subs r3,r1        @ initial result exponent (no bias)
 adds r3,#125      @ add bias

 lsrs r1,r0,#8     @ dividend mantissa Q15

@ here
@ r0=dividend mantissa Q23
@ r1=dividend mantissa Q15
@ r2=divisor mantissa Q23
@ r3=initial result exponent
@ r4=1<<23
@ r5=reciprocal estimate Q16
@ r6b31=sign of result

 muls r1,r5

 lsrs r1,#16       @ Q15 qu0=(q15)(u*y0);
 lsls r0,r0,#15    @ dividend Q38
 movs r4,r2
 muls r4,r1        @ Q38 qu0*x
 subs r4,r0,r4     @ Q38 re0=(y<<15)-qu0*x; note this remainder is signed
 asrs r4,#10
 muls r4,r5        @ Q44 qu1=(re0>>10)*u; this quotient correction is also signed
 asrs r4,#16       @ Q28
 lsls r1,#13
 adds r1,r1,r4     @ Q28 qu=(qu0<<13)+(qu1>>16);

@ here
@ r0=dividend mantissa Q38
@ r1=quotient Q28
@ r2=divisor mantissa Q23
@ r3=initial result exponent
@ r6b31=sign of result

 lsrs r4,r1,#28
 bne 1f
@ here the quotient is less than 1<<28 (i.e., result mantissa <1.0)

 adds r1,#5
 lsrs r4,r1,#4     @ rounding + small reduction in systematic bias
 bcc 2f            @ skip if we are not near a rounding boundary
 lsrs r1,#3        @ quotient Q25
 lsls r0,#10       @ dividend mantissa Q48
 muls r1,r1,r2     @ quotient*divisor Q48
 subs r0,r0,r1     @ remainder Q48
 bmi 2f
 b 3f

1:
@ here the quotient is at least 1<<28 (i.e., result mantissa >=1.0)

 adds r3,#1        @ bump exponent (and shift mantissa down one more place)
 adds r1,#9
 lsrs r4,r1,#5     @ rounding + small reduction in systematic bias
 bcc 2f            @ skip if we are not near a rounding boundary

 lsrs r1,#4        @ quotient Q24
 lsls r0,#9        @ dividend mantissa Q47
 muls r1,r1,r2     @ quotient*divisor Q47
 subs r0,r0,r1     @ remainder Q47
 bmi 2f
3:
 adds r4,#1        @ increment quotient as we are above the rounding boundary

@ here
@ r3=result exponent
@ r4=correctly rounded quotient Q23 in range [1,2] *note closed interval*
@ r6b31=sign of result

2:
 cmp r3,#254
 bhs 10f           @ this catches both underflow and overflow
 lsls r1,r3,#23
 adds r0,r4,r1
 adds r0,r6
 pop {r4,r5,r6,r15}

@ here divisor is infinite; dividend exponent in r3
20:
 cmp r3,#255
 bne retzero

retinf:
 movs r0,#255
21:
 lsls r0,#23
 orrs r0,r6
 pop {r4,r5,r6,r15}

10:
 bge retinf       @ overflow?
 adds r1,r3,#1
 bne retzero      @ exponent <-1? return 0
@ here exponent is exactly -1
 lsrs r1,r4,#25
 bcc retzero      @ mantissa is not 01000000?
@ return minimum normal
 movs r0,#1
 lsls r0,#23
 orrs r0,r6
 pop {r4,r5,r6,r15}

retzero:
 movs r0,r6
 pop {r4,r5,r6,r15}

@ The square root routine uses an initial approximation to the reciprocal of the square root of the argument based
@ on the top four bits of the mantissa (possibly shifted one place to make the exponent even). It then performs two
@ Newton-Raphson iterations, resulting in about 14 bits of accuracy. This reciprocal is then multiplied by
@ the original argument to produce an approximation to the result, again with about 14 bits of accuracy.
@ Then a remainder is calculated, and multiplied by the reciprocal estiamte to generate a correction term
@ giving a final answer to about 28 bits of accuracy. A final remainder calculation rounds to the correct
@ result if necessary.
@ Again, the fixed-point calculation is carefully implemented to preserve accuracy, and similar comments to those
@ made above on the fast division routine apply.
@ The reciprocal square root calculation has been tested for all possible (possibly shifted) input mantissa values.
.align 2
.thumb_func
mufp_fsqrt:
 push {r4}
 lsls r1,r0,#1
 bcs sq_0         @ negative?
 lsls r1,#8
 lsrs r1,#9       @ mantissa
 movs r2,#1
 lsls r2,#23
 adds r1,r2       @ insert implied 1
 lsrs r2,r0,#23   @ extract exponent
 beq sq_2         @ zero?
 cmp r2,#255      @ infinite?
 beq sq_1
 adds r2,#125     @ correction for packing
 asrs r2,#1       @ exponent/2, LSB into carry
 bcc 1f
 lsls r1,#1       @ was even: double mantissa; mantissa y now 1..4 Q23
1:
 adr r4,rsqrtapp-4@ first four table entries are never accessed because of the mantissa's leading 1
 lsrs r3,r1,#21   @ y Q2
 ldrb r4,[r4,r3]  @ initial approximation to reciprocal square root a0 Q8

 lsrs r0,r1,#7    @ y Q16: first Newton-Raphson iteration
 muls r0,r4       @ a0*y Q24
 muls r0,r4       @ r0=p0=a0*y*y Q32
 asrs r0,#12      @ r0 Q20
 muls r0,r4       @ dy0=a0*r0 Q28
 asrs r0,#13      @ dy0 Q15
 lsls r4,#8       @ a0 Q16
 subs r4,r0       @ a1=a0-dy0/2 Q16-Q15/2 -> Q16
 adds r4,#170     @ mostly remove systematic error in this approximation: gains approximately 1 bit

 movs r0,r4       @ second Newton-Raphson iteration
 muls r0,r0       @ a1*a1 Q32
 lsrs r0,#15      @ a1*a1 Q17
 lsrs r3,r1,#8    @ y Q15
 muls r0,r3       @ r1=p1=a1*a1*y Q32
 asrs r0,#12      @ r1 Q20
 muls r0,r4       @ dy1=a1*r1 Q36
 asrs r0,#21      @ dy1 Q15
 subs r4,r0       @ a2=a1-dy1/2 Q16-Q15/2 -> Q16

 muls r3,r4       @ a3=y*a2 Q31
 lsrs r3,#15      @ a3 Q16
@ here a2 is an approximation to the reciprocal square root
@ and a3 is an approximation to the square root
 movs r0,r3
 muls r0,r0       @ a3*a3 Q32
 lsls r1,#9       @ y Q32
 subs r0,r1,r0    @ r2=y-a3*a3 Q32 remainder
 asrs r0,#5       @ r2 Q27
 muls r4,r0       @ r2*a2 Q43
 lsls r3,#7       @ a3 Q23
 asrs r0,r4,#15   @ r2*a2 Q28
 adds r0,#16      @ rounding to Q24
 asrs r0,r0,#6    @ r2*a2 Q22
 add r3,r0        @ a4 Q23: candidate final result
 bcc sq_3         @ near rounding boundary? skip if no rounding needed
 mov r4,r3
 adcs r4,r4       @ a4+0.5ulp Q24
 muls r4,r4       @ Q48
 lsls r1,#16      @ y Q48
 subs r1,r4       @ remainder Q48
 bmi sq_3
 adds r3,#1       @ round up
sq_3:
 lsls r2,#23      @ pack exponent
 adds r0,r2,r3
sq_6:
 pop {r4}
 bx r14

sq_0:
 lsrs r1,#24
 beq sq_2         @ -0: return it
@ here negative and not -0: return -Inf
 asrs r0,#31
sq_5:
 lsls r0,#23
 b sq_6
sq_1:             @ +Inf
 lsrs r0,#23
 b sq_5
sq_2:
 lsrs r0,#31
 lsls r0,#31
 b sq_6

@ round(sqrt(2^22./[72:16:248]))
rsqrtapp:
.byte 0xf1,0xda,0xc9,0xbb, 0xb0,0xa6,0x9e,0x97, 0x91,0x8b,0x86,0x82

mufp_lib_end:
