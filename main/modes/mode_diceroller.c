#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "cndraw.h"
#include "esp_timer.h"
#include "swadgeMode.h"
#include "swadge_esp32.h"
#include "led_util.h" // leds
#include "meleeMenu.h"
#include "mode_main_menu.h"
#include "nvs_manager.h"
#include "bresenham.h"
#include "display.h"
#include "embeddednf.h"
#include "embeddedout.h"
#include "musical_buzzer.h"
#include "settingsManager.h"
#include "linked_list.h"
//#include "bootloader_random.h"
#include "esp_random.h"
#include "aabb_utils.h"

#include "swadge_util.h"
#include "text_entry.h"

#include "mode_diceroller.h"


//TODO: Add Doxygen Blocks
//TODO: Add Cosmetic Features of Colored background/foreground objects. Add Smoother animations. Add LED interaction.


void diceEnterMode(display_t* disp);
void diceExitMode(void);
void diceMainLoop(int64_t elapsedUs);
void diceButtonCb(buttonEvt_t* evt);
//void diceTouchCb(touch_event_t* evt);
//void diceAccelerometerCb(accel_t* accel);
//void diceAudioCb(uint16_t* samples, uint32_t sampleCnt);
vector_t* getRegularPolygonVertices(int8_t sides, float rotDeg, int16_t radius);
void drawRegularPolygon(int xCenter, int yCenter, int8_t sides, float rotDeg, int16_t radius, paletteColor_t col, int dashWidth);
void changeActiveSelection();
void changeDiceCountRequest(int change);
void changeDiceSidesRequest(int change);
void doRoll(int count, int sides, int index);
void doStateMachine(int64_t elapsedUs);

void drawSelectionText(int w,int h,char* rollStr, int bfrSize);
void drawSelectionPointer(int w,int h,char* rollStr,int bfrSize);
void drawDiceBackground(int* xGridOffsets,int* yGridOffsets);
void drawDiceText(int* xGridOffsets,int* yGridOffsets);
void drawDiceBackgroundAnimation(int* xGridOffsets, int* yGridOffsets, int32_t rollAnimationTimUs, double rotationOffsetDeg);
void drawFakeDiceText(int* xGridOffsets, int* yGridOffsets);
void genFakeVal(int32_t rollAnimationTimeUs, double rotationOffsetDeg);

void oddEvenFillFix(display_t* disp, int x0, int y0, int x1, int y1,
                 paletteColor_t boundaryColor, paletteColor_t fillColor);

const int MAXDICE = 6;
const int COUNTCOUNT = 7;
const int8_t validSides[] = {4, 6, 8, 10, 12, 20, 100};
const int8_t polygonSides[] = {3, 4, 3, 4, 5, 3, 6};

const int32_t rollAnimationPeriod = 1000000; //1 Second Spin
//const int32_t fakeValRerollPeriod = 200000; //200 ms
//const uint8_t ticksPerRollAnimation = 10;
const int32_t fakeValRerollPeriod = 90919;//(rollAnimationPeriod / (ticksPerRollAnimation + 1)) + 10;
const float spinScaler = 1;

const char DR_NAMESTRING[] = "Dice Roller";

enum dr_stateVals 
{
    DR_STARTUP = 0,
    DR_SHOWROLL = 1,
    DR_ROLLING = 2
};

//int polygonOuterSides[] = []
//Consider adding outer geometry of dice to make them more recognizable

swadgeMode modeDiceRoller =
{
    .modeName = DR_NAMESTRING,
    .fnEnterMode = diceEnterMode,
    .fnExitMode = diceExitMode,
    .fnMainLoop = diceMainLoop,
    .fnButtonCallback = diceButtonCb,
    .fnTouchCallback = NULL,
    .wifiMode = NO_WIFI,
    .fnEspNowRecvCb = NULL,
    .fnEspNowSendCb = NULL,
    .fnAccelerometerCallback = NULL,
    .fnAudioCallback = NULL,
    .fnTemperatureCallback = NULL,
    .overrideUsb = false
};

typedef struct
{
    display_t* disp;
    font_t ibm_vga8;

    double timeAngle;

    int stateAdvanceFlag;
    int state;

    int requestCount;
    int requestSides;
    int sideIndex;

    //esp_timer_handle_t rollTimer;
    int64_t rollStartTimeUs;
    int fakeVal;
    int fakeValIndex;

    int activeSelection;

    int rollIndex;
    int rollSides;
    int rollSize;
    int* rolls;
    int rollTotal;

    uint32_t timeUs;
    uint32_t lastCallTimeUs;
    uint32_t rollerNum;
} diceRoller_t;

diceRoller_t* diceRoller;

void diceEnterMode(display_t* disp)
{
    diceRoller = calloc(1, sizeof(diceRoller_t));
    
    diceRoller->disp = disp;

    loadFont("ibm_vga8.font", &diceRoller->ibm_vga8);

    diceRoller->rolls = NULL;

    diceRoller->timeAngle = 0;
    diceRoller->rollSize = 0;
    diceRoller->rollSides = 0;
    diceRoller->rollTotal = 0;

    //bootloader_random_enable();
    diceRoller->rollerNum = 0;

    diceRoller->requestCount = 1;
    diceRoller->sideIndex = 5;
    diceRoller->requestSides = validSides[diceRoller->sideIndex];
    
    
    diceRoller->activeSelection = 0;

    diceRoller->state = DR_STARTUP;
    diceRoller->stateAdvanceFlag = 0;
}

void diceExitMode(void)
{
    freeFont(&diceRoller->ibm_vga8);
    if(diceRoller->rolls != NULL)
    {
        free(diceRoller->rolls);
    }
    free(diceRoller);
    //bootloader_random_disable();
}

void diceMainLoop(int64_t elapsedUs)
{
    doStateMachine(elapsedUs);
}




void diceButtonCb(buttonEvt_t* evt)
{
    switch(evt->button)
    {
        case BTN_A:
        {
            if(evt->down)
            {
                //diceRoller->rollerNum = (esp_random() % 20) + 1;
                if(diceRoller->requestCount > 0 && diceRoller->requestSides > 0)
                {
                    doRoll(diceRoller->requestCount,diceRoller->requestSides, diceRoller->sideIndex);
                    diceRoller->rollStartTimeUs = esp_timer_get_time();
                    diceRoller->fakeValIndex = -1;
                    diceRoller->state = DR_ROLLING;
                }
                
            }
            break;
        }
        case BTN_B:
        {
            if(evt->down)
            {
                //diceRoller->rollerNum = (esp_random() % 20) + 1;
                if(diceRoller->requestCount > 0 && diceRoller->requestSides > 0)
                {
                    doRoll(diceRoller->requestCount,diceRoller->requestSides, diceRoller->sideIndex);
                    diceRoller->rollStartTimeUs = esp_timer_get_time();
                    diceRoller->fakeValIndex = -1;
                    diceRoller->state = DR_ROLLING;
                }
                
            }
            break;
        }
        case UP:
        {
            if(evt->down)
            {
                if(!(diceRoller->activeSelection))
                {
                    changeDiceCountRequest(1);
                }
                else
                {
                    changeDiceSidesRequest(1);
                }
            }
            break;
        }
        case DOWN:
        {
            if(evt->down)
            {
                if(!(diceRoller->activeSelection))
                {
                    changeDiceCountRequest(-1);
                }
                else
                {
                    changeDiceSidesRequest(-1);
                }
            }
            break;
        }
        case LEFT:
        {
            if(evt->down)
            {
                changeActiveSelection();
            }
            break;
        }
        case RIGHT:
        {
            if(evt->down)
            {
                changeActiveSelection();
            }
            break;
        }
        default:
        {
            break;
        }
    }
}
//{

//}




void doStateMachine(int64_t elapsedUs)
{
    switch(diceRoller->state)
    {
        case DR_STARTUP:
        {
                diceRoller->disp->clearPx();

                drawText(
                    diceRoller->disp,
                    &diceRoller->ibm_vga8, c555,
                    DR_NAMESTRING,
                    diceRoller->disp->w/2 - textWidth(&diceRoller->ibm_vga8,DR_NAMESTRING)/2,
                    diceRoller->disp->h/2
                );
                

                int w = diceRoller->disp->w;
                int h = diceRoller->disp->h;
                char rollStr[32];
                drawSelectionText(w,h,rollStr,32);
                drawSelectionPointer(w,h,rollStr,32);
                
                
                if(diceRoller->stateAdvanceFlag)
                {
                    diceRoller->state = DR_ROLLING;
                    diceRoller->stateAdvanceFlag = 0;
                }
                break;
                
                
        }
        case DR_SHOWROLL:
        {
            diceRoller->disp->clearPx();

            int w = diceRoller->disp->w;
            int h = diceRoller->disp->h;
            char rollStr[32];
            drawSelectionText(w,h,rollStr,32);
            drawSelectionPointer(w,h,rollStr,32);
            int xGridMargin = w/4;
            int yGridMargin = h/7;
            int xGridOffsets[] = {w/2-xGridMargin, w/2, w/2+xGridMargin, w/2-xGridMargin, w/2, w/2+xGridMargin};
            int yGridOffsets[] = {h/2-yGridMargin, h/2-yGridMargin, h/2-yGridMargin, h/2+yGridMargin, h/2+yGridMargin, h/2+yGridMargin};
            drawDiceBackground(xGridOffsets, yGridOffsets);
            drawDiceText(xGridOffsets, yGridOffsets);
            char totalStr[32];
            snprintf(totalStr,sizeof(totalStr),"Total: %d",diceRoller->rollTotal);

            drawText(
                diceRoller->disp,
                &diceRoller->ibm_vga8, c555,
                totalStr,
                diceRoller->disp->w/2 - textWidth(&diceRoller->ibm_vga8,totalStr)/2,
                diceRoller->disp->h*7/8
            );
            if(diceRoller->stateAdvanceFlag)
            {
                diceRoller->state = DR_ROLLING;
                diceRoller->stateAdvanceFlag = 0;
            }
            break;

                
                
        }
        case DR_ROLLING:
        {
            diceRoller->disp->clearPx();
            int w = diceRoller->disp->w;
            int h = diceRoller->disp->h;
            char rollStr[32];
            drawSelectionText(w,h,rollStr,32);

            int xGridMargin = w/4;
            int yGridMargin = h/7;
            int xGridOffsets[] = {w/2-xGridMargin, w/2, w/2+xGridMargin, w/2-xGridMargin, w/2, w/2+xGridMargin};
            int yGridOffsets[] = {h/2-yGridMargin, h/2-yGridMargin, h/2-yGridMargin, h/2+yGridMargin, h/2+yGridMargin, h/2+yGridMargin};

            //int total = 0;
            int32_t rollAnimationTimeUs = esp_timer_get_time() - diceRoller->rollStartTimeUs;
            double rotationOffsetDeg = rollAnimationTimeUs / (double)rollAnimationPeriod * 360.0 * spinScaler;
            genFakeVal(rollAnimationTimeUs,rotationOffsetDeg);
            drawDiceBackgroundAnimation(xGridOffsets, yGridOffsets,rollAnimationTimeUs,rotationOffsetDeg);
            drawFakeDiceText(xGridOffsets, yGridOffsets);
            
            if(rollAnimationTimeUs > rollAnimationPeriod)
            {
                diceRoller->state = DR_SHOWROLL;
            }
            break;
           

        }
        default:
        {
            break;
        }
    }
}


void changeActiveSelection()
{
    diceRoller->activeSelection = !(diceRoller->activeSelection);
}

//Never less than 1 except at mode start, never greater than MAXDICE
void changeDiceCountRequest(int change)
{
    diceRoller->requestCount = (diceRoller->requestCount - 1 + change + MAXDICE) % MAXDICE + 1;
}

void changeDiceSidesRequest(int change)
{
    diceRoller->sideIndex = (diceRoller->sideIndex + COUNTCOUNT + change) % COUNTCOUNT;
    diceRoller->requestSides = validSides[diceRoller->sideIndex];
}

void doRoll(int count, int sides, int ind)
{
    free(diceRoller->rolls);
    diceRoller->rolls = (int*)malloc(sizeof(int)*count);
    int total = 0;
    for(int m=0; m<count; m++)
    {
        int curVal = (esp_random() % sides) + 1;
        diceRoller->rolls[m] = curVal;
        total += curVal;
    }
    diceRoller->rollSize = count;
    diceRoller->rollSides = sides;
    diceRoller->rollIndex = ind;
    diceRoller->rollTotal = total;
}

double cosDeg(double degrees)
{
    return cos(degrees/360.0*2*M_PI);
}

double sinDeg(double degrees)
{
    return sin(degrees/360.0*2*M_PI);
}

//Coords of vertices as offsets so we can reuse this calculation.
//MUST DEALLOCATE AFTER EACH ROLL
vector_t* getRegularPolygonVertices(int8_t sides, float rotDeg, int16_t radius)
{
     
    vector_t* vertices = (vector_t*) malloc(sizeof(vector_t)*sides);
    double increment = 360.0/sides;
    for(int k = 0; k < sides; k++)
    {
        //use display.c math functions
        vertices[k].x = round(radius*cosDeg(increment*k + rotDeg));
        vertices[k].y = round(radius*sinDeg(increment*k + rotDeg));
    }
    return vertices;
}

void drawRegularPolygon(int xCenter, int yCenter, int8_t sides, float rotDeg, int16_t radius, paletteColor_t col, int dashWidth)
{
    vector_t* vertices = getRegularPolygonVertices(sides, rotDeg, radius);
    
    for(int vertInd = 0; vertInd < sides; vertInd++)
    {
        int8_t endInd = (vertInd+1)%sides;
        
        plotLine(
            diceRoller->disp,
            xCenter + vertices[vertInd].x, yCenter + vertices[vertInd].y,
            xCenter + vertices[endInd].x, yCenter + vertices[endInd].y,
            col, dashWidth
            );
        
    }
    
    free(vertices);
}

void oddEvenFillFix(display_t* disp, int x0, int y0, int x1, int y1,
                 paletteColor_t boundaryColor, paletteColor_t fillColor)
{
    SETUP_FOR_TURBO( disp );

    // Adjust the bounding box if it's out of bounds
    if(x0 < 0)
    {
        x0 = 0;
    }
    if(x1 > disp->w)
    {
        x1 = disp->w;
    }
    if(y0 < 0)
    {
        y0 = 0;
    }
    if(y1 > disp->h)
    {
        y1 = disp->h;
    }
    for(int y = y0; y < y1; y++)
    {
        // Assume starting outside the shape for each row
        bool isInside = false;
        bool insideHysteresis = false;
        uint16_t transitionCount = 0;
        for(int x = x0; x < x1; x++)
        {
            if(boundaryColor == GET_PIXEL(disp, x, y))
            {
                // Flip this boolean, don't color the boundary
                if(!insideHysteresis)
                {
                    isInside = !isInside;
                    insideHysteresis = true;
                    transitionCount++;
                }
            }
            else if(isInside)
            {
                // If we're in-bounds, color the pixel
                
                insideHysteresis = false;
            }
            else
            {
                insideHysteresis = false;
            }
        }
        //printf("Transition Count %d: %d",y,transitionCount);
        if(!(transitionCount%2))
        {
            isInside = false;
            insideHysteresis = false;
            for(int x = x0; x < x1; x++)
            {
                // If a boundary is hit
                if(boundaryColor == GET_PIXEL(disp, x, y))
                {
                    // Flip this boolean, don't color the boundary
                    if(!insideHysteresis)
                    {
                        isInside = !isInside;
                        insideHysteresis = true;
                    }
                }
                else if(isInside)
                {
                    // If we're in-bounds, color the pixel
                    TURBO_SET_PIXEL_BOUNDS(disp, x, y, fillColor);
                    insideHysteresis = false;
                }
                else
                {
                    insideHysteresis = false;
                }
            }
        }
    }
}


void drawSelectionText(int w,int h,char* rollStr, int bfrSize)
{
    snprintf(rollStr,bfrSize,"Next roll is %dd%d",diceRoller->requestCount,diceRoller->requestSides);

    drawText(
        diceRoller->disp,
        &diceRoller->ibm_vga8, c555,
        rollStr,
        diceRoller->disp->w/2 - textWidth(&diceRoller->ibm_vga8,rollStr)/2,
        diceRoller->disp->h/8
    );
}

void drawSelectionPointer(int w,int h,char* rollStr,int bfrSize)
{
    
    
    int xPointerOffset = 40;
    int xPointerSelectionOffset = 16; 
    int yPointerOffset = 17;
    //printf("rollStrSize: %d\n",(int)sizeof(rollStr));
    snprintf(rollStr,bfrSize,"Next roll is %dd%d",diceRoller->requestCount,diceRoller->requestSides);
    int centerToEndPix = textWidth(&diceRoller->ibm_vga8,rollStr)/2;
    snprintf(rollStr,bfrSize,"%dd%d",diceRoller->requestCount,diceRoller->requestSides);
    int endToNumStartPix = textWidth(&diceRoller->ibm_vga8,rollStr);
    snprintf(rollStr,bfrSize,"%d",diceRoller->requestCount);
    int firstNumPix = textWidth(&diceRoller->ibm_vga8,rollStr);
    int dWidth = textWidth(&diceRoller->ibm_vga8,"d");
    snprintf(rollStr,bfrSize,"%d",diceRoller->requestSides);
    int lastNumPix = textWidth(&diceRoller->ibm_vga8,rollStr);

    //printf("%s\n", rollStr);
    

    int countSelX = w/2 + centerToEndPix - endToNumStartPix + firstNumPix/2;
    int sideSelX = w/2 + centerToEndPix - lastNumPix/2;
    drawRegularPolygon(diceRoller->activeSelection ? sideSelX : countSelX,
        h/8+yPointerOffset, 3, -90, 5, c555, 0
    );
}


void drawDiceBackground(int* xGridOffsets,int* yGridOffsets)
{
    for(int m = 0; m < diceRoller->rollSize; m++)
    {
        drawRegularPolygon(xGridOffsets[m],yGridOffsets[m]+5,
        polygonSides[diceRoller->rollIndex],-90,20,c555,0
        );
        int oERadius = 23;
        
        oddEvenFillFix(diceRoller->disp, xGridOffsets[m]-oERadius,
        yGridOffsets[m]-oERadius+5,
        xGridOffsets[m]+oERadius,
        yGridOffsets[m]+oERadius+5,
        c555,c111);
    }
}



void drawDiceText(int* xGridOffsets,int* yGridOffsets)
{
    for(int m = 0; m < diceRoller->rollSize; m++)
    {
        
        char rollOutcome[32];
        snprintf(rollOutcome,sizeof(rollOutcome),"%d",diceRoller->rolls[m]);
    
        drawText(
            diceRoller->disp,
            &diceRoller->ibm_vga8, c555,
            rollOutcome,
            xGridOffsets[m] - textWidth(&diceRoller->ibm_vga8,rollOutcome)/2,
            yGridOffsets[m]
        );

    }
}

void drawDiceBackgroundAnimation(int* xGridOffsets, int* yGridOffsets, int32_t rollAnimationTimUs, double rotationOffsetDeg)
{
    for(int m = 0; m < diceRoller->rollSize; m++)
    {
        

        drawRegularPolygon(xGridOffsets[m],yGridOffsets[m]+5,
            polygonSides[diceRoller->rollIndex],-90 + rotationOffsetDeg,20,c555,0
        );

        int oERadius = 23;

        oddEvenFillFix(diceRoller->disp, xGridOffsets[m]-oERadius,
        yGridOffsets[m]-oERadius+5,
        xGridOffsets[m]+oERadius,
        yGridOffsets[m]+oERadius+5,
        c555,c111);
    }
}

void drawFakeDiceText(int* xGridOffsets, int* yGridOffsets){
                    for(int m = 0; m < diceRoller->rollSize; m++)
                    {
                        //total += diceRoller->rolls[m];
                        char rollOutcome[32];
                        snprintf(rollOutcome,sizeof(rollOutcome),"%d",diceRoller->fakeVal);
                        
                        drawText(
                            diceRoller->disp,
                            &diceRoller->ibm_vga8, c555,
                            rollOutcome,
                            xGridOffsets[m] - textWidth(&diceRoller->ibm_vga8,rollOutcome)/2,
                            yGridOffsets[m]
                        );

                    }
                }

void genFakeVal(int32_t rollAnimationTimeUs, double rotationOffsetDeg)
{
    if(floor(rollAnimationTimeUs / fakeValRerollPeriod) > diceRoller->fakeValIndex)
    {
        diceRoller->fakeValIndex = floor(rollAnimationTimeUs / fakeValRerollPeriod);
        diceRoller->fakeVal = esp_random() % diceRoller->rollSides + 1;
    }
}

void maskDiceTexture();
void drawDie(int sides, int value);
void drawDice(int sides, int* values);

//void diceState(int input)
//{
    
//}