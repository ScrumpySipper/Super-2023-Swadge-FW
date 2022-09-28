//==============================================================================
// Functions
//==============================================================================

#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

#include "swadge_esp32.h"
#include "swadgeMode.h"
#include "meleeMenu.h"
#include "p2pConnection.h"

#include "mode_main_menu.h"
#include "fighter_menu.h"
#include "mode_fighter.h"
#include "fighter_hr_result.h"
#include "fighter_mp_result.h"
#include "fighter_records.h"

//==============================================================================
// Enums & Structs
//==============================================================================

typedef enum
{
    FIGHTER_MENU,
    FIGHTER_CONNECTING,
    FIGHTER_WAITING,
    FIGHTER_GAME,
    FIGHTER_HR_RESULT,
    FIGHTER_MP_RESULT,
    FIGHTER_RECORDS,
} fighterScreen_t;

typedef enum
{
    CHAR_SEL_MSG,
    STAGE_SEL_MSG,
    BUTTON_INPUT_MSG,
    SCENE_COMPOSED_MSG,
    MP_GAME_OVER_MSG
} fighterMessageType_t;

typedef struct
{
    font_t mmFont;
    meleeMenu_t* menu;
    display_t* disp;
    fighterScreen_t screen;
    p2pInfo p2p;
    fightingCharacter_t characters[2];
    fightingStage_t stage;
    fighterMessageType_t lastSentMsg;
    wsg_t fd_bg;
    int64_t txTimeStart;
} fighterMenu_t;

typedef struct
{
    uint8_t msgType;
    uint32_t roundTimeMs;
    fightingCharacter_t self;
    fightingCharacter_t other;
    int16_t selfDmg;
    int16_t otherDmg;
    int8_t selfKOs;
    int8_t otherKOs;
} fighterMpGameResult_t;

//==============================================================================
// Function Prototypes
//==============================================================================

void fighterEnterMode(display_t* disp);
void fighterExitMode(void);
void fighterMainLoop(int64_t elapsedUs);
void fighterButtonCb(buttonEvt_t* evt);
void fighterBackgroundDrawCb(display_t* disp, int16_t x, int16_t y,
                             int16_t w, int16_t h, int16_t up, int16_t upNum );

void setFighterMainMenu(void);
void fighterMainMenuCb(const char* opt);

void setFighterHrMenu(void);
void fighterHrMenuCb(const char* opt);

void setFighterMultiplayerCharSelMenu(void);
void fighterMultiplayerCharMenuCb(const char* opt);

void setFighterMultiplayerStageSelMenu(void);
void fighterMultiplayerStageMenuCb(const char* opt);

void fighterEspNowRecvCb(const uint8_t* mac_addr, const char* data, uint8_t len, int8_t rssi);
void fighterEspNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status);
void fighterP2pConCbFn(p2pInfo* p2p, connectionEvt_t);
void fighterP2pMsgRxCbFn(p2pInfo* p2p, const uint8_t* payload, uint8_t len);
void fighterP2pMsgTxCbFn(p2pInfo* p2p, messageStatus_t status);
void fighterCheckGameBegin(void);

//==============================================================================
// Variables
//==============================================================================

static const char str_swadgeBros[]  = "Swadge Bros";
const char str_multiplayer[] = "Multiplayer";
const char str_hrContest[]   = "HR Contest";
static const char str_records[]     = "Records";
static const char str_exit[]        = "Exit";

static const char str_charKD[]      = "King Donut";
static const char str_charSN[]      = "Sunny";
static const char str_charBF[]      = "Big Funkus";

// Must match order of fightingCharacter_t
const char* charNames[3] =
{
    str_charKD,
    str_charSN,
    str_charBF
};

static const char str_back[]        = "Back";

static const char str_stgBF[]       = "Battlefield";
static const char str_stgFD[]       = "Final Destination";

swadgeMode modeFighter =
{
    .modeName = str_swadgeBros,
    .fnEnterMode = fighterEnterMode,
    .fnExitMode = fighterExitMode,
    .fnMainLoop = fighterMainLoop,
    .fnButtonCallback = fighterButtonCb,
    .fnTouchCallback = NULL, // fighterTouchCb,
    .wifiMode = ESP_NOW,
    .fnEspNowRecvCb = fighterEspNowRecvCb,
    .fnEspNowSendCb = fighterEspNowSendCb,
    .fnAccelerometerCallback = NULL, // fighterAccelerometerCb,
    .fnAudioCallback = NULL, // fighterAudioCb,
    .fnTemperatureCallback = NULL, // fighterTemperatureCb
    .fnBackgroundDrawCallback = fighterBackgroundDrawCb,
};

fighterMenu_t* fm;

//==============================================================================
// Functions
//==============================================================================

/**
 * @brief Enter the fighter mode by displaying the top level menu
 *
 * @param disp The display to draw to
 */
void fighterEnterMode(display_t* disp)
{
    // Allocate and zero memory
    fm = calloc(1, sizeof(fighterMenu_t));

    // Save the display pointer
    fm->disp = disp;
    setFrameRateUs(FRAME_TIME_MS * 1000); // 20FPS

    // Each menu needs a font, so load that first
    loadFont("mm.font", &(fm->mmFont));

    // Create the menu
    fm->menu = initMeleeMenu(str_swadgeBros, &(fm->mmFont), fighterMainMenuCb);

    // Load a background image to SPI RAM
    loadWsgSpiRam("fdbg.wsg", &fm->fd_bg, true);

    // Set the main menu
    setFighterMainMenu();

    // Start on the menu
    fm->screen = FIGHTER_MENU;

    // Clear state
    fm->characters[0] = NO_CHARACTER;
    fm->characters[1] = NO_CHARACTER;
    fm->stage = NO_STAGE;
}

/**
 * @brief Exit the fighter mode by freeing all resources
 */
void fighterExitMode(void)
{
    fighterExitGame();
    deinitFighterHrResult();
    deinitFighterMpResult();
    deinitFighterRecords();
    deinitMeleeMenu(fm->menu);
    p2pDeinit(&fm->p2p);
    freeFont(&(fm->mmFont));
    freeWsg(&fm->fd_bg);
    free(fm);
}

/**
 * Call the appropriate main loop function for the screen being displayed
 *
 * @param elapsedUs Microseconds since this function was last called
 */
void fighterMainLoop(int64_t elapsedUs)
{
    switch(fm->screen)
    {
        case FIGHTER_MENU:
        {
            drawMeleeMenu(fm->disp, fm->menu);
            break;
        }
        case FIGHTER_GAME:
        {
            // Loop the game
            fighterGameLoop(elapsedUs);
            break;
        }
        case FIGHTER_CONNECTING:
        {
            // TODO spin a wheel or something
            drawBackgroundGrid(fm->disp);
            const char searching_for[] = "Searching For";
            const char another_swadge[] = "Another Swadge";
            int16_t tWidth = textWidth(&fm->mmFont, searching_for);
            drawText(fm->disp, &fm->mmFont, c540, searching_for, (fm->disp->w - tWidth) / 2, (fm->disp->h / 2) - fm->mmFont.h - 4);
            tWidth = textWidth(&fm->mmFont, another_swadge);
            drawText(fm->disp, &fm->mmFont, c540, another_swadge, (fm->disp->w - tWidth) / 2, (fm->disp->h / 2) + 4);
            break;
        }
        case FIGHTER_WAITING:
        {
            // TODO spin a wheel or something
            drawBackgroundGrid(fm->disp);
            const char searching_for[] = "Waiting for";
            const char another_swadge[] = "Other Swadge";
            int16_t tWidth = textWidth(&fm->mmFont, searching_for);
            drawText(fm->disp, &fm->mmFont, c540, searching_for, (fm->disp->w - tWidth) / 2, (fm->disp->h / 2) - fm->mmFont.h - 4);
            tWidth = textWidth(&fm->mmFont, another_swadge);
            drawText(fm->disp, &fm->mmFont, c540, another_swadge, (fm->disp->w - tWidth) / 2, (fm->disp->h / 2) + 4);
            break;
        }
        case FIGHTER_HR_RESULT:
        {
            // Draw result after a Home Run Contest
            fighterHrResultLoop(elapsedUs);
            break;
        }
        case FIGHTER_MP_RESULT:
        {
            // Draw results after a multiplayer match
            fighterMpResultLoop(elapsedUs);
            break;
        }
        case FIGHTER_RECORDS:
        {
            // Draw fighter records
            fighterRecordsLoop(elapsedUs);
            break;
        }
    }
}

/**
 * Call the appropriate button function for the screen being displayed
 *
 * @param evt
 */
void fighterButtonCb(buttonEvt_t* evt)
{
    switch(fm->screen)
    {
        case FIGHTER_MENU:
        {
            // Pass button events from the Swadge mode to the menu
            if(evt->down)
            {
                meleeMenuButton(fm->menu, evt->button);
            }
            break;
        }
        case FIGHTER_GAME:
        {
            // Pass button events from the Swdage mode to the game
            fighterGameButtonCb(evt);
            break;
        }
        case FIGHTER_CONNECTING:
        {
            // START or SELECT exits the HR Result
            if(evt->down && ((START == evt->button) || (SELECT == evt->button)))
            {
                p2pDeinit(&(fm->p2p));
                setFighterMainMenu();
                fm->screen = FIGHTER_MENU;
            }
            break;
        }
        case FIGHTER_WAITING:
        {
            // No cancel when waiting
            break;
        }
        case FIGHTER_HR_RESULT:
        {
            // START or SELECT exits the HR Result
            if(evt->down && ((START == evt->button) || (SELECT == evt->button)))
            {
                deinitFighterHrResult();
                setFighterMainMenu();
                fm->screen = FIGHTER_MENU;
            }
            break;
        }
        case FIGHTER_MP_RESULT:
        {
            // START or SELECT exits the MP Result
            if(evt->down && ((START == evt->button) || (SELECT == evt->button)))
            {
                deinitFighterMpResult();
                setFighterMainMenu();
                fm->screen = FIGHTER_MENU;
            }
            break;
        }
        case FIGHTER_RECORDS:
        {
            // Some buttons return to main menu
            if(evt->down)
            {
                switch(evt->button)
                {
                    case BTN_A:
                    case BTN_B:
                    case START:
                    case SELECT:
                    {
                        deinitFighterRecords();
                        fm->screen = FIGHTER_MENU;
                        break;
                    }
                    case UP:
                    case DOWN:
                    case LEFT:
                    case RIGHT:
                    default:
                    {
                        break;
                    }
                }
            }
            break;
        }
    }
}

/**
 * @brief Draw a portion of the background when requested
 *
 * @param disp The display to draw to
 * @param x The X offset to draw
 * @param y The Y offset to draw
 * @param w The width to draw
 * @param h The height to draw
 * @param up The current number of the update call
 * @param upNum The total number of update calls for this frame
 */
void fighterBackgroundDrawCb(display_t* disp, int16_t x, int16_t y,
                             int16_t w, int16_t h, int16_t up, int16_t upNum )
{
    switch (fm->screen)
    {
        case FIGHTER_MENU:
        case FIGHTER_CONNECTING:
        case FIGHTER_WAITING:
        case FIGHTER_RECORDS:
        case FIGHTER_MP_RESULT:
        {
            // These draw menu background
            break;
        }
        case FIGHTER_GAME:
        {
            // Notify the game that it should draw after the background is drawn
            fighterSetDrawScene();
        }
        /* FALLTHRU */
        case FIGHTER_HR_RESULT:
        {
            // Figure out source and destination pointers
            paletteColor_t* dst = &disp->pxFb[(y * disp->w) + x];
            paletteColor_t* src = &fm->fd_bg.px[(y * disp->w) + x];
            // Copy the image to the framebuffer
            memcpy(dst, src, w * h);
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Sets up the top level menu for Fighter, including callback
 */
void setFighterMainMenu(void)
{
    resetMeleeMenu(fm->menu, str_swadgeBros, fighterMainMenuCb);
    addRowToMeleeMenu(fm->menu, str_multiplayer);
    addRowToMeleeMenu(fm->menu, str_hrContest);
    addRowToMeleeMenu(fm->menu, str_records);
    addRowToMeleeMenu(fm->menu, str_exit);
    fm->screen = FIGHTER_MENU;
}

/**
 * This is called when a menu option is selected from the top level menu
 *
 * @param opt The option that was selected (string pointer)
 */
void fighterMainMenuCb(const char* opt)
{
    // When a row is clicked, print the label for debugging
    if(opt == str_multiplayer)
    {
        // Clear state
        fm->characters[0] = NO_CHARACTER;
        fm->characters[1] = NO_CHARACTER;
        fm->stage = NO_STAGE;
        // Show the screen for connecting
        fm->screen = FIGHTER_CONNECTING;
        // Initialize p2p
        p2pDeinit(&(fm->p2p));
        p2pInitialize(&(fm->p2p), 'F', fighterP2pConCbFn, fighterP2pMsgRxCbFn, -20);
        // Start the connection
        p2pStartConnection(&fm->p2p);
    }
    else if (opt == str_hrContest)
    {
        // Home Run contest selected, display character select menu
        setFighterHrMenu();
    }
    else if (opt == str_records)
    {
        initFighterRecords(fm->disp, &fm->mmFont);
        fm->screen = FIGHTER_RECORDS;
    }
    else if (opt == str_exit)
    {
        // Exit selected
        switchToSwadgeMode(&modeMainMenu);
    }
}

////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Sets up the Home Run Contest menu for Fighter, including callback
 */
void setFighterHrMenu(void)
{
    resetMeleeMenu(fm->menu, str_hrContest, fighterHrMenuCb);
    addRowToMeleeMenu(fm->menu, str_charKD);
    addRowToMeleeMenu(fm->menu, str_charSN);
    addRowToMeleeMenu(fm->menu, str_charBF);
    addRowToMeleeMenu(fm->menu, str_back);
    fm->screen = FIGHTER_MENU;
}

/**
 * This is called when a menu option is selected from the Home Run Contest menu
 *
 * @param opt The option that was selected (string pointer)
 */
void fighterHrMenuCb(const char* opt)
{
    // These are the same for HR COntest
    fm->stage = HR_STADIUM;
    fm->characters[1] = SANDBAG;

    // Check the menu option selected
    if (opt == str_charKD)
    {
        // King Donut Selected
        fm->characters[0] = KING_DONUT;
    }
    else if (opt == str_charSN)
    {
        // Sunny Selected
        fm->characters[0] = SUNNY;
    }
    else if (opt == str_charBF)
    {
        // Big Funkus Selected
        fm->characters[0] = BIG_FUNKUS;
    }
    else if (opt == str_back)
    {
        // Reset to top level melee menu
        setFighterMainMenu();
        return;
    }
    else
    {
        // Shouldn't happen, but return just in case
        return;
    }

    // No return, start the game
    fighterStartGame(fm->disp, &fm->mmFont, HR_CONTEST, fm->characters, fm->stage, true);
    fm->screen = FIGHTER_GAME;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Sets up the multiplayer character select menu for Fighter, including callback
 */
void setFighterMultiplayerCharSelMenu(void)
{
    resetMeleeMenu(fm->menu, str_multiplayer, fighterMultiplayerCharMenuCb);
    addRowToMeleeMenu(fm->menu, str_charKD);
    addRowToMeleeMenu(fm->menu, str_charSN);
    addRowToMeleeMenu(fm->menu, str_charBF);
    fm->screen = FIGHTER_MENU;
}

/**
 * This is called when a menu option is selected from the multiplayer character select menu
 *
 * @param opt The option that was selected (string pointer)
 */
void fighterMultiplayerCharMenuCb(const char* opt)
{
    uint8_t charIdx = (GOING_FIRST == fm->p2p.cnc.playOrder) ? 0 : 1;
    if (opt == str_charKD)
    {
        // King Donut Selected
        fm->characters[charIdx] = KING_DONUT;
    }
    else if (opt == str_charSN)
    {
        // Sunny Selected
        fm->characters[charIdx] = SUNNY;
    }
    else if (opt == str_charBF)
    {
        // Big Funkus Selected
        fm->characters[charIdx] = BIG_FUNKUS;
    }
    else
    {
        // Shouldn't happen, but return just in case
        return;
    }

    // No return means a character was selected
    // Send character to the other swadge
    const uint8_t payload[] =
    {
        CHAR_SEL_MSG,
        fm->characters[charIdx]
    };
    p2pSendMsg(&fm->p2p, payload, sizeof(payload), true, fighterP2pMsgTxCbFn);
    fm->txTimeStart = esp_timer_get_time();
    fm->lastSentMsg = CHAR_SEL_MSG;

    if(GOING_FIRST == fm->p2p.cnc.playOrder)
    {
        // Player going first picks the stage
        setFighterMultiplayerStageSelMenu();
    }
    else
    {
        // Otherwise wait for the stage to be picked
        fm->screen = FIGHTER_WAITING;
    }
}

////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Sets up the multiplayer stage select menu for Fighter, including callback
 */
void setFighterMultiplayerStageSelMenu(void)
{
    resetMeleeMenu(fm->menu, str_multiplayer, fighterMultiplayerStageMenuCb);
    addRowToMeleeMenu(fm->menu, str_stgBF);
    addRowToMeleeMenu(fm->menu, str_stgFD);
    fm->screen = FIGHTER_MENU;
}

/**
 * This is called when a menu option is selected from the multiplayer stage select menu
 *
 * @param opt The option that was selected (string pointer)
 */
void fighterMultiplayerStageMenuCb(const char* opt)
{
    if(str_stgBF == opt)
    {
        fm->stage = BATTLEFIELD;
    }
    else if(str_stgFD == opt)
    {
        fm->stage = FINAL_DESTINATION;
    }
    else
    {
        // Shouldn't happen, but return just in case
        return;
    }

    // No return means a stage was selected
    // Send stage to other swadge
    const uint8_t payload[] =
    {
        STAGE_SEL_MSG,
        fm->stage
    };
    p2pSendMsg(&fm->p2p, payload, sizeof(payload), true, fighterP2pMsgTxCbFn);
    fm->txTimeStart = esp_timer_get_time();
    fm->lastSentMsg = STAGE_SEL_MSG;

    // Wait for the other swadge to pick a character
    fm->screen = FIGHTER_WAITING;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * This function is called whenever an ESP-NOW packet is received.
 *
 * @param mac_addr The MAC address which sent this data
 * @param data     A pointer to the data received
 * @param len      The length of the data received
 * @param rssi     The RSSI for this packet, from 1 (weak) to ~90 (touching)
 */
void fighterEspNowRecvCb(const uint8_t* mac_addr, const char* data, uint8_t len, int8_t rssi)
{
    // Forward to p2p
    p2pRecvCb(&fm->p2p, mac_addr, (const uint8_t*)data, len, rssi);
}

/**
 * This function is called whenever an ESP-NOW packet is sent.
 * It is just a status callback whether or not the packet was actually sent.
 * This will be called after calling espNowSend()
 *
 * @param mac_addr The MAC address which the data was sent to
 * @param status   The status of the transmission
 */
void fighterEspNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status)
{
    // Forward to p2p
    p2pSendCb(&fm->p2p, mac_addr, status);
    if(ESP_NOW_SEND_SUCCESS == status)
    {
        // Let the game know how long to wait before retrying a packet
        setFighterRetryTimeUs(esp_timer_get_time() - fm->txTimeStart);
    }
}

/**
 * @brief This is the p2p connection callback
 *
 * @param p2p The p2p struct for this connection
 * @param evt The connection event that occurred
 */
void fighterP2pConCbFn(p2pInfo* p2p, connectionEvt_t evt)
{
    switch(evt)
    {
        case CON_STARTED:
        case RX_GAME_START_ACK:
        case RX_GAME_START_MSG:
        {
            break;
        }
        case CON_ESTABLISHED:
        {
            // Connection established, show character select screen
            setFighterMultiplayerCharSelMenu();
            break;
        }
        case CON_LOST:
        {
            // Reset to top level melee menu
            fighterExitGame();
            setFighterMainMenu();
            break;
        }
    }
}

/**
 * @brief This is the p2p message receive callback
 *
 * @param p2p The p2p struct for this connection
 * @param payload The payload for the message
 * @param len The length of the message
 */
void fighterP2pMsgRxCbFn(p2pInfo* p2p, const uint8_t* payload, uint8_t len)
{
    switch(fm->screen)
    {
        case FIGHTER_MENU:
        case FIGHTER_CONNECTING:
        case FIGHTER_WAITING:
        {
            // Check what was received
            if(payload[0] == CHAR_SEL_MSG)
            {
                // Receive a character selection, so save it
                uint8_t charIdx = (GOING_FIRST == fm->p2p.cnc.playOrder) ? 1 : 0;
                fm->characters[charIdx] = payload[1];
                fighterCheckGameBegin();
            }
            else if(payload[0] == STAGE_SEL_MSG)
            {
                // Receive a stage selection, so save it
                fm->stage = payload[1];
                fighterCheckGameBegin();
            }
            break;
        }
        case FIGHTER_GAME:
        {
            if(BUTTON_INPUT_MSG == payload[0])
            {
                // Receive button inputs, so save them
                fighterRxButtonInput(payload[1]);
            }
            else if(SCENE_COMPOSED_MSG == payload[0])
            {
                // Receive a scene, so draw it
                fighterRxScene((const fighterScene_t*) payload, len);
            }
            else if (MP_GAME_OVER_MSG == payload[0])
            {
                // Show the result
                const fighterMpGameResult_t* res = (const fighterMpGameResult_t*)payload;
                initFighterMpResult(fm->disp, &fm->mmFont, res->roundTimeMs,
                                    res->other, res->otherKOs, res->otherDmg,
                                    res->self, res->selfKOs, res->selfDmg);
                fm->screen = FIGHTER_MP_RESULT;

                // Deinit the game
                fighterExitGame();
            }
            break;
        }
        case FIGHTER_HR_RESULT:
        case FIGHTER_MP_RESULT:
        case FIGHTER_RECORDS:
        {
            // These screens don't receive packets
            break;
        }
    }
}

/**
 * @brief This is the p2p message sent callback
 *
 * @param p2p The p2p struct for this connection
 * @param status The status of the transmission
 */
void fighterP2pMsgTxCbFn(p2pInfo* p2p, messageStatus_t status)
{
    // Check what was ACKed or failed
    switch(status)
    {
        case MSG_ACKED:
        {
            // After character or stage selection, check if the game can begin
            if (fm->lastSentMsg == CHAR_SEL_MSG || fm->lastSentMsg == STAGE_SEL_MSG)
            {
                fighterCheckGameBegin();
            }
            break;
        }
        case MSG_FAILED:
        {
            setFighterMainMenu();
            break;
        }
    }
}

/**
 * Check if the multiplayer game can begin. It can begin when both characters
 * and a stage are selected
 */
void fighterCheckGameBegin(void)
{
    // Check if the game should be started
    if(fm->screen != FIGHTER_GAME &&
            fm->characters[0] != NO_CHARACTER &&
            fm->characters[1] != NO_CHARACTER &&
            fm->stage != NO_STAGE)
    {
        // Characters and stage set, start the game!
        fighterStartGame(fm->disp, &fm->mmFont, MULTIPLAYER, fm->characters,
                         fm->stage, GOING_FIRST == fm->p2p.cnc.playOrder);
        fm->screen = FIGHTER_GAME;
    }
}

/**
 * @brief Send a packet to the other swadge with this's player's button input
 *
 * @param btnState
 */
void fighterSendButtonsToOther(int32_t btnState)
{
    const uint8_t payload[] =
    {
        BUTTON_INPUT_MSG,
        btnState // This clips 32 bits to 8 bits, but there are 8 buttons anyway
    };
    // Don't ack, retry until the scene is received
    p2pSendMsg(&fm->p2p, payload, sizeof(payload), false, fighterP2pMsgTxCbFn);
    fm->txTimeStart = esp_timer_get_time();
    fm->lastSentMsg = BUTTON_INPUT_MSG;
}

/**
 * @brief Send a packet to the other swadge with the scene to draw
 *
 * @param scene
 * @param len
 */
void fighterSendSceneToOther(fighterScene_t* scene, uint8_t len)
{
    // Insert the message type (this byte should be empty)
    ((uint8_t*)scene)[0] = SCENE_COMPOSED_MSG;
    // Don't ack, retry until buttons are received
    p2pSendMsg(&fm->p2p, (const uint8_t*)scene, len, false, fighterP2pMsgTxCbFn);
    fm->txTimeStart = esp_timer_get_time();
    fm->lastSentMsg = SCENE_COMPOSED_MSG;
}

/**
 * @brief Initialize and start showing the result after a Home Run contest
 *
 * @param character The character who played the game
 * @param position The final position of the sandbag
 * @param velocity The final velocity of the sandbag
 * @param gravity The gravity affecting the sandbag
 * @param platformEndX The end of the platform
 */
void fighterShowHrResult(fightingCharacter_t character, vector_t position,
                         vector_t velocity, int32_t gravity, int32_t platformEndX)
{
    initFighterHrResult(fm->disp, &fm->mmFont, character, position, velocity, gravity, platformEndX);
    fm->screen = FIGHTER_HR_RESULT;
}

/**
 * @brief Initialize and start showing the result after a multiplayer match contest
 *
 * @param roundTimeMs The time the round took, in milliseconds
 * @param self This swadge's character
 * @param selfKOs This swadge's number of KOs
 * @param selfDmg The amount of damage this swadge did
 * @param other The other swadge's character
 * @param otherKOs The other swadge's number of KOs
 * @param otherDmg The amount of damage the other swadge did
 */
void fighterShowMpResult(uint32_t roundTimeMs,
                         fightingCharacter_t self,  int8_t selfKOs, int16_t selfDmg,
                         fightingCharacter_t other, int8_t otherKOs, int16_t otherDmg)
{
    // Send result to other swadge with an acknowledged packet
    const fighterMpGameResult_t res =
    {
        .msgType = MP_GAME_OVER_MSG,
        .roundTimeMs = roundTimeMs,
        .self = self,
        .selfKOs = selfKOs,
        .selfDmg = selfDmg,
        .other = other,
        .otherKOs = otherKOs,
        .otherDmg = otherDmg,
    };
    p2pSendMsg(&fm->p2p, (const uint8_t*)&res, sizeof(fighterMpGameResult_t), true, fighterP2pMsgTxCbFn);

    initFighterMpResult(fm->disp, &fm->mmFont, roundTimeMs,
                        self, selfKOs, selfDmg,
                        other, otherKOs, otherDmg);
    fm->screen = FIGHTER_MP_RESULT;
}
