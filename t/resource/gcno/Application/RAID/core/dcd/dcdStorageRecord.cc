/*******************************************************************************

NAME            dcdStorageRecord.cc
VERSION         %version: 7 %
UPDATE DATE     %date_modified: Wed Sep  3 09:58:43 2014 %
PROGRAMMER      %created_by:    gkenneth %

        Copyright 2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION:
This class persists the configuration information used by Cache Core Dump.

*******************************************************************************/

/***  INCLUDES  ***/
#include "vkiWrap.h"
#include "dqvki.h"
#include "rpaLib.h"
#include "dcdStorageRecord.h"
#include "dcdCoreDumpMgmtP.h"
//* BeginGearsBlock Cpp Feature_RPARefactor
#include "rpaDMAMgr.h"
#include "rpaiRegionMgr.h"
//* EndGearsBlock Cpp Feature_RPARefactor
#include "utlFunctions.h"
#include "utlSTLXdr.h"

// The signature is STORE
const char dcd::StorageRecord::STORAGE_RECORD_SIGNATURE[]={0x53, 0x54, 0x4f, 0x52, 0x45};

extern DqWriter* dcdIoDqWriter;
static USHORT dcdIoStorageRecFstrBase = 0;

#define DCD_IO_STORAGE_REC_WRITE_RPA             (dcdIoStorageRecFstrBase + 0)

/******************************************************************************
DESCRIPTION:
Initialization of Debug Queue format strings for storage record.
*/
void
dcdStorageRecDq
    (
    )
{
    dcdIoStorageRecFstrBase = dqvkiNextFsn(dcdIoDqWriter);
    dqvkiAddFstr(dcdIoDqWriter, DCD_IO_STORAGE_REC_WRITE_RPA, FDL_DETAIL, "dcdStorageRec",
                 "%h dcdStorageRec  Write to RPA - dst:0x%llx, src:0x%x and count:%u");
}

/******************************************************************************
DESCRIPTION:
Constructor. Initializes the StorageRecord object.
 */
dcd::StorageRecord::StorageRecord
    (
    )
{
    clear();
}

/******************************************************************************
DESCRIPTION:
Destructor.
 */
dcd::StorageRecord::~StorageRecord
    (
    )
{
}

/******************************************************************************
DESCRIPTION:
Show routine called from shell to display Storage Record contents.
 */
void
dcd::StorageRecord::show
    (
    ) const
{
    VKI_PRINTF("Storage Record information :\n");
    VKI_PRINTF("m_Signature         : ");
    for (UINT i = 0; i < STORAGE_RECORD_SIGNATURE_SIZE; i++)
    {
        VKI_PRINTF("%c", m_Signature[i]);
    }
    VKI_PRINTF("\n");
    VKI_PRINTF("m_Valid             : %d\n",   m_Valid);
    VKI_PRINTF("m_Version           : %u\n",   m_Version);
    VKI_PRINTF("m_State             : %u\n",   m_State);
    VKI_PRINTF("m_Mode              : %u\n",   m_Mode);
    VKI_PRINTF("m_VolumeSsid        : 0x%x\n", m_VolumeSsid);
    VKI_PRINTF("m_AltVolumeSsid     : 0x%x\n", m_AltVolumeSsid);
    VKI_PRINTF("m_NetworkAddress    : ");
    for (UINT i = 0; i < CCD_NETWORK_ADDRESS_SIZE; i++)
    {
        VKI_PRINTF("%c", m_NetworkAddress[i]);
    }
    VKI_PRINTF("\n");
}

/******************************************************************************
DESCRIPTION:
Clear the Storage Record, reset to default values
 */
void
dcd::StorageRecord::clearStorageRecord
    (
    )
{
    clear();
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Returns the valid flag
 */
bool
dcd::StorageRecord::isValid
    (
    ) const
{
    return m_Valid;
}

/******************************************************************************
DESCRIPTION:
Sets the valid flag
 */
void
dcd::StorageRecord::setValid
    (
    bool valid
    )
{
    m_Valid = valid;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Check the Storage Record signature.  Returns true if the signature matches.
*/
bool
dcd::StorageRecord::checkSignature
    (
    ) const
{
    return !VKI_MEMCMP(m_Signature, STORAGE_RECORD_SIGNATURE, STORAGE_RECORD_SIGNATURE_SIZE);
}

/******************************************************************************
DESCRIPTION:
Returns the version number
 */
UINT32
dcd::StorageRecord::getVersion
    (
    ) const
{
    return m_Version;
}

/******************************************************************************
DESCRIPTION:
Sets the version number
 */
void
dcd::StorageRecord::setVersion
    (
    UINT32 version
    )
{
    m_Version = version;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Checks the version number, returns true if it matches the current version
 */
bool
dcd::StorageRecord::checkVersion
    (
    ) const
{
    return (m_Version == STORAGE_RECORD_LAYOUT_VERSION);
}

/******************************************************************************
DESCRIPTION:
Set the state
 */
void
dcd::StorageRecord::setState
    (
    dcd::CcdState state
    )
{
    m_State = state;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Get the state
 */
dcd::CcdState
dcd::StorageRecord::getState
    (
    ) const
{
    return m_State;
}

/******************************************************************************
DESCRIPTION:
Set the storage mode
 */
void
dcd::StorageRecord::setMode
    (
    dcd::CcdStorageMode storageMode
    )
{
    m_Mode = storageMode;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Get the storage mode
 */
dcd::CcdStorageMode
dcd::StorageRecord::getMode
    (
    ) const
{
    return m_Mode;
}

/******************************************************************************
DESCRIPTION:
Set the volume ssid for this controller
 */
void
dcd::StorageRecord::setVolumeSsid
    (
    UINT32 ssid
    )
{
    m_VolumeSsid = ssid;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Get the volume ssid for this controller
 */
UINT32
dcd::StorageRecord::getVolumeSsid
    (
    ) const
{
    return m_VolumeSsid;
}

/******************************************************************************
DESCRIPTION:
Set the volume ssid for the alternate controller
 */
void
dcd::StorageRecord::setAltVolumeSsid
    (
    UINT32 ssid
    )
{
    m_AltVolumeSsid = ssid;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Get the volume ssid for the alternate controller
 */
UINT32
dcd::StorageRecord::getAltVolumeSsid
    (
    ) const
{
    return m_AltVolumeSsid;
}

/******************************************************************************
DESCRIPTION:
Set the storage network path
 */
bool
dcd::StorageRecord::setNetworkPath
    (
    const char* path
    )
{
    if (!path)
        return false;

    VKI_STRNCPY(reinterpret_cast<char*>(m_NetworkAddress), path, CCD_NETWORK_ADDRESS_SIZE);
    m_NetworkAddress[sizeof(m_NetworkAddress)-1] = '\0';
    writeToRpa();

    return true;
}

/******************************************************************************
DESCRIPTION:
Get the storage network path
 */
const char*
dcd::StorageRecord::getNetworkPath
    (
    )
{
    return reinterpret_cast<const char*>(&m_NetworkAddress);
}

/****************************************************************************
DESCRIPTION:
xdr routine for StorageRecord class.
 */
bool
dcd::StorageRecord::xdr
    (
    XDR* xdrs
    )
{
    assert(xdrs != 0);

    if (!xdr_opaque(xdrs, reinterpret_cast<char*>(&m_Signature), STORAGE_RECORD_SIGNATURE_SIZE))
        return false;
    if (!xdr_bool(xdrs, &m_Valid))
        return false;
    if (!xdr_opaque(xdrs, reinterpret_cast<char*>(&m_Pad), STORAGE_RECORD_PAD_SIZE))
        return false;
    if (!xdr_UINT32(xdrs, &m_Version))
        return false;
    if (!xdr_enum(xdrs, reinterpret_cast<enum_t*>(&m_Mode)))
        return false;
    if (!xdr_UINT32(xdrs, &m_VolumeSsid))
        return false;
    if (!xdr_UINT32(xdrs, &m_AltVolumeSsid))
        return false;
    if (!xdr_opaque(xdrs, reinterpret_cast<char*>(&m_NetworkAddress), CCD_NETWORK_ADDRESS_SIZE))
        return false;
    if (!xdr_enum(xdrs, reinterpret_cast<enum_t*>(&m_State)))
        return false;
    if (!xdr_opaque(xdrs, reinterpret_cast<char*>(&m_Reserved), STORAGE_RECORD_RESERVED_SIZE))
        return false;

    return true;
}

/****************************************************************************
DESCRIPTION:
xdr routine to convert StorageRecord class for DOMI messaging.
 */
bool_t xdr_StorageRecord
    (
    XDR*                              xdrs,
    dcd::StorageRecord*               record
    )
{
    return record->xdr(xdrs);
}

/******************************************************************************
DESCRIPTION:
Return the size of the xdr for the Storage Record.
 */
UINT
dcd::StorageRecord::getXdrSize
    (
    )
{
    // Note: bool_t and BYTE(u_char_t) use a 4 byte transfer size

    return (sizeof(ULONG)                 + // SizeVar (added by Agent)
            STORAGE_RECORD_SIGNATURE_SIZE + // m_Signature byte array
            sizeof(bool_t)                + // m_Valid
            STORAGE_RECORD_PAD_SIZE       + // m_Pad byte array
            sizeof(UINT32)                + // m_Version enumeration
            sizeof(UINT32)                + // m_Mode enumeration
            sizeof(UINT32)                + // m_VolumeSsid
            sizeof(UINT32)                + // m_AltVolumeSsid
            CCD_NETWORK_ADDRESS_SIZE      + // m_NetworkAddress byte array
            sizeof(UINT32)                + // m_State enumeration
            STORAGE_RECORD_RESERVED_SIZE);  // m_Reserved byte array
}

/******************************************************************************
DESCRIPTION:
Clear the Storage Record fields that do not need to be maintained.
 */
void
dcd::StorageRecord::clear
    (
    )
{
    m_Valid         = false;
    // Set the current version
    m_Version       = STORAGE_RECORD_LAYOUT_VERSION;
    m_State         = CCD_STATE_UNKNOWN;
    m_Mode          = CCD_UNKNOWN_STORAGE_MODE;
    m_VolumeSsid    = INVALID_SSID;
    m_AltVolumeSsid = INVALID_SSID;

    // Set the signature
    VKI_MEMCPY(m_Signature, STORAGE_RECORD_SIGNATURE, sizeof(STORAGE_RECORD_SIGNATURE));
    VKI_MEMCLEAR(m_Pad, sizeof(m_Pad));
    VKI_MEMCLEAR(m_NetworkAddress, sizeof(m_NetworkAddress));
    VKI_MEMCLEAR(m_Reserved, sizeof(m_Reserved));
}

/******************************************************************************
DESCRIPTION:
Writes the Storage Record to RPA memory.
 */
void
dcd::StorageRecord::writeToRpa
    (
    )
{
    // The source parameter is knowingly cast to UINT32 not CADDR. Casting a pointer
    // (this) to a larger integral type (CADDR) is subject to platform-specific
    // interpretation. The implicit conversion of UINT32 to CADDR will zero the
    // upper bits.
    UINT32 crcValue = 0;
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //CADDR dst = getRpaCoreDumpPhyMemBase() + sizeof(ROCoreDumpMetadataRecord);
    //dqvkiWrite(dcdIoDqWriter, DCD_IO_STORAGE_REC_WRITE_RPA, dst, reinterpret_cast<UINT32>(this),
               //STORAGE_RECORD_SIZE);
    //if (rpaCopyCoreSectionWithCrc(dst, reinterpret_cast<UINT32>(this), STORAGE_RECORD_SIZE, 0,
                                  //&crcValue))
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    CADDR dst = rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP) +
                                                     sizeof(ROCoreDumpMetadataRecord);
    dqvkiWrite(dcdIoDqWriter, DCD_IO_STORAGE_REC_WRITE_RPA, dst, reinterpret_cast<UINT32>(this),
               STORAGE_RECORD_SIZE);
    if (!rpa::DMAMgr::getInstance()->
            copyCoreSectionWithCrc(dst, reinterpret_cast<UINT32>(this), STORAGE_RECORD_SIZE, 0,
                                   &crcValue))
//* EndGearsBlock Cpp Feature_RPARefactor
    {
        VKI_PRINTF("Failed to write dcd::StorageRecord to RPA \n");
        return;
    }
}
