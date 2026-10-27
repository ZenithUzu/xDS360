// SPDX-License-Identifier: GPL-3.0-or-later
// Menu language and the letters NooDS's font lacks (n with tilde, accents, inverted ? and !).
#pragma once

#include <stdint.h>
#include <string>

// 0 English, 1 Spanish (saved in noods.ini as "language").
extern int xdsLanguage;

// The menu text in the chosen language; text without a translation comes back as is.
std::string xdsText(const std::string &english);

// The font cell of the character at text[i], moving i to its last byte. File
// names come as UTF-8 (FTP) or Latin-1; unknown characters show as '?'.
int xdsGlyph(const std::string &text, uint32_t &i);
int xdsGlyphWidth(int cell, const uint8_t *asciiWidths);

// NooDS's font.bmp with two more rows for the extra letters (built once).
uint8_t *xdsFontBmp(uint8_t *bmp);
