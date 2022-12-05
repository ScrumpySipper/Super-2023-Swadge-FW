//==============================================================================
// Includes
//==============================================================================

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "led_util.h"
#include "swadgeMode.h"

#include "bresenham.h"
#include "meleeMenu.h"

//==============================================================================
// Constant Data
//==============================================================================

/* If defined, display the top arrow when a menu with more rows than fit on the screen is scrolled to the top,
 * and display the bottom arrow when such a menu is scrolled to the bottom.
 * These are used to indicate that the user can wrap-around to the other end of the menu.
 */
#define SHOW_TOP_AND_BOTTOM_ARROWS

// If defined, override other arrow defines, and always display arrows
//#define ALWAYS_SHOW_ARROWS

// Colors for the border when each row is selected
static const paletteColor_t borderColors[NUM_ROW_COLORS_AND_OFFSETS] =
{
    c112, c211, c021, c221, c102, c210,
};

static const led_t borderLedColors[NUM_ROW_COLORS_AND_OFFSETS] =
{
    {.r = 0x10, .g = 0x10, .b = 0x20},
    {.r = 0x20, .g = 0x10, .b = 0x10},
    {.r = 0x00, .g = 0x20, .b = 0x10},
    {.r = 0x20, .g = 0x20, .b = 0x00},
    {.r = 0x10, .g = 0x00, .b = 0x20},
    {.r = 0x20, .g = 0x10, .b = 0x00},
};

#define MIN_ROW_OFFSET 20
#define MAX_ROW_OFFSET 70

// X axis offset for each row
static const uint8_t rowOffsets[NUM_ROW_COLORS_AND_OFFSETS] =
{
    
    MAX_ROW_OFFSET, 45, MIN_ROW_OFFSET, 36, 29, 52,
};

// Boundary color is the same for all entries
static const paletteColor_t boundaryColor = c321;
// Fill color for unselected menu label shapes
static const paletteColor_t unselectedFillColor = c000;

//==============================================================================
// Function Prototypes
//==============================================================================

static void drawMeleeMenuText(display_t* d, font_t* font, const char* text,
                              int16_t xPos, int16_t yPos, bool isSelected);

uint8_t maybeGrowRowsArray(meleeMenu_t* menu, size_t originalCount, size_t additionalCount);

//==============================================================================
// Functions
//==============================================================================

/**
 * Initialize and return a melee menu
 *
 * @param title  The title to be displayed for this menu. The underlying memory
 *               isn't copied, so this string must persist for the lifetime of
 *               the menu.
 * @param font   The font to draw this menu with (should be mm.font)
 * @param cbFunc The function to call when a menu option is selected. The
 *               argument to the callback will be the same pointer
 * @return meleeMenu_t* A pointer to the newly allocated meleeMenu_t. This must
 *                      be deinitialized when the menu is not used anymore.
 */
meleeMenu_t* initMeleeMenu(const char* title, font_t* font, meleeMenuCb cbFunc)
{
    // Allocate a menu and clear the memory
    meleeMenu_t* newMenu = calloc(1, sizeof(meleeMenu_t));
    // Allocate a screen's worth of rows and clear the memory
    newMenu->rows = calloc(MAX_ROWS_ON_SCREEN, sizeof(const char*));
    newMenu->numRowsAllocated = MAX_ROWS_ON_SCREEN;
    // Save the arguments
    newMenu->title = title;
    newMenu->cbFunc = cbFunc;
    newMenu->font = font;
    newMenu->allowLEDControl = true;
    newMenu->usePerRowXOffsets = true;
    // Return the menu
    return newMenu;
}

/**
 * @brief Clear all rows and reset the menu title
 *
 * @param menu The menu to reset
 * @param title The new title to display
 * @param cbFunc The function to call when a menu option is selected. The
 *               argument to the callback will be the same pointer
 */
void resetMeleeMenu(meleeMenu_t* menu, const char* title, meleeMenuCb cbFunc)
{
    menu->title = title;
    menu->numRows = 0;
    menu->firstRowOnScreen = 0;
    menu->selectedRow = 0;
    menu->cbFunc = cbFunc;
    memset(menu->rows, 0,  menu->numRowsAllocated * sizeof(const char*));
}

/**
 * Deinitialize a melee menu
 *
 * @param menu A pointer to the meleeMenu_t to deinitialize.
 */
void deinitMeleeMenu(meleeMenu_t* menu)
{
    free(menu->rows);
    free(menu);
}

/**
 * Add a row to a melee menu. This can only add up to MAX_ROWS rows.
 *
 * @param menu  The menu to add a row to
 * @param label The label for this row. The underlying memory isn't copied, so
 *              this string must persist for the lifetime of the menu.
 * @return value is row number that was inserted, or -1 if error.
 */
int addRowToMeleeMenu(meleeMenu_t* menu, const char* label)
{
    // Make sure there's space for this row
    if(menu->numRows < MAX_ROWS)
    {
        // Try to allocate more rows if we need to
        if(menu->numRowsAllocated < menu->numRows + 1)
        {
            uint8_t rowsAdded = maybeGrowRowsArray(menu, menu->numRowsAllocated, MAX_ROWS_ON_SCREEN);
            if(rowsAdded == 0)
            {
                return -1;
            }
            menu->numRowsAllocated += rowsAdded;
        }

        // Add the row
        menu->rows[menu->numRows] = label;
        return menu->numRows++;
    }
    return -1;
}

/**
 * Process button events for the given menu
 *
 * @param menu The menu to process button events for
 * @param btn The button that was pressed
 */
void meleeMenuButton(meleeMenu_t* menu, buttonBit_t btn)
{
    switch(btn)
    {
        case UP:
        {
            // Scroll up, with wraparound
            if(0 == menu->selectedRow)
            {
                menu->selectedRow = menu->numRows - 1;
            }
            else
            {
                menu->selectedRow--;
            }

            if(menu->selectedRow < menu->firstRowOnScreen)
            {
                if (!menu->animating)
                {
                    menu->animateStartRow = menu->firstRowOnScreen;
                }
                menu->firstRowOnScreen--;
            }
            else if(menu->selectedRow > menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN - 1)
            {
                if (!menu->animating)
                {
                    menu->animateStartRow = menu->firstRowOnScreen;
                }
                menu->firstRowOnScreen = menu->numRows - MAX_ROWS_ON_SCREEN;
            }

            break;
        }
        case SELECT:
        case DOWN:
        {
            // Scroll down, with wraparound
            if(menu->selectedRow == menu->numRows - 1)
            {
                menu->selectedRow = 0;
            }
            else
            {
                menu->selectedRow++;
            }

            if(menu->selectedRow > menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN - 1)
            {
                if (!menu->animating)
                {
                    menu->animateStartRow = menu->firstRowOnScreen;
                }
                menu->firstRowOnScreen++;
            }
            else if(menu->selectedRow < menu->firstRowOnScreen)
            {
                if (!menu->animating)
                {
                    menu->animateStartRow = menu->firstRowOnScreen;
                }
                menu->firstRowOnScreen = 0;
            }

            break;
        }
        case START:
        case BTN_A:
        {
            // Call the callback function for the given row
            menu->cbFunc(menu->rows[menu->selectedRow]);
            break;
        }
        case LEFT:
        case RIGHT:
        case BTN_B:
        default:
        {
            // do nothing
            break;
        }
    }
}

/**
 * @brief Draw a background grid for the menu
 * 
 * @param d 
 */
void drawBackgroundGrid(display_t * d)
{
    // Draw a dim blue background with a grey grid
    for(int16_t y = 0; y < d->h; y++)
    {
        for(int16_t x = 0; x < d->w; x++)
        {
            if(((x % 12) == 0) || ((y % 12) == 0))
            {
                SET_PIXEL(d, x, y, c111); // Grid
            }
            else
            {
                SET_PIXEL(d, x, y, c001); // Background
            }
        }
    }
}

/**
 * Draw a melee menu to a display. This overwrites the entire framebuffer.
 *
 * @param d    The display to draw to
 * @param menu The menu to draw
 */
void drawMeleeMenu(display_t* d, meleeMenu_t* menu)
{
    drawBackgroundGrid(d);

    // The width of the border
#define BORDER_WIDTH 7
    // The gap between display edge and border
#define BORDER_GAP  24
    // The X gap between title and border
#define TITLE_X_GAP BORDER_WIDTH + 1
    // The Y gap between title and border, and between menu item texts and their borders
#define TEXT_Y_GAP  2

    // Draw the title and note where it ends
    int16_t textEnd = drawText(d, menu->font, c222, menu->title, BORDER_GAP + 1 + TITLE_X_GAP, BORDER_GAP + 1);
    textEnd += TITLE_X_GAP;

    paletteColor_t borderColor = borderColors[menu->selectedRow % NUM_ROW_COLORS_AND_OFFSETS];

    // Draw a border, on the right
    fillDisplayArea(d,
                    BORDER_GAP,                BORDER_GAP + menu->font->h + TEXT_Y_GAP + 1,
                    BORDER_GAP + BORDER_WIDTH, d->h - BORDER_GAP,
                    borderColor);
    // Then the left
    fillDisplayArea(d,
                    d->w - BORDER_GAP - BORDER_WIDTH, BORDER_GAP,
                    d->w - BORDER_GAP,                d->h - BORDER_GAP,
                    borderColor);
    // At the bottom
    fillDisplayArea(d,
                    BORDER_GAP,        d->h - BORDER_GAP - BORDER_WIDTH,
                    d->w - BORDER_GAP, d->h - BORDER_GAP,
                    borderColor);
    // Right of title
    fillDisplayArea(d,
                    textEnd,                BORDER_GAP,
                    textEnd + BORDER_WIDTH, BORDER_GAP + menu->font->h + TEXT_Y_GAP + 1,
                    borderColor);
    // Below title
    fillDisplayArea(d,
                    BORDER_GAP,             BORDER_GAP + menu->font->h + TEXT_Y_GAP + 1,
                    textEnd + BORDER_WIDTH, BORDER_GAP + menu->font->h + TEXT_Y_GAP + 1 + BORDER_WIDTH,
                    borderColor);
    // Top right of the title
    fillDisplayArea(d,
                    textEnd,           BORDER_GAP,
                    d->w - BORDER_GAP, BORDER_GAP + BORDER_WIDTH,
                    borderColor);

    // Adjust entries displayed on screen to include the selected row
    if(menu->selectedRow < menu->firstRowOnScreen)
    {
        if (!menu->animating)
        {
            menu->animateStartRow = menu->firstRowOnScreen;
        }
        // Equivalent to shifting the view up until the selected row is on-screen
        menu->firstRowOnScreen = menu->selectedRow;
    }
    else if(menu->selectedRow > menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN - 1)
    {
        if (!menu->animating)
        {
            menu->animateStartRow = menu->firstRowOnScreen;
        }
        // Equivalent to shifting the view down until the selected row is on-screen
        menu->firstRowOnScreen = menu->selectedRow - MAX_ROWS_ON_SCREEN + 1;
    }

#define ARROW_WIDTH  15 // Must be odd
#define ARROW_HEIGHT 9
    int16_t arrowFlatSideX1 = MAX_ROW_OFFSET;
    int16_t arrowFlatSideX2 = arrowFlatSideX1 + ARROW_WIDTH - 1;
    int16_t arrowPointX = (arrowFlatSideX1 + arrowFlatSideX2) / 2;

    int16_t yIdx = BORDER_GAP + 1 + menu->font->h + 2 * TEXT_Y_GAP + 2 * BORDER_WIDTH + 1;
    int16_t rowGap = menu->font->h + 2 * TEXT_Y_GAP + 3;


#define ANIM_ACCEL 1
#define ANIM_MAXSPEED 16

    // Start animating
    if (!menu->animating && menu->animateStartRow != menu->firstRowOnScreen)
    {
        menu->animating = true;
        menu->animateSpeed = 0;
        menu->animateOffset = 0;
    }

    // Draw up arrow
#ifndef ALWAYS_SHOW_ARROWS
#ifdef SHOW_TOP_AND_BOTTOM_ARROWS
    if(menu->numRows > MAX_ROWS_ON_SCREEN && !menu->animating)
#else
    if(menu->firstRowOnScreen > 0 && !menu->animating)
#endif
#endif
    {
        int16_t arrowFlatSideY = yIdx - TEXT_Y_GAP - 3;
        int16_t arrowPointY = arrowFlatSideY - ARROW_HEIGHT + 1; //= round(arrowFlatSideY - (ARROW_WIDTH * sqrt(3.0f)) / 2.0f);
        plotLine(d, arrowFlatSideX1, arrowFlatSideY, arrowFlatSideX2, arrowFlatSideY, boundaryColor, 0);
        plotLine(d, arrowFlatSideX1, arrowFlatSideY - 1, arrowPointX, arrowPointY, boundaryColor, 0);
        plotLine(d, arrowFlatSideX2, arrowFlatSideY - 1, arrowPointX, arrowPointY, boundaryColor, 0);

        // Fill the arrow shape
        oddEvenFill(d,
                    arrowFlatSideX1, arrowPointY,
                    arrowFlatSideX2 + 1, arrowFlatSideY,
                    boundaryColor, unselectedFillColor);
    }

    uint8_t startRow = menu->firstRowOnScreen;
    uint8_t endRow = menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN;
    // Draw the entries
    if (menu->animating)
    {
        // By "going down" I mean increasing Y
        bool goingDown = menu->animateStartRow < menu->firstRowOnScreen;

        if (!goingDown) {
            if (startRow > 0)
            {
                startRow--;
            }
            yIdx -= rowGap;
        }
        else if (goingDown)
        {
            endRow++;
            yIdx += rowGap;
        }

        menu->animateSpeed += ANIM_ACCEL * (goingDown ? 1 : -1);

        if (menu->animateSpeed > ANIM_MAXSPEED)
        {
            menu->animateSpeed = ANIM_MAXSPEED;
        }
        else if (menu->animateSpeed < -ANIM_MAXSPEED)
        {
            menu->animateSpeed = -ANIM_MAXSPEED;
        }

        menu->animateOffset += menu->animateSpeed;
        if ((goingDown && menu->animateOffset > rowGap) || (!goingDown && menu->animateOffset < -rowGap))
        {
            // We have reached our destination (or gone past it)
            menu->animateStartRow = menu->firstRowOnScreen;
            menu->animateOffset = 0;
            menu->animateSpeed = 0;
            menu->animating = false;

            startRow = menu->firstRowOnScreen;
            endRow = menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN;
            yIdx = BORDER_GAP + 1 + menu->font->h + 2 * TEXT_Y_GAP + 2 * BORDER_WIDTH + 1;
        }
    }

    for(uint8_t row = startRow; row < menu->numRows && row < endRow; row++)
    {
        drawMeleeMenuText(d, menu->font, menu->rows[row],
                          menu->usePerRowXOffsets ? rowOffsets[row % NUM_ROW_COLORS_AND_OFFSETS] : MIN_ROW_OFFSET,
                          yIdx - menu->animateOffset,
                          (row == menu->selectedRow));

        yIdx += rowGap;
    }

    // Draw down arrow
#ifndef ALWAYS_SHOW_ARROWS
#ifdef SHOW_TOP_AND_BOTTOM_ARROWS
    if(menu->numRows > MAX_ROWS_ON_SCREEN && !menu->animating)
#else
    if(menu->numRows > menu->firstRowOnScreen + MAX_ROWS_ON_SCREEN && !menu->animating)
#endif
#endif
    {
        int16_t arrowFlatSideY = yIdx - TEXT_Y_GAP - 1;
        int16_t arrowPointY = arrowFlatSideY + ARROW_HEIGHT - 1; //round(arrowFlatSideY + (ARROW_WIDTH * sqrt(3.0f)) / 2.0f);
        plotLine(d, arrowFlatSideX1, arrowFlatSideY, arrowFlatSideX2, arrowFlatSideY, boundaryColor, 0);
        plotLine(d, arrowFlatSideX1, arrowFlatSideY + 1, arrowPointX, arrowPointY, boundaryColor, 0);
        plotLine(d, arrowFlatSideX2, arrowFlatSideY + 1, arrowPointX, arrowPointY, boundaryColor, 0);

        // Fill the arrow shape
        oddEvenFill(d,
                    arrowFlatSideX1, arrowFlatSideY + 1,
                    arrowFlatSideX2 + 1, arrowPointY,
                    boundaryColor, unselectedFillColor);
    }

    if( menu->allowLEDControl )
    {
        led_t leds[NUM_LEDS] = {0};
        for(uint8_t i = 0; i < NUM_LEDS; i++)
        {
            leds[i] = borderLedColors[menu->selectedRow % NUM_ROW_COLORS_AND_OFFSETS];
        }
        setLeds(leds, NUM_LEDS);
    }
}

/**
 * Draw text with a boundary and filled background to a Melee style menu
 *
 * @param d    The display to draw to
 * @param font The font to use
 * @param text The text for this box
 * @param xPos The X position of the text. Note, this is not the position of the
 *             boundary and filled background
 * @param yPos The Y position of the text. Note, this is not the position of the
 *             boundary and filled background
 * @param isSelected true if this is the selected item. This will draw with
 *                   different colors
 */
static void drawMeleeMenuText(display_t* d, font_t* font, const char* text,
                              int16_t xPos, int16_t yPos, bool isSelected)
{
    // Figure out the text width to draw around it
    int16_t tWidth = textWidth(font, text);

    // Top line
    plotLine(d,
             xPos - TEXT_Y_GAP - 1, yPos - TEXT_Y_GAP - 1,
             xPos + tWidth + 1,     yPos - TEXT_Y_GAP - 1,
             boundaryColor, 0);
    // Bottom line
    plotLine(d,
             xPos - 8,          yPos + font->h + TEXT_Y_GAP,
             xPos + tWidth + 1, yPos + font->h + TEXT_Y_GAP,
             boundaryColor, 0);
    // Left side doodad, x -3 to -13, y -3 to 14
    plotLine(d,
             xPos - TEXT_Y_GAP - 1, yPos - TEXT_Y_GAP - 1,
             xPos - 13, yPos + 14,
             boundaryColor, 0);
    // x -13 to -8, y 15 to 23
    plotLine(d,
             xPos - 13, yPos + 15,
             xPos -  8, yPos + font->h + 2,
             boundaryColor, 0);
    // Right side semi-circle
    int16_t radius = (font->h + 6) / 2;
    plotCircleQuadrants(d,
                        xPos + tWidth, yPos - 3 + radius, radius,
                        true, false, false, true,
                        boundaryColor);

    // Text and fill colors are different if selected
    paletteColor_t textColor = c431;
    paletteColor_t fillColor = unselectedFillColor;
    if(isSelected)
    {
        textColor = c000;
        fillColor = c540;
    }

    // Fill the label shape
    oddEvenFill(d,
                xPos - 13, yPos - 2,
                xPos + tWidth + radius + 1, yPos + font->h + 2,
                boundaryColor, fillColor);

    // Draw the text
    drawText(d, font, textColor, text, xPos, yPos);
}

/**
 * Ensures that the rows array has enough space for `additionalCount` additional elements,
 * growing the array if necessary. Retuns the number of elements added if there is sufficient
 * space, or 0 if sufficient space could not be allocated.
*/
uint8_t maybeGrowRowsArray(meleeMenu_t* menu, size_t originalCount, size_t additionalCount)
{
    size_t newCount = originalCount + additionalCount;

    void* newPtr = realloc(menu->rows, sizeof(const char*) * newCount);
    if (newPtr == NULL)
    {
        return 0;
    }

    menu->rows = newPtr;

    return additionalCount;
}
