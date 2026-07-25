#include "jaylib.h"
#include "utils.h"

void Jaylib::drawBand(int16_t x, int16_t y, const uint8_t * sprite, uint8_t cols, uint8_t color) {
    uint16_t s;
    int16_t p;

    for(; cols --;) {
        if(x + cols < 0 || x + cols >= WIDTH) continue;

        p = (y >> 3) * WIDTH + cols + x;
        s = pgm_read_byte(sprite + cols) << (y & 0x7);

        if(color) {
            if(p >= 0 && p < WIDTH * (HEIGHT / 8))
                sBuffer[p] |= s;
            if(p + WIDTH >= 0 && p + WIDTH < WIDTH * (HEIGHT / 8))
                sBuffer[p + WIDTH] |= s >> 8;
        } else {
            if(p >= 0 && p < WIDTH * (HEIGHT / 8))
                sBuffer[p] &= ~s;
            if(p + WIDTH >= 0 && p + WIDTH < WIDTH * (HEIGHT / 8))
                sBuffer[p + WIDTH] &= ~(s >> 8);
        }
    }
}

void Jaylib::smallPrint(int x, int y, const uint8_t * str, uint8_t color) {
    int x0 = x;
    char c;
    for(;c = *str ++;) {
        if(c == '\n') {
            x = x0;
            y += 6;
            continue;
        }
        if(c == ' ') {
            x += 2;
            continue;
        }
        c -= 32;
        drawBand(x, y, PRINTABLE_CHARS + 3 * c, 3, color);
        x += 4;
    }
}

void Jaylib::smallPrintPgm(int x, int y, const __FlashStringHelper * str, uint8_t color) {
    int x0 = x;
    char c;
    uint8_t * ptr = (uint8_t *) str;
    for(;c = pgm_read_byte(ptr ++);) {
        if(c == '\n') {
            x = x0;
            y += 6;
            continue;
        }
        if(c == ' ') {
            x += 2;
            continue;
        }
        c -= 32;
        drawBand(x, y, PRINTABLE_CHARS + 3 * c, 3, color);
        x += 4;
    }
}

void Jaylib::smallPrintWrapped(uint8_t x, uint8_t y, uint8_t w, const uint8_t * str, uint8_t color) {
    uint8_t x_off = 0;
    uint8_t y_off = 0;
    char c;
    for(;c = *str; str++) {
        if(c == ' ') {
            uint8_t* ptr = str + 1;
            while(*ptr && *ptr != ' ' && *ptr != '\n') {
                ptr ++;
            }
            if((ptr - str) * 4 + x_off > w) {
                x_off = 0;
                y_off += 6;
                continue;
            }
            x_off += 2;
        } else if(c == '\n') {
            x_off = 0;
            y_off += 6;
        } else {
            c -= 32;
            drawBand(x + x_off, y + y_off, PRINTABLE_CHARS + 3 * c, 3, color);
            x_off += 4;
        }
    }
}

void Jaylib::largePrint(uint8_t x, uint8_t y, const uint8_t * str, uint8_t kern, uint8_t color) {
    char c;
    for(;c = *str ++;) {
        c -= 32;
        drawBand(x, y, PRINTABLE_CHARS_LARGE + 5 * c, 5, color);
        x += 5 + kern;
    }
}

void Jaylib::drawFastVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color) {
    uint8_t i;
    for(i = h; i --;)
        drawPixel(x, y + i, color);
}

/*
void Jaylib::drawFastVLine(uint8_t x, uint8_t y, uint8_t h, uint8_t color) {
    // Crop the line if it's off the bottom edge.
    if(y + h > HEIGHT)
        h = HEIGHT - y;

    // The screen is arranged as 8-pixel vertical stripes laid horizontally.
    uint8_t *pBuf = sBuffer + (y / (HEIGHT / 8)) * WIDTH + x;

    uint8_t mask;

    color = color ? 0xff : 0;

    // Handle the beginning of the line where the first
    // segment might only partially fill up an 8-pixel block.
    uint8_t shift = y & 0x7;
    if(h <= 8 - shift) {
        mask = ~(0xFF << (h & 0x7)) << shift;
        if(color) *pBuf |= mask;
        else *pBuf &= ~mask;
        return;
    }

    // Line must reach the end of the block;
    mask = 0xFF << shift;
    if(color) *pBuf |= mask;
    else *pBuf &= ~mask;
    h -= 8 - shift;

    // Draw all complete 8-pixel segments.
    for(;h > 8; h-= 8) {
        pBuf += WIDTH;
        *pBuf = color;
    }

    // Handle the case where a partial segment is left over.
    if(h) {
        pBuf += WIDTH;
        mask = 0xff << h;
        if(color) *pBuf |= ~mask;
        else *pBuf &= mask;
    }
}
*/

void Jaylib::drawFastHLine(uint8_t x, uint8_t y, uint8_t w, uint8_t color) {
    uint8_t i;
    for(i = w; i --;)
        drawPixel(x + i, y, color);
}

void Jaylib::drawCursorBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    uint8_t c = (counter / 4) % 4;

    for(int i=0; i<w; i++) {
        drawPixel(x + i, y, (c - i + 100) % 3 == 1);
        drawPixel(x + i, y + h, (c + i) % 3 == 0);
    }
    for(int i=0; i<h; i++) {
        drawPixel(x, y + i, (c + i) % 3 == 1);
        drawPixel(x + w, y + i, (c - i + 100) % 3 == 0);
    }

}

void Jaylib::display() {
    counter ++;
    Arduboy2Base::display();
}

void Jaylib::drawPrompt(uint8_t y, const uint8_t * str, uint8_t color=1) {
    uint8_t w = strlen(str) * 6 - 1;
    uint8_t x = (128 - w) / 2;
    for(int i = x - 4; i < x + w + 4; i++) {
        drawFastVLine(i, y + 1, 15, !color);
    }
    drawFastVLine(x - 5, y, 17, color);
    drawFastVLine(x + w + 4, y, 17, color);
    drawFastHLine(x - 5, y, w + 6, color);
    drawFastHLine(x - 5, y + 16, w + 6, color);
    largePrint(x, y + 5, str, 1, color);
}
