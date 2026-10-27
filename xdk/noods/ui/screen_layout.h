/*
    Copyright 2019-2026 Hydr8gon

    This file is part of NooDS.

    NooDS is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

class ScreenLayout {
public:
    ScreenLayout(): winWidth(0), winHeight(0), minWidth(0), minHeight(0), topX(0), botX(0), topY(0), botY(0), topWidth(0), botWidth(0), topHeight(0), botHeight(0) {}
private:

public:
    static int screenPosition;
    static int screenRotation;
    static int screenArrangement;
    static int screenSizing;
    static int screenGap;
    static int aspectRatio;
    static int integerScale;
    static int gbaCrop;

    int winWidth, winHeight;
    int minWidth, minHeight;
    int topX, botX;
    int topY, botY;
    int topWidth, botWidth;
    int topHeight, botHeight;

    static void addSettings();
    void update(int winWidth, int winHeight, bool gbaMode, bool splitScreens = false);

    int getTouchX(int x, int y);
    int getTouchY(int x, int y);
};
