//==============================================================================
// Includes
//==============================================================================

#include <stdlib.h>

#include <string.h>
#include <esp_log.h>

#include "display.h"
#include "upng.h"

#include "../../components/hdw-spiffs/spiffs_manager.h"

//==============================================================================
// Defines
//==============================================================================

#define CLAMP(x,l,u) ((x) < l ? l : ((x) > u ? u : (x)))

//==============================================================================
// Functions
//==============================================================================

/**
 * @brief Fill a rectangular area on a display with a single color
 * 
 * @param disp The display to fill an area on
 * @param x1 The x coordinate to start the fill (top left)
 * @param y1 The y coordinate to start the fill (top left)
 * @param x2 The x coordinate to stop the fill (bottom right)
 * @param y2 The y coordinate to stop the fill (bottom right)
 * @param c  The color to fill
 */
void fillDisplayArea(display_t * disp, int16_t x1, int16_t y1, int16_t x2,
    int16_t y2, rgba_pixel_t c)
{
    // Only draw on the display
    int16_t xMin = CLAMP(x1, 0, disp->w);
    int16_t xMax = CLAMP(x2, 0, disp->w);
    int16_t yMin = CLAMP(y1, 0, disp->h);
    int16_t yMax = CLAMP(y2, 0, disp->h);
    
    // Set each pixel
    for (int y = yMin; y < yMax; y++)
    {
        for (int x = xMin; x < xMax; x++)
        {
            disp->setPx(x, y, c);
        }
    }
}

/**
 * @brief Load a png from ROM to RAM. PNGs placed in the spiffs_image folder
 * before compilation will be automatically flashed to ROM 
 * 
 * @param name The filename of the PNG to load
 * @param png  A handle to load the png to
 * @return true if the png was loaded successfully,
 *         false if the png load failed and should not be used
 */
bool loadPng(char * name, png_t * png)
{
    // Read font from file
    uint8_t * buf = NULL;
    size_t sz;
    if(!spiffsReadFile(name, &buf, &sz))
    {
        ESP_LOGE("PNG", "Failed to read %s", name);
        return false;
    }

    // Decode PNG
    upng_t * upng = upng_new_from_bytes(buf, sz);
    if(UPNG_EOK != upng_decode(upng))
    {
        ESP_LOGE("PNG", "Failed to decode png %s", name);
        return false;
    }

    // Variables for PNG metadata
    png->w  = upng_get_width(upng);
    png->h = upng_get_height(upng);
    unsigned int depth  = upng_get_bpp(upng) / 8;
    upng_format format  = upng_get_format(upng);

    // Validate the format
    if(UPNG_RGB8 != format && UPNG_RGBA8 != format)
    {
        ESP_LOGE("PNG", "Invalid PNG format %s (%d)", name, format);
        return false;
    }

    // Allocate space for pixels
    png->px = (rgba_pixel_t*)malloc(sizeof(rgba_pixel_t) * png->w * png->h);

    if(NULL == png->px)
    {
        ESP_LOGE("PNG", "malloc fail (%s)", name);
        return false;
    }

    // Convert the PNG to 565 color, fill up pixels
    for(int y = 0; y < png->h; y++)
    {
        for(int x = 0; x < png->w; x++)
        {
            (png->px)[(y * png->w) + x].rgb.c.r = (127 + (upng_get_buffer(upng)[depth * ((y * png->w) + x) + 0]) * 31) / 255;
            (png->px)[(y * png->w) + x].rgb.c.g = (127 + (upng_get_buffer(upng)[depth * ((y * png->w) + x) + 1]) * 63) / 255;
            (png->px)[(y * png->w) + x].rgb.c.b = (127 + (upng_get_buffer(upng)[depth * ((y * png->w) + x) + 2]) * 31) / 255;
            if(UPNG_RGBA8 == format)
            {
                (png->px)[(y * png->w) + x].a = upng_get_buffer(upng)[depth * ((y * png->w) + x) + 3];
            }
        }
    }

    // Free the upng data
    upng_free(upng);

    // All done
    return true;
}

/**
 * @brief Free the memory for a loaded PNG
 * 
 * @param png The png to free memory from
 */
void freePng(png_t * png)
{
    free(png->px);
}

/**
 * @brief Draw a png to the display
 * 
 * @param disp The display to draw the png to
 * @param png  The png to draw to the display
 * @param xOff The x offset to draw the png at
 * @param yOff The y offset to draw the png at
 */
void drawPng(display_t * disp, png_t *png, int16_t xOff, int16_t yOff)
{
    if(NULL == png->px)
    {
        return;
    }

    // Only draw in bounds
    int16_t xMin = CLAMP(xOff, 0, disp->w);
    int16_t xMax = CLAMP(xOff + png->w, 0, disp->w);
    int16_t yMin = CLAMP(yOff, 0, disp->h);
    int16_t yMax = CLAMP(yOff + png->h, 0, disp->h);
    
    // Draw each pixel
    for (int y = yMin; y < yMax; y++)
    {
        for (int x = xMin; x < xMax; x++)
        {
            int16_t pngX = x - xOff;
            int16_t pngY = y - yOff;
            // TODO handle transparency properly
            if (png->px[(pngY * png->w) + pngX].a)
            {
                disp->setPx(x, y, png->px[(pngY * png->w) + pngX]);
            }
        }
    }
}

/**
 * @brief Load a font from ROM to RAM. Fonts are bitmapped image files that have
 * a single height, all ASCII characters, and a width for each character.
 * PNGs placed in the assets folder before compilation will be automatically
 * flashed to ROM 
 * 
 * @param name The name of the font to load
 * @param font A handle to load the font to
 * @return true if the font was loaded successfully
 *         false if the font failed to load and should not be used
 */
bool loadFont(const char * name, font_t * font)
{
    // Read font from file
    uint8_t * buf = NULL;
    size_t bufIdx = 0;
    size_t sz;
    if(!spiffsReadFile(name, &buf, &sz))
    {
        ESP_LOGE("FONT", "Failed to read %s", name);
        return false;
    }

    // Read the data into a font struct
    font->h = buf[bufIdx++];

    // Read each char
    for(char ch = ' '; ch <= '~'; ch++)
    {
        // Get an easy refence to this character
        font_ch_t * this = &font->chars[ch - ' '];

        // Read the width
        this->w = buf[bufIdx++];

        // Figure out what size the char is
        int pixels = font->h * this->w;
        int bytes = (pixels / 8) + ((pixels % 8 == 0) ? 0 : 1);

        // Allocate space for this char and copy it over
        this->bitmap = (uint8_t *) malloc(sizeof(uint8_t) * bytes);
        memcpy(this->bitmap, &buf[bufIdx], bytes);
        bufIdx += bytes;
    }

    // Free the SPIFFS data
    free(buf);

    return true;
}

/**
 * @brief Free the memory allocated for a font
 *
 * @param font The font to free memory from
 */
void freeFont(font_t * font)
{
    for(char ch = ' '; ch <= '~'; ch++)
    {
        free(font->chars[ch - ' '].bitmap);
    }
}


/**
 * @brief Draw a single character from a font to a display
 * 
 * @param disp  The display to draw a character to
 * @param color The color of the character to draw
 * @param h     The height of the character to draw
 * @param ch    The character bitmap to draw (includes the width of the char)
 * @param xOff  The x offset to draw the png at
 * @param yOff  The y offset to draw the png at
 */
void drawChar(display_t * disp, rgba_pixel_t color, uint16_t h, font_ch_t * ch, int16_t xOff, int16_t yOff)
{
    uint16_t byteIdx = 0;
    uint8_t bitIdx = 0;
    // Iterate over the character bitmap
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < ch->w; x++)
        {
            // Figure out where to draw
            int drawX = x + xOff;
            int drawY = y + yOff;
            // If there is a pixel
            if (ch->bitmap[byteIdx] & (1 << bitIdx))
            {
                // Draw the pixel
                disp->setPx(drawX, drawY, color);
            }

            // Iterate over the bit data
            bitIdx++;
            if(8 == bitIdx) {
                bitIdx = 0;
                byteIdx++;
            }
        }
    }
}

/**
 * @brief Draw text to a display with the given color and font
 * 
 * @param disp  The display to draw a character to
 * @param font  The font to use for the text
 * @param color The color of the character to draw
 * @param text  The text to draw to the display
 * @param xOff  The x offset to draw the text at
 * @param yOff  The y offset to draw the text at
 */
void drawText(display_t * disp, font_t * font, rgba_pixel_t color, const char * text, int16_t xOff, int16_t yOff)
{
    while(*text != 0)
    {
        // Only draw if the char is on the screen
        if (xOff + font->chars[(*text) - ' '].w >= 0)
        {
            // Draw char
            drawChar(disp, color, font->h, &font->chars[(*text) - ' '], xOff, yOff);
        }

        // Move to the next char
        xOff += (font->chars[(*text) - ' '].w + 1);
        text++;

        // If this char is offscreen, finish drawing
        if(xOff >= disp->w)
        {
            return;
        }
    }
}

/**
 * @brief Convert hue, saturation, and value to 15-bit RGB representation
 *
 * @param h The input hue
 * @param s The input saturation
 * @param v The input value
 * @return rgb_pixel_t The output RGB
 */
rgb_pixel_t hsv2rgb(uint16_t h, float s, float v)
{
    float hh, p, q, t, ff;
    uint16_t i;
    rgb_pixel_t px;

    hh = (h % 360) / 60.0f;
    i = (uint16_t)hh;
    ff = hh - i;
    p = v * (1.0 - s);
    q = v * (1.0 - (s * ff));
    t = v * (1.0 - (s * (1.0 - ff)));

    switch (i)
    {
        case 0:
        {
            px.c.r = v * 0x1F;
            px.c.g = t * 0x3F;
            px.c.b = p * 0x1F;
            break;
        }
        case 1:
        {
            px.c.r = q * 0x1F;
            px.c.g = v * 0x3F;
            px.c.b = p * 0x1F;
            break;
        }
        case 2:
        {
            px.c.r = p * 0x1F;
            px.c.g = v * 0x3F;
            px.c.b = t * 0x1F;
            break;
        }
        case 3:
        {
            px.c.r = p * 0x1F;
            px.c.g = q * 0x3F;
            px.c.b = v * 0x1F;
            break;
        }
        case 4:
        {
            px.c.r = t * 0x1F;
            px.c.g = p * 0x3F;
            px.c.b = v * 0x1F;
            break;
        }
        case 5:
        default:
        {
            px.c.r = v * 0x1F;
            px.c.g = p * 0x3F;
            px.c.b = q * 0x1F;
            break;
        }
    }
    return px;
}
