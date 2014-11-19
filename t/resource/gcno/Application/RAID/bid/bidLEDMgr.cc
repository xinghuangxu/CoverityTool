/*******************************************************************************

NAME            bidLEDMgr.cc
VERSION         %version: WIC~6 %
UPDATE DATE     %date_modified: Thu Sep 25 09:40:30 2014 %
PROGRAMMER      %created_by:    douglasp %

        Copyright 2008-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION: implementation of LEDManager class

*******************************************************************************/

#include "vkiWrap.h"

#include "bidBatteryStatus.h"
#include "bidLEDMgr.h"
#include "bidLib.h"
#include "bidTrace.h"

#include "bcmLED.h"

// Debug queue
static UINT16 bidLEDFstrBase;

#define BID_LED_SET_SAR                               (bidLEDFstrBase + 0 )
#define BID_LED_SET_SAA                               (bidLEDFstrBase + 1 )
#define BID_LED_SET_CHRG                              (bidLEDFstrBase + 2 )

/******************************************************************************
 * DESCRIPTION:  Set up all DQ format strings for this LEDMgr class
 *
 */
void
bidLEDDqTrace
    (
    )
{
    bidLEDFstrBase = dqvkiNextFsn(bidDqWriter);

    dqvkiAddFstr(bidDqWriter, BID_LED_SET_SAR,  FDL_NORMAL, "led",
        "%h led       SAR LED#%d state is set from %d to %d"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LED_SET_SAA,  FDL_NORMAL, "led",
        "%h led       SAA LED#%d state is set from %d to %d"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LED_SET_CHRG, FDL_NORMAL, "led",
        "%h led       Charging LED#%d state is set from %d to %d"
                      );
}

/*******************************************************************************
 * DESCRIPTION:  Constructor
 *
 */
bid::LEDManager::LEDManager
    (
    )
{
}

/*******************************************************************************
 * DESCRIPTION:  Destructor
 *
 */
bid::LEDManager::~LEDManager
    (
    )
{
}

/*******************************************************************************
 * DESCRIPTION:  Initialize the blink pattern and set all BBU LEDs to OFF
 *
 * THROWS:      std::bad_alloc()
 */
void
bid::LEDManager::initialize
    (
    )
{
    initializeBlinkPattern();

    try
    {
        BYTE ledCount = getSAACount();
        //initialize SAA Battery LED members
        m_SAA.reserve(ledCount);    // throws
        for (UINT32 idx=0; idx < ledCount; idx++)
        {
            m_SAA.push_back(BID_LED_OFF);
            //BCM to set LED to OFF
            setServiceState(idx, BID_LED_OFF);
        }

        //initialize SAR Battery LED members
        ledCount = getSARCount();
        m_SAR.reserve(ledCount);  // throws
        for (UINT32 idx=0; idx < ledCount; idx++)
        {
            m_SAR.push_back(BID_LED_OFF);
            //BCM to set LED to OFF
            setFaultState(idx, BID_LED_OFF);
        }

        //initialize Charging Battery LED members
        ledCount = getChargingCount();
        m_Charging.reserve(ledCount); // throws
        for (UINT32 idx=0; idx < ledCount; idx++)
        {
            m_Charging.push_back(BID_LED_OFF);
            //BCM to set LED to OFF
            setChargingState(idx, BID_LED_OFF);
        }
    }
    catch ( std::exception& e )
    {
        throw std::bad_alloc();
    }
}

/*******************************************************************************
 * DESCRIPTION:
 * Initialize all BBU blink pattern
 */
void
bid::LEDManager::initializeBlinkPattern
    (
    )
{

//* BeginGearsBlock Cpp DEFAULT_LED
//*  Note: The comments within the gears block must use "//*" or "/*"
    //*  Only Snowmass supports charging LED via FPGA
    if (getChargingCount())
    {
        //* On .5 seconds, Off .5 seconds (1 Hz)
        BCM_SET_LED_BLINK_PATTERN(LED_BLINK_RATE1>>1,0x1,2,500);
    }
//* EndGearsBlock Cpp DEFAULT_LED
}

/*******************************************************************************
 * DESCRIPTION:
 * show LED states
 */
void
bid::LEDManager::show
    (
    )
{
    VKI_PRINTF("\nBid LED Manager - All LEDs' states\n");
    VKI_PRINTF("-----------------------------------------------------\n");
    for (UINT32 idx=0; idx < m_SAA.size(); idx++)
    {
        VKI_PRINTF("SAA LED#%d is %s\n", idx,
                   getLEDStateString(getServiceState(idx)));


        //  NoOp for Non-Pattern platform
    }

    VKI_PRINTF("\n");
    for (UINT32 idx=0; idx < m_SAR.size(); idx++)
    {
        VKI_PRINTF("SAR LED#%d is %s\n", idx,
                   getLEDStateString(getFaultState(idx)));


        //  NoOp for Non-Matterhorn platform
    }

    VKI_PRINTF("\n");
    for (UINT32 idx=0; idx < m_Charging.size(); idx++)
    {
        VKI_PRINTF("CHG LED#%d is %s\n", idx,
                   getLEDStateString(getChargingState(idx)));


        //  NoOp for Non-Pattern platform
    }
    VKI_PRINTF("\n");
}

/*******************************************************************************
 * DESCRIPTION:  Reset LED states members to unknown
 *
 * PARAMETERS:  bbuCruInstance    requested local battery cru instance
 *
 */
void
bid::LEDManager::resetLEDStateMembers
    (
    UINT32 bbuCruInstance
    )
{
    if ( bbuCruInstance < m_SAA.size())
    {
        m_SAA[bbuCruInstance] = BID_LED_STATE_UNKNOWN;
    }

    if ( bbuCruInstance < m_SAR.size())
    {
        m_SAR[bbuCruInstance] = BID_LED_STATE_UNKNOWN;
    }

    if ( bbuCruInstance < m_Charging.size())
    {
        m_Charging[bbuCruInstance] = BID_LED_STATE_UNKNOWN;
    }

}

/*******************************************************************************
 * DESCRIPTION: Get Fault LED state
 *
 * PARAMETERS:  bbuCruInstance    requested local battery cru instance
 *
 * RETURN:      Fault LED state
 *
 */
bid::LEDState
bid::LEDManager::getFaultState
    (
    UINT32 bbuCruInstance
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return BID_LED_STATE_UNKNOWN;
    }

    if ( bbuCruInstance < m_SAR.size())
        return m_SAR[bbuCruInstance];
    else
        return BID_LED_STATE_UNKNOWN;
}

/*******************************************************************************
 * DESCRIPTION: Get Service LED state
 *
 * PARAMETERS:  bbuCruInstance    requested local battery cru instance
 *
 * RETURN:      Service LED state
 *
 */
bid::LEDState
bid::LEDManager::getServiceState
    (
    UINT32 bbuCruInstance
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return BID_LED_STATE_UNKNOWN;
    }

    if ( bbuCruInstance < m_SAA.size())
        return m_SAA[bbuCruInstance];
    else
        return BID_LED_STATE_UNKNOWN;
}

/*******************************************************************************
 * DESCRIPTION: Get Charging LED state
 *
 * PARAMETERS:  bbuCruInstance    requested local battery cru instance
 *
 * RETURN:      Charging LED state
 *
 */
bid::LEDState
bid::LEDManager::getChargingState
    (
    UINT32 bbuCruInstance
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return BID_LED_STATE_UNKNOWN;
    }

    if ( bbuCruInstance < m_Charging.size())
        return m_Charging[bbuCruInstance];
    else
        return BID_LED_STATE_UNKNOWN;
}

/*******************************************************************************
 * DESCRIPTION: Set Fault LED state
 *
 * PARAMETERS:  bbuCruInstance  - requested local battery cru instance
 *              state           - requested led state
 *
 */
void
bid::LEDManager::setFaultState
    (
    UINT32 bbuCruInstance,
    LEDState state
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return;
    }

    if ( bbuCruInstance < m_SAR.size())
    {
        dqvkiWrite(bidDqWriter, BID_LED_SET_SAR, bbuCruInstance,
                   m_SAR[bbuCruInstance], state);
        m_SAR[bbuCruInstance] = state;


//* BeginGearsBlock Cpp DEFAULT_LED
//*  Note: The comments within the gears block must use "//*" or "/*"
        BCM_SET_BATTERY_FAULT_LED(bbuCruInstance, state);
//* EndGearsBlock Cpp DEFAULT_LED
    }
}

/*******************************************************************************
 * DESCRIPTION: Set Service LED state
 *
 * PARAMETERS:  bbuCruInstance  - requested local battery cru instance
 *              state           - requested led state
 *
 */
void
bid::LEDManager::setServiceState
    (
    UINT32 bbuCruInstance,
    LEDState state
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return;
    }

    if ( bbuCruInstance < m_SAA.size())
    {
        dqvkiWrite(bidDqWriter, BID_LED_SET_SAA, bbuCruInstance,
                   m_SAA[bbuCruInstance], state);
        m_SAA[bbuCruInstance] = state;


//* BeginGearsBlock Cpp DEFAULT_LED
//*  Note: The comments within the gears block must use "//*" or "/*"
        BCM_SET_BBU_RTR_LED(bbuCruInstance, state);
//* EndGearsBlock Cpp DEFAULT_LED
    }
}

/*******************************************************************************
 * DESCRIPTION: Set Charging LED state
 *
 * PARAMETERS:  bbuCruInstance  - requested local battery cru instance
 *              state           - requested led state
 *
 */
void
bid::LEDManager::setChargingState
    (
    UINT32 bbuCruInstance,
    LEDState state
    )
{
    if (!canLEDActivated(bbuCruInstance))
    {
        resetLEDStateMembers(bbuCruInstance);
        return;
    }

    if ( bbuCruInstance < m_Charging.size())
    {
        dqvkiWrite(bidDqWriter, BID_LED_SET_CHRG, bbuCruInstance,
                   m_Charging[bbuCruInstance], state);
        m_Charging[bbuCruInstance] = state;

//* BeginGearsBlock Cpp DEFAULT_LED
//*  Note: The comments within the gears block must use "//*" or "/*"
        if (state == BID_LED_BLINK1)
        {
            //* On .5 seconds, Off .5 seconds (1 Hz)
            BCM_SET_BBU_CHARGING_LED(bbuCruInstance, LED_BLINK_RATE1);
        }
        else
        {
            BCM_SET_BBU_CHARGING_LED(bbuCruInstance, state);
        }
//* EndGearsBlock Cpp DEFAULT_LED
    }
}

