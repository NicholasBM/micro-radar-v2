#pragma once

#include "LGFX.h"

void DrawScanLines(LGFX_Sprite& buf, const int x0, const int y0, const int x1, const int y1, const int thickness, const int trailBrightness, const int spacing)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrt(dx * dx + dy * dy);

    // perpendicular unit vector
    float px = -dy / len;
    float py = dx / len;

    // bright leading edge
    buf.drawLine(x0, y0, x1, y1, lgfx::color888(0, 200, 0));

    // solid gradient trail using RGB332-friendly levels
    const int totalWidth = thickness * spacing;
    const int numBands = 6;
    const uint8_t levels[] = { 146, 109, 73, 55, 36, 18 };
    int bandWidth = totalWidth / numBands;

    int offset = 1;
    for (int band = 0; band < numBands; band++) {
        uint32_t col = lgfx::color888(0, levels[band], 0);
        for (int i = 0; i < bandWidth; i++) {
            buf.drawLine(
                x0, y0,
                x1 - (int)(px * offset), y1 - (int)(py * offset),
                col
            );
            offset++;
        }
    }
}