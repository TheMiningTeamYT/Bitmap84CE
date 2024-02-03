section .text
public _rgb888to565
; Takes a pointer to an RGB 888 triplet and converts it to a BGR 565 triplet
_rgb888to565:
    pop iy ; 20
    ex (sp), hl ; 22
    ld de, (hl) ; 20
    inc hl ; 4
    inc hl ; 4
    ld a, (hl) ; 8
    ex de, hl ; 4
    rra ; 4
    rra ; 4
    rra ; 4
    rra ; 4
    rr h ; 8
    rra ; 4
    rr h ; 8
    rra ; 4
    rr h ; 8
    rr l ; 8
    rra ; 4
    rr h ; 8
    rr l ; 8
    rra ; 4
    rr h ; 8
    rr l ; 8
    jp (iy) ; 10
section .text
public _rgb1555to565
; Takes a pointer to a RGB 1555 value and converts it to a BGR 565 value
_rgb1555to565:
    pop iy ; 20
    ex (sp), hl ; 22
    ld hl, (hl) ; 20
    ld a, $E0 ; 8
    and a, l ; 4
    ld d, h ; 4
    ld e, a ; 4
    add.s hl, de ; 8
    jp (iy) ; 10