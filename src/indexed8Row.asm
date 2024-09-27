assume adl=1
section .text
public _displayIndexed8Row
; Arguments (C Convention):
; uint8_t* rowBuffer
; unsigned int width
; unsigned int renderWidth
; uint16_t* palette
; uint16_t* screenPointer
_displayIndexed8Row:
    ; Local variables:
    ; unsigned int x
    ; Init IX
    push ix
    ld ix, 0
    add ix, sp

    ; Make room on the stack for local variables
    ld hl, -3
    add hl, sp
    ld sp, hl

    ; Check that width > 0
    ld hl, (ix + width)
    ld bc, 0
    xor a, a
    adc hl, bc

    ; If width 0, return
    jr z, the_end

    ; Check if width equals renderWidth
    ld de, (ix + renderWidth)
    sbc hl, de

    ; Restore width
    add hl, de

    ; If width equals renderWidth, jump to the code for that
    jr z, width_equ_renderWidth

    ; Put varX into DE
    ex de, hl

    ; Push rowBuffer to the stack
    ld hl, (ix + rowBuffer)
    push hl

    ; Load the first pixel into A
    or a, (hl)
    
    ; Put varX back into HL
    ex de, hl

    ; Push xError onto the stack
    push bc

fill_pixels:
    ; Write varX back to local variables
    ld (ix + varX), hl

    ; Register allocation
    ; HL: xError
    ; DE: pixel
    ; BC: width
    ; IY: screenPointer
    ; Get the pixel value
    ld bc, (ix + palette)
    sbc hl, hl
    ld l, a
    add hl, hl
    add hl, bc
    ld de, (hl)
    ; Init the registers
    pop hl
    ld bc, (ix + width)
    ld iy, (ix + screenPointer)
fill_pixel_loop:
    ; Write pixels while xError >= 0
    ld (iy), e
    ld (iy + 1), d
    ; Increment screenPointer
    lea iy, iy + 2
    ; Update xError
    sbc hl, bc
    ; If no carry (xError did not wrap around from being positive to negative),
    ; jump to the beginning of the loop
    jr nc, fill_pixel_loop
move_row_buffer:
    ; Update screenPointer
    ld (ix + screenPointer), iy
    ; Register allocation
    ; HL: xError
    ; DE: x
    ; BC: renderWidth
    ; IY: rowBuffer
    ; Init the registers
    ld de, (ix + varX)
    ld bc, (ix + renderWidth)
    pop iy
move_row_buffer_loop:
    ; While xError < 0, update x, rowBuffer and xError
    inc iy
    dec de
    ; Add renderWidth to xError
    add hl, bc
    ; If no carry (xError did not wrap around from being negative to positive),
    ; jump to the beginning of the loop
    jr nc, move_row_buffer_loop
check_x:
    ; Load the next pixel into A
    ld a, (iy)
    ; Push rowBuffer back onto the stack
    push iy
    ; Update xError
    push hl
    ; Check if x is 0
    or a, a
    sbc hl, hl
    adc hl, de
    ; If it's not 0, jump to the beginning
    jr nz, fill_pixels
    jr the_end
width_equ_renderWidth:
    ; Register allocation:
    ; HL: varX
    ; DE: screenPointer
    ; BC: palette/0
    ; IY: rowBuffer
    ; Set registers
    ld iy, (ix + rowBuffer)
    ld de, (ix + screenPointer)
width_equ_renderWidth_loop:
    ; Save varX to the stack
    push hl

    ; Get the pixel value
    ld a, (iy)
    ; Get the address of entry in the palette for that pixel
    ld bc, (ix + palette)
    sbc hl, hl
    ld l, a
    add hl, hl
    add hl, bc
    ld bc, 2
    ldir

    ; Increment IY
    inc iy

    ; Pop X from the stack
    pop hl

    ; Set BC to 1
    inc bc

    ; Decrement and test X
    sbc hl, bc

    ; If X is not zero, jump to the beginning
    jr nz, width_equ_renderWidth_loop
the_end:
    ld sp, ix
    pop ix
    ret

rowBuffer equ 6
width equ 9
renderWidth equ 12
palette equ 15
screenPointer equ 18
varX equ -3

extern _rgb888to565