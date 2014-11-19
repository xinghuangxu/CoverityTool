/*******************************************************************************

NAME            bidBatteryDriverMgrVar.cc
VERSION         %version: WIC~6 %
UPDATE DATE     %date_modified: Fri Sep 30 10:04:59 2011 %
PROGRAMMER      %created_by:    douglasp %

        Copyright 2009-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION: Battery Driver Manager Gears Variation file

*******************************************************************************/

#include "bidBatteryDriverMgr.h"

/*******************************************************************************
 * DESCRIPTION:  Get battery type defined in bidBatteryStatus.h
 */
const bid::BatteryType
bid::BatteryDriverManager::getType
    (
    ) const
{
    return (BBU_SINGLE_INDIVIDUAL_FRU);
}

/*******************************************************************************
 * DESCRIPTION:  process battery level initialization.
 *               Access mode can be transitioned to seal mode directly from
 *               either Unseal or Full Access mode
 *
 */
void
bid::BatteryDriverManager::initializeBatteries
    (
    )
{
    if (!isInBackupMode())
    {
//* BeginGearsBlock Cpp ApplicationSOD

        wakeUp();

        setSecurityAccessMode(SLOT_COUNT, SEAL);

//* EndGearsBlock Cpp ApplicationSOD
    }

    if (m_Battery1)
    {
        m_Battery1->mutexLock();
        m_Battery1->updateRegisterStates();
        m_Battery1->mutexUnlock();
    }
    if (m_Battery2)
    {
        m_Battery2->mutexLock();
        m_Battery2->updateRegisterStates();
        m_Battery2->mutexUnlock();
    }
}
