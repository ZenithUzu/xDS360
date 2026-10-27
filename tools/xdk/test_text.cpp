// SPDX-License-Identifier: GPL-3.0-or-later
// PC check of xdk/frontend/xds_text.cpp: reading letters from UTF-8 and
// Latin-1 names, the translation table and the taller font picture.
// Build and run: bash tools/xdk/test_text.sh
#include "xds_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const unsigned short xdsExtraCodes[];

static int extraCell(unsigned short code) {
    for (int k = 0;; k++)
        if (xdsExtraCodes[k] == code) return 100 + k;
}

static int glyphAt(const char *text, uint32_t &i) {
    std::string s(text);
    return xdsGlyph(s, i);
}

int main() {
    uint32_t i = 0;
    assert(glyphAt("a", i) == 'a' - 32 && i == 0);

    i = 0; // UTF-8 ñ (as FTP leaves it): one letter, two bytes
    assert(glyphAt("\xC3\xB1o", i) == extraCell(0xF1) && i == 1);
    i = 2;
    assert(glyphAt("\xC3\xB1o", i) == 'o' - 32);

    i = 0; // Latin-1 ñ: one byte
    assert(glyphAt("\xF1", i) == extraCell(0xF1) && i == 0);

    i = 0; // the button icons stay icons
    assert(glyphAt("\x83 ", i) == 0x83 - 32 && i == 0);
    i = 0;
    assert(glyphAt("\x7F", i) == 0x7F - 32);

    i = 0; // a three-byte letter (euro sign) shows as one '?'
    assert(glyphAt("\xE2\x82\xACx", i) == '?' - 32 && i == 2);

    i = 0; // a cut UTF-8 sequence at the end doesn't read past it
    assert(glyphAt("\xC3", i) == '?' - 32 && i == 0);

    xdsLanguage = 1;
    assert(xdsText("Settings") == "Ajustes");
    assert(xdsText("On") == "S\303\255");
    assert(xdsText("Pokemon.nds") == "Pokemon.nds");
    xdsLanguage = 0;
    assert(xdsText("Settings") == "Settings");

    // The font picture grows downward; the original keeps its top-left pixel.
    static uint8_t bmp[0x46 + 512 * 512 * 4];
    bmp[0x12] = 0x00; bmp[0x13] = 0x02; // width 512
    bmp[0x16] = 0x00; bmp[0x17] = 0x02; // height 512
    bmp[0x46 + (511 * 512) * 4 + 3] = 0x5A; // top-left pixel (bottom-up rows)
    uint8_t *font = xdsFontBmp(bmp);
    const int height = font[0x16] | (font[0x17] << 8);
    assert(height == 576);
    assert(font[0x46 + ((height - 1) * 512) * 4 + 3] == 0x5A);

    printf("xds_text: all checks passed\n");
    return 0;
}
