section .text
public _rgb888to565
; Arguments:
; sp[3-5]: Pointer to an RGB 888le triplet
; sp[6-8]: Pointer to a ColorError struct
; Returns:
; hl: BGR565 triplet (for use with the calculator's display)
_rgb888to565:
    ; Init IY
    ld iy, 0 ; 20
    add iy, sp ; 8
    ; Load the RGB 888le triplet into A:DE
    ld hl, (iy + 3) ; 24
    ld de, (hl) ; 20
    inc hl ; 4
    inc hl ; 4
    ld a, (hl) ; 8
    ; Load the pointer to the ColorError struct into HL
    ld hl, (iy + 6) ; 24

    add a, (hl) ; 8
    ld b, a ; 4
    jr nc, red_a ; 8/13
red_b:
    sbc a, a ; 4
    jr red_cont ; 12
red_a:
    and a, 248 ; 8
red_cont:
    ld c, a ; 4
    ld a, b ; 4
    sub a, c ; 4
    ld (hl), a ; 6
    inc hl ; 4

    ld a, d ; 4
    add a, (hl) ; 8
    ld b, a ; 4
    jr nc, green_a ; 8/13
green_b:
    sbc a, a ; 4
    jr green_cont ; 12
green_a:
    and a, 252 ; 8
green_cont:
    ld d, a ; 4
    ld a, b ; 4
    sub a, d ; 4
    ld (hl), a ; 6
    inc hl ; 4

    ld a, e ; 4
    add a, (hl) ; 8
    ld b, a ; 4
    jr nc, blue_a ; 8/13
blue_b:
    sbc a, a ; 4
    jr blue_cont ; 12
blue_a:
    and a, 248 ; 8
blue_cont:
    ld e, a ; 4
    ld a, b ; 4
    sub a, e ; 4
    ld (hl), a ; 6

    ; Load the red value into A
    ld a, c ; 4
    ; Load the green and blue values into HL
    ex de, hl ; 4
    ; Shift around the least significant 3 bits of A (the red value)
    rra ; 4
    rra ; 4
    rra ; 4
    ; Shift the next least significant 2 bits of A into H and shift the 2 least significant bits of H out
    rra ; 4
    rr h ; 8
    rra ; 4
    rr h ; 8
    ; Shift everything else right by 3 to get the final number
    rra ; 4
    rr h ; 8
    rr l ; 8
    rra ; 4
    rr h ; 8
    rr l ; 8
    rra ; 4
    rr h ; 8
    rr l ; 8
    ; Return
    ret ; 18
    
section .text
public _abs_long
; We'll call the number we take as argument "x"
_abs_long:
    ; 97/132 cycles
    ; Put the return address into IY
    pop iy ; 20
    ; Set HL to point to sp + 6 (from where sp was originally) (this is where the MSB of x is)
    ld hl, 3 ; 16
    add hl, sp ; 4
    ; set e to the MSB of x
    ld e, (hl) ; 8
    ; grab the rest of x
    ex (sp), hl ; 22
    ; Test if the sign bit is set
    bit 7, e ; 8
    ; If so, return (work around for the fact that there is no jp cc, (iy) instruction)
    jr z, return ; 8/9
    ; Else, do 0-x
    ; Save the MSB to another register temporarily
    ld c, e ; 4
    ; Clear out A and the carry flag
    xor a, a ; 4
    ; Put the rest of the number into de
    ex de, hl ; 4
    ; Zero out HL
    sbc hl, hl ; 8
    ; 0-x[23:0]
    sbc hl, de ; 8
    ; 0-x[31:24]
    ; Could maybe use neg here?
    sbc a, c ; 4
    ; Put the MSB back into e
    ld e, a ; 4
    return:
    jp (iy) ; 10 (97/132)

public _spiCmd
_spiCmd:
    ld hl, 3
    add hl, sp
    ld a, (hl)
    jr spiCmd
public _spiParam
_spiParam:
    ld hl, 3
    add hl, sp
    ld a, (hl)
; Taken from WikiTI
; https://wikiti.brandonw.net/index.php?title=84PCE:Ports:D000
; Input: A = parameter
spiParam:
 scf ; First bit is set for data
 ; loganius -- omg this is evil instruction hacking -- i could learn from this
 db 030h ; jr nc,? ; skips over one byte
; Input: A = command
spiCmd:
 or a,a ; First bit is clear for commands
 ld hl,0F80818h
 call spiWrite
 ld l,h
 ld (hl),001h
spiWait:
 ld l,00Dh
spiWait1:
 ld a,(hl)
 and a,0F0h
 jr nz,spiWait1
 dec l
spiWait2:
 bit 2,(hl)
 jr nz,spiWait2
 ld l,h
 ld (hl),a
 ret
spiWrite:
 ld b,3
spiWriteLoop:
 rla
 rla
 rla
 ld (hl),a ; send 3 bits
 djnz spiWriteLoop
 ret

public _boot_InitializeHardware
_boot_InitializeHardware = 0000384h