/*******************************************************************************

NAME            bidLogTemperature.cc
VERSION         %version: WIC~17 %
UPDATE DATE     %date_modified: Thu Sep 25 09:41:33 2014 %
PROGRAMMER      %created_by:    douglasp %

        Copyright 2009-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION: provide implementation for the battery temperature logging class


*******************************************************************************/

#include "vkiWrap.h"

#include "bidLogTemperature.h"
#include "bidDftMgmt.h"
#include "bidLib.h"
#include "bidTrace.h"

#include "bcmDiscreteLines.h"
#include "i2c.h"

// Initialize the BBU EEPROM temperature Logging structure
bid::TempCounterStruct bid::TempCountersAccess::m_TempCounterStruct[] =
{
    { bid::LT_COUNTER_OFFSET0, bid::LT_COUNTER_MIN0, bid::LT_COUNTER_MAX0},
    { bid::LT_COUNTER_OFFSET1, bid::LT_COUNTER_MIN1, bid::LT_COUNTER_MAX1},
    { bid::LT_COUNTER_OFFSET2, bid::LT_COUNTER_MIN2, bid::LT_COUNTER_MAX2},
    { bid::LT_COUNTER_OFFSET3, bid::LT_COUNTER_MIN3, bid::LT_COUNTER_MAX3},
    { bid::LT_COUNTER_OFFSET4, bid::LT_COUNTER_MIN4, bid::LT_COUNTER_MAX4},
    { bid::LT_COUNTER_OFFSET5, bid::LT_COUNTER_MIN5, bid::LT_COUNTER_MAX5},
    { bid::LT_COUNTER_OFFSET6, bid::LT_COUNTER_MIN6, bid::LT_COUNTER_MAX6},
    { bid::LT_COUNTER_OFFSET7, bid::LT_COUNTER_MIN7, bid::LT_COUNTER_MAX7},
    { bid::LT_COUNTER_OFFSET8, bid::LT_COUNTER_MIN8, bid::LT_COUNTER_MAX8}
};


static  const char* BID_LOG_TEMP_SIGNATURE = "LSIraid0";
static  const char* BID_LOG_TEMP_ERROR = "INVALID0";
static  const char* BID_CHARGE_RECORD_SIGNATURE = "NetApp  ";

static UINT16 bidLogTempFstrBase;

#define BID_LT_OVERTEMP             (bidLogTempFstrBase + 0 )
#define BID_LT_COUNTER_MAX          (bidLogTempFstrBase + 1 )
#define BID_LT_INVALID_SLOT         (bidLogTempFstrBase + 2 )
#define BID_LT_INVALID_PARAM        (bidLogTempFstrBase + 3 )
#define BID_LT_READ_ERROR           (bidLogTempFstrBase + 4 )
#define BID_LT_WRITE_ERROR          (bidLogTempFstrBase + 5 )
#define BID_LT_RESET_SIGNATURE      (bidLogTempFstrBase + 6 )
#define BID_LT_RESET_COUNTER        (bidLogTempFstrBase + 7 )
#define BID_LT_COUNTER_MARK_ERROR   (bidLogTempFstrBase + 8 )

/*******************************************************************************
* Set up DQ format strings for this file
*/
void
bidLogTempDqTrace
    (
    )
{
    bidLogTempFstrBase = dqvkiNextFsn(bidDqWriter);

    dqvkiAddFstr(bidDqWriter, BID_LT_OVERTEMP,              FDL_NORMAL, "logtmp",
        "%h logtmp       Battery%d's temperature is %d degrees C"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_INVALID_SLOT,          FDL_NORMAL, "logtmp",
        "%h logtmp       Invalid battery slot %d"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_INVALID_PARAM,         FDL_NORMAL, "logtmp",
        "%h logtmp       Invalid parameters: offset(0x%2x), buf(0x%x), bufSize(%d)"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_READ_ERROR,            FDL_NORMAL, "logtmp",
        "%h logtmp       Error reading Battery%d's LogTemp region for offset(0x%2x)"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_WRITE_ERROR,           FDL_NORMAL, "logtmp",
        "%h logtmp       Error writing Battery%d's LogTemp region for offset(0x%2x)"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_RESET_SIGNATURE,       FDL_NORMAL, "logtmp",
        "%h logtmp       Battery%d's LogTemp signature has been reset"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_RESET_COUNTER,         FDL_NORMAL, "logtmp",
        "%h logtmp       Battery%d's LogTemp temp counters has been reset"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_COUNTER_MARK_ERROR,    FDL_NORMAL, "logtmp",
        "%h logtmp       Battery%d's counter at offset(0x%2x) "
                      "marked INVALID0 due to non-ASCII numeric value"
                      );
    dqvkiAddFstr(bidDqWriter, BID_LT_COUNTER_MAX,           FDL_DETAIL, "logtmp",
        "%h logtmp       Battery%d's counter at offset(0x%2x) "
                      "has exceeded the max value (%d)"
                      );
}

/*******************************************************************************
 * DESCRIPTION: Get the number of BBUs that we have to log temperature to its
 *              EEPROM
 */
BYTE
bid::LogTemperature::getBBUCount
    (
    ) const
{
    return 1;
}

/****************************************************************************
 * Description: LogTemperature constructor.
 */
bid::LogTemperature::LogTemperature
    (
    )
{
    m_NofSlots = getBBUCount();
    for (BYTE bbuCt = 0; bbuCt < SLOT_COUNT; bbuCt++)
    {
        m_TempCountersAccess[bbuCt] = 0;
        m_Interval[bbuCt] = 0;
    }

    try
    {
        for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
        {
            m_Interval[bbuCt] = LT_POLL_INTERVAL;
            m_TempCountersAccess[bbuCt] =
                new TempCountersAccess(static_cast<BatterySlot>(bbuCt));
        }
    }
    catch (std::exception& e)
    {
        VKI_CMN_ERR(CE_PANIC, "BID caught exception %s in %s\n",
                    e.what(), __FUNCTION__);
        for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
        {
            delete m_TempCountersAccess[bbuCt];
            m_TempCountersAccess[bbuCt] = 0;
        }
    }
}

/****************************************************************************
 * Description: Destructor for the LogTemperature singleton. Not expected
 *              to be called.
 *
 */
bid::LogTemperature::~LogTemperature
    (
    )
{
    for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
    {
        delete m_TempCountersAccess[bbuCt];
        m_TempCountersAccess[bbuCt] = 0;
    }
}

/*******************************************************************************
 * DESCRIPTION: Get the number of BBUs that we have to log temperature to its
 *              EEPROM
 */
BYTE
bid::ChargeRecord::getBBUCount
    (
    ) const
{
    return 1;
}

/*******************************************************************************
 * DESCRIPTION: Get the number of BBUs that we have to log temperature to its
 *              EEPROM
 */
void
bid::ChargeRecord::show
    (
    )
{
    m_ChargeRecordAccess->show();
}

/*******************************************************************************
 * DESCRIPTION: Get the number of BBUs that we have to log temperature to its
 *              EEPROM
 */
void
bid::ChargeRecordAccess::show
    (
    )
{
    VKI_PRINTF("Battery EEPROM Charge State Region (MEMORY)\n");

    if (!isSignatureValid())
    {
        VKI_PRINTF("\nInvalid Signature\n");
        return;
    }

    VKI_PRINTF("\n");
    VKI_PRINTF("Signature  (%s)\n",m_BidChargeRecordImage.m_Signature);
    VKI_PRINTF("    Version = %d \n", m_BidChargeRecordImage.m_Version);

    VKI_PRINTF("Installation Time           = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_InstallationTime,
            showTime(m_BidChargeRecordImage.m_InstallationTime));

    VKI_PRINTF("LastShallowLearnStop Time   = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_LastShallowLearnStop,
            showTime(m_BidChargeRecordImage.m_LastShallowLearnStop ));

    VKI_PRINTF("LastShallowLearnOkay Time   = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_LastShallowLearnOkay,
            showTime(m_BidChargeRecordImage.m_LastShallowLearnOkay));

    VKI_PRINTF("LastDeepLearnStop Time      = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_LastDeepLearnStop,
            showTime(m_BidChargeRecordImage.m_LastDeepLearnStop));

    VKI_PRINTF("LastDeepLearnOkay Time      = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_LastDeepLearnOkay,
            showTime(m_BidChargeRecordImage.m_LastDeepLearnOkay));

    VKI_PRINTF("LastSleptTime               = 0x%08" PRIxPTR " %s\n",
            m_BidChargeRecordImage.m_LastSleptTime,
            showTime(m_BidChargeRecordImage.m_LastSleptTime));

    VKI_PRINTF("Number of Backup SOD Events = %d\n", m_BidChargeRecordImage.m_BackupSODCount);

}

/****************************************************************************
 * Description: ChargeRecord constructor.
 */
bid::ChargeRecord::ChargeRecord
    (
    )
{
    m_NofSlots = getBBUCount();
    try
    {
        m_ChargeRecordAccess = new ChargeRecordAccess(static_cast<BatterySlot>(FIRST_SLOT));
    }
    catch (std::exception& e)
    {
        VKI_CMN_ERR(CE_PANIC, "BID caught exception %s in %s\n",
                    e.what(), __FUNCTION__);
            delete m_ChargeRecordAccess;
            m_ChargeRecordAccess = 0;
    }
}

/****************************************************************************
 * Description: Destructor for the ChargeRecord singleton. Not expected
 *              to be called.
 */
bid::ChargeRecord::~ChargeRecord
    (
    )
{
    for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
    {
        delete m_ChargeRecordAccess;
        m_ChargeRecordAccess = NULL;
    }
}

/****************************************************************************
 * Description: Destructor for the ChargeRecordAccess. Not expected
 *              to be called.
 */

bid::ChargeRecordAccess::~ChargeRecordAccess
    (
    )
{
}

////////////////////////////////////////////////////////////////////////////////
/////////////////////SYMbolTestAPI and MAPI ////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/*******************************************************************************
 * DESCRIPTION: Show the temperature counters for each BBU EEPROM
 *
 * PARAMETERS:  slot - requested battery slot
 *
 */
void
bid::LogTemperature::showCountersDft
    (
    BatterySlot slot
    ) const
{
    if (slot > m_NofSlots || !m_TempCountersAccess[slot])
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - invalid slot (%d).\n",
                    __FUNCTION__, slot);
        return;
    }

    if (!m_TempCountersAccess[slot]->isSignatureValid())
    {
        if (isI2CError(slot))
        {
            VKI_CMN_ERR(CE_ERROR, "LogTemperature - Error accessing Battery%d's EEPROM"
                                  " LogTemp region\n", slot);
            return;
        }
        else
        {
            VKI_CMN_ERR(CE_ERROR, "LogTemperature - Battery%d has invalid LogTemp "
                                  "signature\n", slot);
        }
    }

    m_TempCountersAccess[slot]->show(LT_TEMP_COUNTERS);
}

/*******************************************************************************
 * DESCRIPTION: Clear or reset the temperature counters for requested BBU EEPROM
 *
 * PARAMETERS:  slot - requested battery slot
 *
 * RETURNS:     0 (OK)                  if clear operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::LogTemperature::clearCountersDft
    (
    BatterySlot slot
    )
{
    if (slot > m_NofSlots || !m_TempCountersAccess[slot])
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - invalid slot (%d).\n",
                    __FUNCTION__, slot);
        return BID_ERROR;
    }

    return m_TempCountersAccess[slot]->initCounters();
}

/*******************************************************************************
 * DESCRIPTION: Show any access activity to the temperature logging region for
 *              the requested BBU
 *
 * PARAMETERS:  slot - requested battery slot
 *
 */
void
bid::LogTemperature::showActivityDft
    (
    BatterySlot slot,
    bool        isEnabled
    )
{
    if (slot > m_NofSlots || !m_TempCountersAccess[slot])
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - invalid slot (%d).\n",
                    __FUNCTION__, slot);
        return;
    }

    m_TempCountersAccess[slot]->setIsShowActivityEnabledDft(isEnabled);
}

/*******************************************************************************
 * DESCRIPTION: Set numeric counter for the requested temperature range of battery
 *              EEPROM temperature logging region
 *
 * PARAMETERS:  slot - requested battery slot
 *              range - the requested temperature range
 *              counter - temperature counter value
 *
 */
void
bid::LogTemperature::setCounterDft
    (
    BatterySlot slot,
    UINT32      range,
    UINT32       counter
    )
{
    if (slot > m_NofSlots
        || !m_TempCountersAccess[slot]
        || range == 0   // region header
        || range > LT_RANGES
        || counter > LT_COUNTER_VALUE_MAX)
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - invalid params Slot(%d), "
                              "range(%d) or counter(%d)\n",
                              __FUNCTION__, slot, range, counter);
        return;
    }

    UINT32 offset = TempCountersAccess::m_TempCounterStruct[range - 1].offset;

    UINT32 tempCount = m_TempCountersAccess[slot]->getCounter(offset);
    if (tempCount != BID_ERROR)
        m_TempCountersAccess[slot]->setCounter(offset, counter);
}

/*******************************************************************************
 * DESCRIPTION: Write user provided data to the requested temperature range of
 *              battery EEPROM temperature logging region including header
 *
 * PARAMETERS:  slot - requested battery slot
 *              range - the requested temperature range
 *              buffer - pointer to the buffer
 *              bufSize - buffer length
 *
 */
void
bid::LogTemperature::setTempRangeDataDft
    (
    BatterySlot slot,
    UINT32      range,
    const BYTE* buffer,
    UINT32      bufSize
    )
{
    if (slot > m_NofSlots
        || !m_TempCountersAccess[slot]
        || range > LT_RANGES
        || buffer == 0
        || bufSize > LT_COUNTER_SIZE)
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - invalid params Slot(%d), "
                              "Range(%d), buf(%p), bufSize(%d)\n",
                              __FUNCTION__, slot, range, buffer, bufSize);
        return;
    }

    UINT32 offset = (range) ? TempCountersAccess::m_TempCounterStruct[range - 1].offset
                           : LT_SIGNATURE_OFFSET;

    BYTE tempBuf[bufSize];
    VKI_MEMCPY(&tempBuf[0], buffer, bufSize);
    UINT32 retStatus = m_TempCountersAccess[slot]->bcmEepromWrite(offset, tempBuf, bufSize);
    if (retStatus == BID_ERROR)
    {
        VKI_CMN_ERR(CE_ERROR, "LogTemperature::%s - Error writing Battery%d's "
                              "EEPROM Temperature Logging Offset(0x%x)\n",
                              __FUNCTION__, slot, offset);
    }
    else
        m_TempCountersAccess[slot]->updateInMemoryData(offset, tempBuf, bufSize);
}

/*******************************************************************************
 * DESCRIPTION: Show the log Temperature information
 *
 * PARAMETERS:  level - the requested show level
 *
 * RETURN:      0 (OK)                if operation is successful
 *              0xffffffff(BID_ERROR) otherwise
 */
UINT32
bid::LogTemperature::show
    (
    LogTempShowLevel level
    )
{
    if (m_NofSlots == 0)
        return BID_ERROR;

    bool isError = false;
    for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
    {
        if (m_TempCountersAccess[bbuCt])
        {
            UINT32 retStatus = m_TempCountersAccess[bbuCt]->show(level);
            isError |= (retStatus != OK) ? true : false;
        }
        else
        {
            VKI_CMN_ERR(CE_NOTE, "LogTemperature - invalid Battery(%d).\n", bbuCt);
            return BID_ERROR;
        }
    }
    return (isError) ? BID_ERROR : OK;
}

/*******************************************************************************
 * DESCRIPTION: Clear the log Temperature region
 *
 * RETURN:     0 (OK)                if operation is successful
 *             0xffffffff(BID_ERROR) otherwise
 */
UINT32
bid::LogTemperature::clearRegion
    (
    )
{
    for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
    {
        if (m_TempCountersAccess[bbuCt])
        {
            if (m_TempCountersAccess[bbuCt]->initRegion() == BID_ERROR)
                return BID_ERROR;
        }
        else
            return BID_ERROR;
    }
    return OK;
}


////////////////////////////////////////////////////////////////////////////////
/////////////////////LogTemperature class internal methods//////////////////////
////////////////////////////////////////////////////////////////////////////////

/*******************************************************************************
 * DESCRIPTION: Initialize the BBU EEPROM value and the LogTemperature object
 *
 */
void
bid::LogTemperature::initialize
    (
    )
{
    // temperature logging will not be enabled in backup mode
    if (isInBackupMode())
        return;

    for (BYTE bbuCt = 0; bbuCt < m_NofSlots; bbuCt++)
    {
        if (m_TempCountersAccess[bbuCt])
            m_TempCountersAccess[bbuCt]->initialize();
        else
        {
            VKI_CMN_ERR(CE_PANIC, "LogTemperature::%s - TempCountersAccess has"
                                  " invalid pointer for battery%d.\n",
                        __FUNCTION__, bbuCt);
        }

    }
}

/*******************************************************************************
 * DESCRIPTION: Initialize the BBU EEPROM value and the ChargeRecord object
 *
 */
void
bid::ChargeRecord::initialize
    (
    )
{
    if (m_ChargeRecordAccess)
        m_ChargeRecordAccess->initialize();
    else
    {
        VKI_CMN_ERR(CE_PANIC, "Charge Record::%s - ChargeRecordAccess has"
                              " invalid pointer for battery%d.\n",
                              __FUNCTION__, FIRST_SLOT);
    }
}
/*******************************************************************************
 * DESCRIPTION: Update EEPROM with current temperature
 *
 * PARAMETERS:  slot - requested battery slot
 *              temp - current temperature needs to be counted
 */
void
bid::LogTemperature::update
    (
    BatterySlot slot,
    UINT32      temp
    )
{
    if (slot > m_NofSlots || !m_TempCountersAccess[slot])
    {
        dqvkiWrite(bidDqWriter, BID_LT_INVALID_SLOT, slot);
        return;
    }

    // Log Temperature is disabled in backup mode
    if (isInBackupMode())
        return;

    // Check to see if temp log region is initialized OK or not
    if (!m_TempCountersAccess[slot]->isSignatureValid())
    {
        // signature is truly invalid and not because of i2c issue
        if (!m_TempCountersAccess[slot]->isI2CError())
        {
            m_TempCountersAccess[slot]->initialize();
        }
        else // If i2c issue, retry during next update interval
            return;
    }

    // Add trace to monitor temp changes over time
    if (temp > LT_TEMP_DQ)
        dqvkiWrite(bidDqWriter, BID_LT_OVERTEMP, slot, temp);

    UINT32 offset = m_TempCountersAccess[slot]->getOffset(temp);
    if (offset != BID_ERROR)
    {
        UINT32 tempCount = m_TempCountersAccess[slot]->getCounter(offset);;
        if (tempCount != BID_ERROR)
        {
            // The tempCount value should not exceed the LT_COUNTER_VALUE_MAX
            // However, having a dq entry to log such condition just in case
            // and stop logging that counter.
            if (tempCount >= LT_COUNTER_VALUE_MAX)
                dqvkiWrite(bidDqWriter, BID_LT_COUNTER_MAX, slot, offset, tempCount);
            else
                m_TempCountersAccess[slot]->setCounter(offset, ++tempCount);
        }
    }
}

/****************************************************************************
 * DESCRIPTION: Temperature counter access object constructor.
 */
bid::TempCountersAccess::TempCountersAccess
    (
    BatterySlot slot
    ) : m_Slot(slot)
      , m_I2CError(false)
      , m_IsShowActivityEnabledDft(false)
{
    VKI_MEMCLEAR(m_LogTempImage, LT_IMAGE_SIZE);
}

/****************************************************************************
 * DESCRIPTION: Eeprom access object constructor.
 */
bid::EepromAccess::EepromAccess
    (
    BatterySlot slot
    ) : m_Slot(slot)
      , m_I2CError(false)
      , m_IsShowActivityEnabledDft(false)
{

}

/****************************************************************************
 * DESCRIPTION: Charge Record access object constructor.
 */
bid::ChargeRecordAccess::ChargeRecordAccess
    (
    BatterySlot slot
    ) : EepromAccess(slot)
{
    VKI_MEMSET(&m_BidChargeRecordImage,0, BID_CHARGE_RECORD_SIZE);
}

/****************************************************************************
 * DESCRIPTION: Temperature counter access object destructor.
 */
bid::TempCountersAccess::~TempCountersAccess
    (
    )
{
}

/****************************************************************************
 * DESCRIPTION: Eeprom access object destructor.
 */
bid::EepromAccess::~EepromAccess
    (
    )
{
}


/*******************************************************************************
 * DESCRIPTION: Initialize the BBU EEPROM value and the LogTemperature object
 *              If EEPROM Temp Log region has valid signature, check the counters
 *              to make sure that they are ASCII encoded, otherwise mark error
 *              and the value will persist in EEPROM.
 *              If the region has invalid signature with no access issue, the whole
 *              region will be initialized with valid signature and 0 for each
 *              temperature counter
 *
 */
void
bid::TempCountersAccess::initialize
    (
    )
{
    if (isSignatureValid())
    {
        // Update in memory signtaure
        VKI_MEMCPY(&m_LogTempImage[0], BID_LOG_TEMP_SIGNATURE, LT_SIGNATURE_SIZE);
        for (BYTE tempRange = 0; tempRange < LT_RANGES; tempRange++)
        {
            UINT32 offset = m_TempCounterStruct[tempRange].offset;
            if (!isTempCounterValid(offset) && !isI2CError())
                markError(offset);
        }
    }
    else
    {
        // Signature is indeed invalid, clear the region
        if (!isI2CError())
            initRegion();
    }
}

/*******************************************************************************
 * DESCRIPTION: Initialize the BBU EEPROM value and the ChargeRecordAccess object
 *              If EEPROM Charge Record region has valid signature, check the counters
 *              to make sure that they are ASCII encoded, otherwise mark error
 *              and the value will be initialized.
 *
 */
void
bid::ChargeRecordAccess::initialize
    (
    )
{
    readRegion();
    if (isSignatureValid() && isVersionValid())
    {
        // Update in memory signtaure
    }
    else
    {
        // Signature is indeed invalid, clear the region
        if (!isI2CError())
        {
            VKI_CMN_ERR(CE_WARN,"BID: Signature are invalid, initializing Eeprom region\n");
            initRegion();
        }
    }

    if (isInBackupMode())
        incrementBackup();
}

/****************************************************************************
 * DESCRIPTION: Check the validity of Log Temperature region signature
 *
 * RETURNS:     true  - valid signature
 *              false - otherwise
 */
bool
bid::TempCountersAccess::isSignatureValid
    (
    )
{
    BYTE buf[LT_SIGNATURE_SIZE];

    // read the data from SMART BBU VPD region
    UINT32 retStatus = bcmEepromRead(LT_SIGNATURE_OFFSET, buf, LT_SIGNATURE_SIZE);

    if (retStatus != BID_ERROR)
        return !VKI_MEMCMP(buf, BID_LOG_TEMP_SIGNATURE, LT_SIGNATURE_SIZE);
    else
        return false;
}

/****************************************************************************
 * DESCRIPTION: Check the validity of Charge Record Signature
 *
 * RETURNS:     true  - valid signature
 *              false - otherwise
 */
bool
bid::ChargeRecordAccess::isSignatureValid
    (
    )
{
    return !VKI_MEMCMP(&m_BidChargeRecordImage.m_Signature,BID_CHARGE_RECORD_SIGNATURE,
            BID_CHARGE_RECORD_SIGNATURE_SIZE);
}

/****************************************************************************
 * DESCRIPTION: Check the validity of Charge Record Version
 *
 * RETURNS:     true  - valid signature
 *              false - otherwise
 */
bool
bid::ChargeRecordAccess::isVersionValid
    (
    )
{
    if (m_BidChargeRecordImage.m_Version == BID_CHARGE_RECORD_VERSION)
        return true;

    return false;
}

/****************************************************************************
 * DESCRIPTION: Check the validity of Log Temperature counter value
 *              Temperature counter BYTE array can only contain ASCII numeric
 *              value
 *
 * PARAMETERS:  offset - the requested temperature counter offset
 *
 * RETURNS:     true  - counter value is valid
 *              false - otherwise
 */
bool
bid::TempCountersAccess::isTempCounterValid
    (
    UINT32 offset
    )
{
    BYTE buf[LT_COUNTER_SIZE];

    // read the data from SMART BBU temperature logging region
    UINT32 retStatus = bcmEepromRead(offset, buf, LT_COUNTER_SIZE);

    if (retStatus != BID_ERROR)
    {
        if (isAsciiNumeric(buf, LT_COUNTER_SIZE))
        {
            // Update in memory temperature counter
            updateInMemoryData(offset, buf, LT_COUNTER_SIZE);
            return true;
        }
    }
    return false;
}

/****************************************************************************
 * DESCRIPTION: Check if Log Temperature counter value has been marked error
 *
 * PARAMETERS: buf - pointer to the BYTE array
 *             bufSize - size of the array
 *
 * RETURNS:    true  - counter value is valid
 *             false - otherwise
 */
bool
bid::TempCountersAccess::hasMarkedError
    (
    BYTE* buf,
    BYTE  bufSize
    ) const
{
    return !VKI_MEMCMP(buf, BID_LOG_TEMP_ERROR, bufSize);
}

/****************************************************************************
 * DESCRIPTION: Check the Log Temperature counter value is in ASCII numeric
 *              format
 *
 * PARAMETERS: buf - pointer to the BYTE array
 *             bufSize - size of the array
 *
 * RETURNS:   true  - counter value is in ascii format
 *            false - otherwise
 */
bool
bid::TempCountersAccess::isAsciiNumeric
    (
    BYTE* buf,
    BYTE  bufSize
    ) const
{
    for ( BYTE idx = 0; idx < bufSize; ++idx, ++buf )
    {
        // if BYTE value is not null and is not numeric
        if ( (*buf != LT_ASCII_NULL)
             && (*buf < LT_ASCII_ZERO || *buf > LT_ASCII_NINE ) )
        {
            return false;
        }
    }
    return true;
}

/****************************************************************************
 * DESCRIPTION: Check the validity of Log Temperature counter value
 *
 * PARAMETERS:  offset - the requested temperature counter offset
 *
 * RETURNS:     temperature counter value
 *
 */
UINT32
bid::TempCountersAccess::getCounter
    (
    UINT32 offset
    )
{
    BYTE tempCounterBuf[LT_COUNTER_SIZE];

    // read the data from SMART BBU EEPROM region
    UINT32 retStatus = bcmEepromRead(offset, tempCounterBuf, LT_COUNTER_SIZE);

    if (retStatus != BID_ERROR)
    {
        if (isAsciiNumeric(tempCounterBuf, LT_COUNTER_SIZE))
            return byte2Ulong(tempCounterBuf, LT_COUNTER_SIZE);

        if (!hasMarkedError(tempCounterBuf, LT_COUNTER_SIZE))
            markError(offset);
    }
    // if there is read issue or content is invalid
    return BID_ERROR;

}

/****************************************************************************
 * DESCRIPTION: Get the EEPROM offset associated with the requested temperature
 *
 * PARAMETERS:  temp - the requested temperature
 *
 * RETURNS:     temperature counter offset
 *
 */
UINT32
bid::TempCountersAccess::getOffset
    (
    UINT32 temp
    )
{
    // Since there is no upper bound for the last offset
    if ( temp > LT_COUNTER_MAX7)
        return LT_COUNTER_OFFSET8;
    else
    {
        for (BYTE tempRange = 0; tempRange < LT_RANGES - 1; tempRange++)
        {
            if (temp <= TempCountersAccess::m_TempCounterStruct[tempRange].upperBound)
                return TempCountersAccess::m_TempCounterStruct[tempRange].offset;
        }
    }
    // Should not happen
    return BID_ERROR;
}

/****************************************************************************
 * DESCRIPTION: Update the in memory temperature counter value
 *
 * PARAMETERS:  offset - requested offset to read from
 *              buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 */
void
bid::TempCountersAccess::updateInMemoryData
    (
    UINT32 offset,
    BYTE*  buf,
    UINT32 bufSize
    )
{
    UINT32 byteIndex = getOffsetIndex(offset);

    if (byteIndex == BID_ERROR
        || buf == 0
        || bufSize > LT_COUNTER_SIZE)
    {
        dqvkiWrite(bidDqWriter, BID_LT_INVALID_PARAM, offset, buf, bufSize);
        return;
    }

    VKI_MEMCPY(&m_LogTempImage[byteIndex], buf, bufSize);
}

/****************************************************************************
 * DESCRIPTION: Set the temperature range counter value in EEPROM
 *
 * PARAMETERS:  offset - the offset to have its counter value changed
 *              tempCount - the temperature counter value
 *
 */
void
bid::TempCountersAccess::setCounter
    (
    UINT32  offset,
    UINT32  tempCount
    )
{
    BYTE tempCounterBuf[LT_COUNTER_SIZE];
    // Convert to ASCII BYTE array
    uint322Byte(tempCount, tempCounterBuf);

    UINT32 retStatus = bcmEepromWrite(offset, tempCounterBuf, LT_COUNTER_SIZE);

    if (retStatus == OK)
        updateInMemoryData(offset, tempCounterBuf, LT_COUNTER_SIZE);
}

/****************************************************************************
 * DESCRIPTION: Show all the temperature counters in the EEPROM
 *
 * PARAMETERS:  level - determine what format to display log temperature region
 *
 * RETURN:      0 (OK)                if operation is successful
 *              0xffffffff(BID_ERROR) otherwise
 */
UINT32
bid::TempCountersAccess::show
    (
    LogTempShowLevel level
    )
{
    UINT32 retStatus = OK;

    // Access EEPROM directly to retrieve temperature logging data
    if (level == LT_EEPROM_RAW)
    {
        BYTE logTempBuf[LT_IMAGE_SIZE];

        // read the data from SMART BBU VPD region
        retStatus = bcmEepromRead(LT_SIGNATURE_OFFSET, logTempBuf, LT_IMAGE_SIZE);

        if (retStatus == BID_ERROR)
        {
            VKI_PRINTF("\nTempCountersAccess::show - Error accessing Battery%d's EEPROM "
                       "region!\n", getSlot());
        }
        else
        {
            VKI_PRINTF("\nBattery EEPROM Log Temperature Region(RAW)\n");
            VKI_PRINTF("------------------------------------------------\n");
            dumpBuffer(&logTempBuf[0], LT_IMAGE_SIZE, LT_SIGNATURE_OFFSET, LT_DUMP_LENGTH);
        }
        return retStatus;
    }

    VKI_PRINTF("\nBattery EEPROM Log Temperature Region(MEMORY)\n");
    VKI_PRINTF("---------------------------------------------------------\n");

    switch (level)
    {
    case LT_LOG_TEMP:        // intentional fall through
        VKI_PRINTF("Log Temperature Signtaure: ");
        dumpString(m_LogTempImage, LT_SIGNATURE_SIZE);
        VKI_PRINTF("\n");
    case LT_TEMP_COUNTERS:
        for (BYTE tempRange = 0; tempRange < LT_RANGES; tempRange++)
        {
            // Buffer start from signature and index need to start from first offset
            BYTE idx = (tempRange + 1) * LT_COUNTER_SIZE;
            VKI_PRINTF("Temp Range %d(%2d-%2d deg C) with offset 0x%2x's count is %d\n",tempRange,
                       m_TempCounterStruct[tempRange].lowerBound,
                       m_TempCounterStruct[tempRange].upperBound,
                       LT_SIGNATURE_OFFSET +  idx,
                       byte2Ulong(&m_LogTempImage[idx], LT_COUNTER_SIZE));
        }
        break;
    case LT_EEPROM_MEM:
        dumpBuffer(&m_LogTempImage[0], LT_IMAGE_SIZE, LT_SIGNATURE_OFFSET, LT_DUMP_LENGTH);
        break;
    default:
        VKI_PRINTF("\nTempCountersAccess::show - Invalid parameter!\n");
        retStatus = BID_ERROR;
        break;
    }

    return retStatus;
}

/****************************************************************************
 * DESCRIPTION: Initialize the EEPROM Log Temp counters
 *              Counter is set to be right justified and use NULL for unused
 *              bytes.
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::TempCountersAccess::initCounters
    (
    )
{
    for (BYTE tempRange = 0; tempRange < LT_RANGES; tempRange++)
    {
        // Initialize temp counters to ascii 0 (right justified) and null the rest
        BYTE base = (tempRange + 1) * LT_COUNTER_SIZE;
        for (BYTE idx = 0; idx < LT_COUNTER_SIZE - 1; idx++)
        {
            m_LogTempImage[base + idx] = LT_ASCII_NULL;
        }
        m_LogTempImage[base + LT_COUNTER_SIZE - 1] = LT_ASCII_ZERO;
    }

    return bcmEepromWrite(LT_COUNTER_OFFSET0, &m_LogTempImage[LT_COUNTER_SIZE],
                          LT_COUNTERS_SIZE);
}

/****************************************************************************
 * DESCRIPTION: Initialize the EEPROM Log Temp region
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::TempCountersAccess::initRegion
    (
    )
{
    VKI_MEMCPY(&m_LogTempImage[0], BID_LOG_TEMP_SIGNATURE, LT_SIGNATURE_SIZE);

    UINT32 retStatus = bcmEepromWrite(LT_SIGNATURE_OFFSET, m_LogTempImage,
                                     LT_SIGNATURE_SIZE);

    if (retStatus == OK)
    {
        dqvkiWrite(bidDqWriter, BID_LT_RESET_SIGNATURE, getSlot());
        retStatus = initCounters();
        if (retStatus == OK)
            dqvkiWrite(bidDqWriter, BID_LT_RESET_COUNTER, getSlot());
    }

    return retStatus;
}

/****************************************************************************
 * DESCRIPTION: Initialize the EEPROM Charge Record region
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::ChargeRecordAccess::initRegion
    (
     bool sign
    )
{
    VKI_MEMSET(&m_BidChargeRecordImage,0, BID_CHARGE_RECORD_SIZE);
    if (sign)
    {
        VKI_MEMCPY(m_BidChargeRecordImage.m_Signature, BID_CHARGE_RECORD_SIGNATURE,
                sizeof(m_BidChargeRecordImage.m_Signature));
        m_BidChargeRecordImage.m_Version = BID_CHARGE_RECORD_VERSION;
    }

    UINT32 retStatus = bcmEepromWrite(BID_CHARGE_RECORD_SIGNATURE_OFFSET,
                                     static_cast<void*>(&m_BidChargeRecordImage),
                                     BID_CHARGE_RECORD_SIZE);
    return retStatus;
}

/****************************************************************************
 * DESCRIPTION: Read the EEPROM Charge Record region
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::ChargeRecordAccess::readRegion
    (
    )
{
    VKI_MEMSET(&m_BidChargeRecordImage,0, BID_CHARGE_RECORD_SIZE);

    UINT32 retStatus = bcmEepromRead(BID_CHARGE_RECORD_SIGNATURE_OFFSET,
                                     static_cast<void*>(&m_BidChargeRecordImage),
                                     BID_CHARGE_RECORD_SIZE);
    return retStatus;
}

/****************************************************************************
 * DESCRIPTION: set the EEPROM Charge Record Learn cycle event
 *
 * PARAMETERS:
 *              LearnMethod - type of the learn cycle
 *              success - if the learn cycle execution was successful
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::ChargeRecordAccess::setLearnCycleComplete
    (
     LearnMethod lm,
     bool successful
     )
{
    if (lm == LEARN_DEEP_DISCHARGE)
        setDeepLearnComplete(successful);
    else
        setShallowLearnComplete(successful);

    UINT32 retStatus = bcmEepromWrite(BID_CHARGE_RECORD_SIGNATURE_OFFSET, &m_BidChargeRecordImage,
            BID_CHARGE_RECORD_SIZE);

    return retStatus;
}

/**
 * This operations sets the time in the Charge Record Image
 * and write the image to the VPD of the BBU
 *
 * \param time_t t
 *
 * \returns UINT32 - status  of operation
 *
 * bid::ChargeRecordAccess::setInstallationTime
 *
 */
UINT32
bid::ChargeRecordAccess::setInstallationTime
    (
     time_t t
     )
{
    m_BidChargeRecordImage.m_InstallationTime = (t)? t : VKI_TIME(0);

    UINT32 retStatus = bcmEepromWrite(BID_CHARGE_RECORD_SIGNATURE_OFFSET, &m_BidChargeRecordImage,
            BID_CHARGE_RECORD_SIZE);

    return retStatus;
}

/**
 * This operations sets the sleep time in the Charge Record Image
 * and write the image to the VPD of the BBU
 *
 * \param time_t t
 *
 * \returns UINT32 - status  of operation
 *
 */
UINT32
bid::ChargeRecordAccess::setLastSleptTime
    (
     time_t t
     )
{
    m_BidChargeRecordImage.m_LastSleptTime = (t)? t : VKI_TIME(0);

    UINT32 retStatus = bcmEepromWrite(BID_CHARGE_RECORD_SIGNATURE_OFFSET, &m_BidChargeRecordImage,
            BID_CHARGE_RECORD_SIZE);

    return retStatus;
}

/**
 * This operations returns the last slept time stored in memory
 *
 * \returns time_t - time of last sleep
 *
 * \note this does not instigate READ of the VPD page
 */
time_t
bid::ChargeRecordAccess::getLastSleptTime
    (
     )
{
    return m_BidChargeRecordImage.m_LastSleptTime;
}

/**
 * This interface calls to ChargeRecordAccess to set the time
 * \param time_t t
 *
 * \returns UINT32 - status  of operation
 *
 * bid::ChargeRecord::setLastSleptTime
 *
 *
 */
UINT32
bid::ChargeRecord::setLastSleptTime
    (
     time_t t
     )
{
    time_t now = t ? t : VKI_TIME(0);

    if (!m_ChargeRecordAccess->isSignatureValid())
        return 0;

    return  m_ChargeRecordAccess->setLastSleptTime(now);
}

/**
 * This interface calls to ChargeRecordAcces to obtain last sleep time
 *
 * \returns time_t - time of last sleep
 *
 * bid::ChargeRecord::getLastSleptTime
 *
 */
time_t
bid::ChargeRecord::getLastSleptTime
    (
     )
{
    return  m_ChargeRecordAccess->getLastSleptTime();
}


/**
 * This interface calls to ChargeRecordAccess to set the time
 * \param time_t t
 *
 * \returns UINT32 - status  of operation
 *
 * bid::ChargeRecord::setInstallationTime
 *
 *
 */
UINT32
bid::ChargeRecord::setInstallationTime
    (
     time_t t
     )
{
    if (!m_ChargeRecordAccess->isSignatureValid())
        return 0;

    return  m_ChargeRecordAccess->setInstallationTime(t);
}

/**
 * This operation returns the installation time stored in memory
 *
 * \returns time_t - time of installation
 *
 * \note this does not instigate READ of the VPD page
 */
time_t
bid::ChargeRecordAccess::getInstallationTime
    (
     )
{
    return m_BidChargeRecordImage.m_InstallationTime;
}

/**
 * This interface calls to ChargeRecordAcces to obtain installation time
 *
 * \returns time_t - time of installation
 *
 * bid::ChargeRecord::getInstallationTime
 *
 */
time_t
bid::ChargeRecord::getInstallationTime
    (
     )
{
    return  m_ChargeRecordAccess->getInstallationTime();
}

/****************************************************************************
 * DESCRIPTION: get the type of learn cycle being executed
 *
 */
time_t
bid::ChargeRecordAccess::getLearnCycleComplete
    (
     LearnMethod lm
     )
{
    if (lm == LEARN_DEEP_DISCHARGE)
        return getDeepLearnComplete();
    else
        return getShallowLearnComplete();
}

/****************************************************************************
 * DESCRIPTION: set learn cycle complete calling method
 */
UINT32
bid::ChargeRecord::setLearnCycleComplete
    (
     LearnMethod lm,
     bool successful
     )
{
    return  m_ChargeRecordAccess->setLearnCycleComplete(lm,successful);
}

/****************************************************************************
 * DESCRIPTION: set learn cycle complete calling method
 *
 * PARAMETERS:
 *            LearnMethod - The type of learn cycle
 *
 * RETURNS:
 *            time_t - time of completed learn cycle
 */
time_t
bid::ChargeRecord::getLearnCycleComplete
    (
     LearnMethod lm
     )
{
    return  m_ChargeRecordAccess->getLearnCycleComplete(lm);
}

/****************************************************************************
 * DESCRIPTION: calling function to clear Charge record region
 *
 */
UINT32
bid::ChargeRecord::clearRegion
    (
     )
{

    return  m_ChargeRecordAccess->clearRegion();

}

/****************************************************************************
 * DESCRIPTION: clear Charge record region
 *
 */
UINT32
bid::ChargeRecordAccess::clearRegion
    (
     )
{
    return initRegion(0);
}

/****************************************************************************
 * DESCRIPTION: Get the temperature logging region index for requested offset
 *
 * PARAMETERS:  offset - the offset has invalid value
 *
 */
UINT32
bid::TempCountersAccess::getOffsetIndex
    (
    UINT32 offset
    )
{
    if (offset == LT_SIGNATURE_OFFSET)
        return 0;
    else
    {
        for (BYTE tempRange = 0; tempRange < LT_RANGES; tempRange++)
        {
            // Region index starts from signature offset
            if (m_TempCounterStruct[tempRange].offset == offset)
                return (tempRange + 1) * LT_COUNTER_SIZE;
        }
    }
    return BID_ERROR;
}

/****************************************************************************
 * DESCRIPTION: Mark the temperature logging for the requested  offset error
 *
 * PARAMETERS:  offset - the offset has invalid value
 *
 */
void
bid::TempCountersAccess::markError
    (
    UINT32 offset
    )
{
    BYTE tempCounterBuf[LT_COUNTER_SIZE];
    VKI_MEMCPY(&tempCounterBuf[0], BID_LOG_TEMP_ERROR, LT_COUNTER_SIZE);

    UINT32 retStatus = bcmEepromWrite(offset, tempCounterBuf, LT_COUNTER_SIZE);
    if (retStatus == OK)
    {
        UINT32 byteCount = getOffsetIndex(offset);
        if (byteCount != BID_ERROR)
        {
            VKI_MEMCPY(&m_LogTempImage[byteCount], tempCounterBuf, LT_COUNTER_SIZE);
            dqvkiWrite(bidDqWriter, BID_LT_COUNTER_MARK_ERROR, getSlot(), offset);
        }
    }
}

/************************************************************************************
 * DESCRIPTION: Access EEPROM to read the log Temp region
 *              Caller is responsible for allocating enough memory for the buffer
 *
 * PARAMETERS:  offset - requested offset to read from
 *              buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::TempCountersAccess::bcmEepromRead
    (
    UINT32 offset,
    BYTE*  buf,
    UINT32 bufSize
    )
{
    // read the data from SMART BBU VPD region
    UINT32 retStatus =  static_cast<UINT32>(BCM_ATTEMPT_READ_VPD_DATA(BBU_VPD_DEVNUM,
                                          getSlot(), offset, bufSize, buf));
    if (retStatus != OK)
    {
        setI2CError(true);
        dqvkiWrite(bidDqWriter, BID_LT_READ_ERROR, getSlot(), offset);
    }
    else
    {
        setI2CError(false);
        return OK;
    }

    return BID_ERROR;
}

/************************************************************************************
 * DESCRIPTION: Access EEPROM to read the Eeprom Access
 *              Caller is responsible for allocating enough memory for the buffer
 *
 * PARAMETERS:  offset - requested offset to read from
 *              buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::EepromAccess::bcmEepromRead
    (
    UINT32 offset,
    void*  buf,
    UINT32 bufSize
    )
{
    // read the data from SMART BBU VPD region
    UINT32 retStatus =  static_cast<UINT32>(BCM_ATTEMPT_READ_VPD_DATA(BBU_VPD_DEVNUM,
                                          getSlot(), offset, bufSize,
                                          static_cast<BYTE*>(buf)));
    if (retStatus != OK)
    {
        setI2CError(true);
        dqvkiWrite(bidDqWriter, BID_LT_READ_ERROR, getSlot(), offset);
    }
    else
    {
        setI2CError(false);
        return OK;
    }

    return BID_ERROR;
}


/************************************************************************************
 * DESCRIPTION: Access EEPROM to write the log Temp region
 *              Caller is responsible for allocating enough memory for the buffer
 *
 * PARAMETERS:  offset - requested offset to read from
 *              buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::TempCountersAccess::bcmEepromWrite
    (
    UINT32 offset,
    BYTE*  buf,
    UINT32 bufSize
    )
{
    if (isShowActivityEnabledDft())
    {
        VKI_CMN_ERR(CE_NOTE, "BID - write access to Log Temperature Region with "
                             "offset(0x%2x)\n", offset);
    }

    // write the data to SMART BBU VPD region
    UINT32 retStatus =  static_cast<UINT32>(BCM_ATTEMPT_WRITE_VPD_DATA(BBU_VPD_DEVNUM,
                                          getSlot(), offset, bufSize, buf));

    if (retStatus != OK)
    {
        setI2CError(true);
        dqvkiWrite(bidDqWriter, BID_LT_WRITE_ERROR, getSlot(), offset);
    }
    else
    {
        setI2CError(false);
        return OK;
    }

    return BID_ERROR;
}


/************************************************************************************
 * DESCRIPTION: setter for shallow discharge learn cycle in charge record region
 *
 * PARAMETER:
 *              success - if learn cycle was successful or not
 */

UINT32
bid::ChargeRecordAccess::setShallowLearnComplete
    (
     bool success
    )
{
    time_t now = VKI_TIME(0);

    m_BidChargeRecordImage.m_LastShallowLearnStop = now;

    if (success)
        m_BidChargeRecordImage.m_LastShallowLearnOkay = now;

    return OK;
}

/************************************************************************************
 * DESCRIPTION: get shallow discharge learn cycle in charge record region
 *
 * RETURNS:
 *      time_t returns the time of the specified learn cycle
 */
time_t
bid::ChargeRecordAccess::getShallowLearnComplete
    (
    )
{
    return m_BidChargeRecordImage.m_LastShallowLearnStop;
}

/************************************************************************************
 * DESCRIPTION: set shallow discharge learn cycle in charge record region
 *
 * PARAMETER:
 *              success - if learn cycle was successful or not
 *
 */
UINT32
bid::ChargeRecordAccess::setDeepLearnComplete
    (
     bool success
    )
{
    time_t now = VKI_TIME(0);

    m_BidChargeRecordImage.m_LastDeepLearnStop = now;

    if (success)
        m_BidChargeRecordImage.m_LastDeepLearnOkay = now;

    return OK;
}

/**
 * \returns UINT32 status 0 (OK) or 0xffffffff(BID_ERROR)
 *
 * Increment the Backup SOD counter in the VPD page of the BBU
 * This will aid in determing the total number of power cycles the
 * battery has observed
 *
 */
UINT32
bid::ChargeRecordAccess::incrementBackup
    (
    )
{
    m_BidChargeRecordImage.m_BackupSODCount++;

    UINT32 retStatus = bcmEepromWrite(BID_CHARGE_RECORD_SIGNATURE_OFFSET, &m_BidChargeRecordImage,
            BID_CHARGE_RECORD_SIZE);

    return retStatus;
}

/**
 * \returns UINT32 The total number of backup SODs observed by this BBU
 *
 *
 * get the total number of back up SOD events observed by this BBU
 *
 */
UINT32
bid::ChargeRecordAccess::getBackupSODCount
    (
    )
{
    return m_BidChargeRecordImage.m_BackupSODCount;
}

/************************************************************************************
 * DESCRIPTION: get deep discharge learn cycle in charge record region
 *
 * RETURNS:
 *      time_t returns the time of the specified learn cycle
 */
time_t
bid::ChargeRecordAccess::getDeepLearnComplete
    (
    )
{
    return m_BidChargeRecordImage.m_LastDeepLearnStop;
}

/************************************************************************************
 * DESCRIPTION: write operation to eeprom of charge record region
 *
 * PARAMETERS:  offset - requested offset to read from
 *              buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * RETURNS:     0 (OK)                  if operation is successful
 *              0xffffffff(BID_ERROR)   otherwise
 */
UINT32
bid::EepromAccess::bcmEepromWrite
    (
    UINT32 offset,
    void*  buf,
    UINT32 bufSize
    )
{
    if (isShowActivityEnabledDft())
    {
        VKI_CMN_ERR(CE_NOTE, "BID - write access to Log Temperature Region with "
                             "offset(0x%2x)\n", offset);
    }

    // write the data to SMART BBU VPD region
    UINT32 retStatus =  static_cast<UINT32>(BCM_ATTEMPT_WRITE_VPD_DATA(BBU_VPD_DEVNUM,
                                          getSlot(), offset, bufSize,
                                          static_cast<BYTE*>(buf)));

    if (retStatus != OK)
    {
        setI2CError(true);
        dqvkiWrite(bidDqWriter, BID_LT_WRITE_ERROR, getSlot(), offset);
    }
    else
    {
        setI2CError(false);
        return OK;
    }

    return BID_ERROR;
}

/************************************************************************************
 * DESCRIPTION: Convert from BYTE array of ASCII to UINT32
 *
 * PARAMETERS:  buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * RETURN:      tempCount - temperature counter value
 *
 * NOTE: The array is MSByte justified and null for unused bytes.  Thus during
 *       conversion, null values are ignored and ascii value will change back to
 *       decimal value.
 *
 */
UINT32
bid::TempCountersAccess::byte2Ulong
    (
    BYTE* buf,
    UINT32 bufSize
    )
{
    UINT32 tempCount = 0;

    for (BYTE idx = 0; idx < LT_COUNTER_SIZE; idx++)
    {
        if (buf[idx] != LT_ASCII_NULL)
        {
            if (buf[idx] == LT_ASCII_ZERO)
                tempCount = tempCount * 10;
            else
                tempCount = tempCount * 10 + ( buf[idx] - LT_ASCII_ZERO);
        }
    }
    return tempCount;
}

/************************************************************************************
 * DESCRIPTION: Convert from UINT32 to BYTE array of ASCII with size of LT_COUNTER_SIZE
 *
 * PARAMETERS:  buf - pointer to the BYTE array
 *              bufSize - size of the array
 *
 * NOTE: The value is right justified and use null for the rest of the unused bytes
 *
 */
void
bid::TempCountersAccess::uint322Byte
    (
    UINT32 tempCount,
    BYTE*  buf
    )
{
    for ( int idx = LT_COUNTER_SIZE - 1; idx >= 0 ; idx--)
    {
        BYTE tmp = LT_ASCII_NULL;
        if (tempCount != 0)
            tmp = tempCount % 10 + LT_ASCII_ZERO;

        buf[idx] = tmp;
        tempCount /= 10;
    }
}

extern "C"
{

void bidtestshow(bid::LogTempShowLevel input)
{
    bid::LogTemp::getInstance()->show(input);
}

void bidtime()
{
    bid::LogTemp::getInstance()->setIntervalDft(bid::FIRST_SLOT, 1);
}

void biderror( int test, UINT32 input, UINT32 input1)
{
    if (test == 1)
    {
        // modify sig
        BYTE sig[] = "GABAGE0";
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 0, sig, sizeof(sig));
    }
    else if (test == 2)
    {
        bid::LogTemp::getInstance()->update(bid::FIRST_SLOT, input);
    }
    else if (test == 3)
    {
        BYTE buf2[8] = { 9,1,1,9,1,1,9,9};
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 3, buf2, sizeof(buf2));
    }
    else if (test == 4)
    {
        bid::LogTemp::getInstance()->clearCountersDft(bid::FIRST_SLOT);
    }
    else if (test == 5)
    {
        BYTE buf3[8] = { 0x39,0x31,0x31,0x00,0x00,0x00,0x00,0x00};
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 6, buf3, sizeof(buf3));
        BYTE buf4[8] = {  0x39,0x31,0x31,0x00,0x00,0x00,0x00,0xe0};
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 4, buf4, sizeof(buf4));
    }
    else if (test == 6)
    {
        BYTE buf3[8] = { 0x39,0x39,0x39,0x39,0x39,0x39,0x39,0x39};
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 9, buf3, sizeof(buf3));
    }
    else if (test == 7)
    {
        bid::LogTemp::getInstance()->setIntervalDft(bid::FIRST_SLOT, input);
    }
    else if (test == 8)
    {
        bid::LogTemp::getInstance()->clearCountersDft(bid::FIRST_SLOT);
    }
    else if (test == 9)
    {
        BYTE buf1[] = "Q";
        bid::LogTemp::getInstance()->setTempRangeDataDft(bid::FIRST_SLOT, 8, buf1, 1);
    }
    else if (test == 10)
    {
        bid::LogTemp::getInstance()->showActivityDft(bid::FIRST_SLOT, (input) ? true : false);
    }
    else if (test == 20)
    {
        bid::BatteryDftMgmt::getInstance()->setTemperatureDft(bid::FIRST_SLOT, input);
    }
    else if (test == 21)
    {
        bid::BatteryDftMgmt::getInstance()->showTempLogDft(bid::FIRST_SLOT);
    }
    else if (test == 22)
    {
        bid::BatteryDftMgmt::getInstance()->clearTempLogDft(bid::FIRST_SLOT);
    }
    else if (test == 23)
    {
        bid::BatteryDftMgmt::getInstance()->setTempLogIntervalDft(bid::FIRST_SLOT, input);
    }
    else if (test == 24)
    {
        bid::BatteryDftMgmt::getInstance()->showTempLogActivityDft(bid::FIRST_SLOT, input);
    }
    else if (test == 25)
    {
        bid::BatteryDftMgmt::getInstance()->setTempLogCounterDft(bid::FIRST_SLOT, input, input1);
    }
    else if (test == 26)
    {
        BYTE buf1[] = "TEST";
        bid::BatteryDftMgmt::getInstance()->writeTempLogRegionDft(bid::FIRST_SLOT, input, buf1, 4);
    }
}

}
