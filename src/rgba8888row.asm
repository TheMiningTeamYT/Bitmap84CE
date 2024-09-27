assume adl=1
section .text
public _displayRGBARow
; Arguments (C Convention):
; uint8_t* rowBuffer
; unsigned int width
; unsigned int renderWidth
; uint16_t* screenPointer
_displayRGBARow:
    ; Local variables:
    ; int xError
    ; unsigned int x
    ; ColorError err
    ; Init IX
    push ix
    ld ix, 0
    add ix, sp

    ; Make room on the stack for local variables
    ld hl, -9
    add hl, sp
    ld sp, hl

    ; Check that width > 0
    ld hl, (ix + width)
    ld bc, -1
    adc hl, bc

    ; If width is 0, return
    jp z, the_end

    ; Init local variables
    inc bc
    ld (ix + xError), bc
    ld (ix + varErr), bc

    ; Push &err to the stack for rgb888to565
    pea ix + varErr
    ; Push rowBuffer to the stack
    ld de, (ix + rowBuffer)
    push de

fill_pixels:
    ; Write varX back to local variables
    ld (ix + varX), hl
    ; Set the alpha values for each pixel
    ; Register allocation
    ; A: alpha
    ; HL: pixel / ((pixel*alpha)/255)
    ; BC: 255
    ; IY: rowBuffer
    ; Put rowBuffer into IY
    ld iy, (ix - 15)

    ; Load the alpha value into A
    ld a, (iy + 3)

    ; If alpha is 255, continue
    cp a, 255
    jr z, alpha_cont

    ; If the alpha is not 0, continue
    or a, a
    jr nz, alpha_not_zero

    ; Set the pixel to 0
    ld bc, 0
    ld (iy), bc
    jr alpha_cont

alpha_not_zero:
    ; Set each pixel value to ((pixel*alpha)/255)
    ld bc, 255
    ld h, a
    ld l, (iy)
    mlt hl
    call __sdivu
    ld (iy), l

    ld h, a
    ld l, (iy + 1)
    mlt hl
    call __sdivu
    ld (iy + 1), l

    ld h, a
    ld l, (iy + 2)
    mlt hl
    call __sdivu
    ld (iy + 2), l
    
alpha_cont:
    ; Register allocation
    ; HL: xError
    ; DE: pixel
    ; BC: width
    ; IY: screenPointer
    ; Get the pixel value
    call _rgb888to565
    ; Init the registers
    ex de, hl
    ld hl, (ix + xError)
    ld bc, (ix + width)
    ld iy, (ix + screenPointer)
    ; Clear the carry flag
    or a, a
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
    lea iy, iy + 4
    dec de
    ; Add renderWidth to xError
    add hl, bc
    ; If no carry (xError did not wrap around from being negative to positive),
    ; jump to the beginning of the loop
    jr nc, move_row_buffer_loop
check_x:
    ; Push rowBuffer back onto the stack
    push iy
    ; Update x and xError
    ld (ix + varX), de
    ld (ix + xError), hl
    ; Check if x is 0
    sbc hl, hl
    adc hl, de
    ; If it's not 0, jump to the beginning
    jr nz, fill_pixels
the_end:
    ld sp, ix
    pop ix
    ret

rowBuffer equ 6
width equ 9
renderWidth equ 12
screenPointer equ 15
xError equ -3
varX equ -6
varErr equ -9

extern _rgb888to565
extern __sdivu