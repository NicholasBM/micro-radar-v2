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

    // solid gradient trail fading behind the leading edge
    for (int i = 1; i <= thickness * spacing; i++) {
        float t = (float)i / (float)(thickness * spacing);
        uint8_t brightness = (uint8_t)((1.0f - t) * trailBrightness);

        buf.drawLine(
            x0, y0,
            x1 - (int)(px * i), y1 - (int)(py * i),
            lgfx::color888(0, brightness, 0)
        );
    }
}