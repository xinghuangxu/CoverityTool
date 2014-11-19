/*******************************************************************************

NAME            bidLEDMgr.h
VERSION         %version: WIC~5 %
UPDATE DATE     %date_modified: Thu Sep 25 09:40:37 2014 %
PROGRAMMER      %created_by:    douglasp %

        Copyright 2008-2014 NetApp, Inc. All Rights Reserved.


DESCRIPTION: Prototype of LEDMgr class

*******************************************************************************/


#ifndef __INCbidLEDMgr
#define __INCbidLEDMgr

#include "vkiWrap.h"

#include "bidLEDMgmt.h"

#include "bcmDiscreteLines.h"
#include "utlSingletonMgr.h"

#include <vector>

namespace bid
{
    /**********************************************************************
     * LED blink rate is not globally defined but clients
     * must co-ordinate to ensure the proper patterns are
     * displayed
     *
     *  Client    File name                Blink Index
     *  ssm       ssmDaDerivedLEDMgr.h     4,6 (GenericBlinkIndex)
     *                                     4,6,8,10,12
     *                                     14,16,18 (MercuryBlinkIndex)
     *  lem       lemFaultLED.cc           18
     *  bid       bidLEDMgr.h              26 (LEDBlinkIndex)
     *
     */
    enum LEDBlinkIndex
    {
        LED_BLINK_RATE0    = 0,
        LED_BLINK_RATE1    = 26,          // Snowmass allow 12 blink patterns
                                          // and bid will start from the 12th
    };

    class LEDManager;
    typedef utl::SingletonMgr<LEDManager, LEDManagement> LEDMgr;

    /**********************************************************************
     * DESCRIPTION:
     * Singleton class that manages local battery LEDs in the Storage array
     *
     */
    class LEDManager : public bid::LEDManagement
    {
    public:
                   LEDManager();
        virtual    ~LEDManager();

        // LEDMgmt interface
        virtual LEDState    getFaultState(UINT32 bbuCruInstance);
        virtual LEDState    getServiceState(UINT32 bbuCruInstance);
        virtual LEDState    getChargingState(UINT32 bbuCruInstance);

        virtual void  setFaultState(UINT32 bbuCruInstance, LEDState state);
        virtual void  setServiceState(UINT32 bbuCruInstance, LEDState state);
        virtual void  setChargingState(UINT32 bbuCruInstance, LEDState state);

        inline BYTE   getSAACount() const;
        inline BYTE   getSARCount() const;
        inline BYTE   getChargingCount() const;
        inline bool   canLEDActivated(UINT32 bbuCruInstance) const;

        void          initialize();
        void          initializeBlinkPattern();
        void          show();
        void          resetLEDStateMembers(UINT32 bbuCruInstance);

    private:
        LEDManager(const LEDManager&); // not implemented
        LEDManager& operator=(const LEDManager&); // not implemented

        friend class utl::SingletonMgr<LEDManager, LEDManagement>;

        // vector offset corresponds to the actual number of bbuCruInstance
        std::vector<LEDState>     m_SAA;
        std::vector<LEDState>     m_SAR;
        std::vector<LEDState>     m_Charging;

    };
}


/*******************************************************************************
 * DESCRIPTION: Get Service Action Allow LED count
 */
BYTE
bid::LEDManager::getSAACount
    (
    ) const
{
    return 0;
}

/*******************************************************************************
 * DESCRIPTION: Get Service Action Required LED count
 */
BYTE
bid::LEDManager::getSARCount
    (
    ) const
{
    return 1;
}

/*******************************************************************************
 * DESCRIPTION: Get Charging LED count
 */
BYTE
bid::LEDManager::getChargingCount
    (
    ) const
{
    return 1;
}

/*******************************************************************************
 * DESCRIPTION: Can LED be activated with current battery state.
 */
bool
bid::LEDManager::canLEDActivated
    (
    UINT32 bbuCruInstance
    ) const
{
    // Use Gears variable to indicate if LED is physically located inside the
    // BBU CRU or not.  If it is, LED cannot be activated.
    if (false)
        return (BCM_GET_BATTERY_IN_PLACE(bbuCruInstance) == 1) ? true : false;
    else
        return true;
}

#endif        /* End of __INCbidLEDMgr */
