/*******************************************************************************

NAME            dcdCoreDumpMetadata.cc
VERSION         %version: 49 %
UPDATE DATE     %date_modified: Mon Sep 15 14:49:16 2014 %
PROGRAMMER      %created_by:    adrians %

        Copyright 2011-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION:
Implementation for read-write metadata record.

*******************************************************************************/

#include "vkiWrap.h"
#include "bcmArrayInfo.h"
#include "bcmBoardInfo.h"
#include "cacheAddr.h"
#include "cmnCtlrSlotMgmt.h"
#include "dcdCoreDumpMetadata.h"
#include "dcdRebootReason.h"
#include "dqvki.h"
#include "refmControllerRefData.h"
#include "refmRefMgmt.h"
//* BeginGearsBlock Cpp Feature_RPARefactor
#include "rpaDMAMgr.h"
#include "rpaiRegionMgr.h"
//* EndGearsBlock Cpp Feature_RPARefactor
#include "rpaLib.h"
#include "utlFunctions.h"
#include "RepositoryDefs.h"


extern DqWriter* dcdIoDqWriter;
static USHORT dcdIoRWMetadataFstrBase = 0;

#define DCD_IO_METADATA_WRITE_RPA               (dcdIoRWMetadataFstrBase + 0)

/******************************************************************************
DESCRIPTION:
Initialization of Debug Queue format strings for metadata.
*/
void
dcdRWMetadataDq
    (
    )
{
    dcdIoRWMetadataFstrBase = dqvkiNextFsn(dcdIoDqWriter);
    dqvkiAddFstr(dcdIoDqWriter, DCD_IO_METADATA_WRITE_RPA, FDL_DETAIL, "dcdRWMetaD",
                 "%h dcdRWMetaD   Write to RPA - dst:0x%llx, src:0x%x and count:%u");
}

/******************************************************************************
DESCRIPTION:
Constructor. Initializes the RWCoreDumpMetadataRecord object.
 */
dcd::RWCoreDumpMetadataRecord::RWCoreDumpMetadataRecord
    (
    )
{
}

/******************************************************************************
DESCRIPTION:
Destructor.
 */
dcd::RWCoreDumpMetadataRecord::~RWCoreDumpMetadataRecord
    (
    )
{
}

/******************************************************************************
DESCRIPTION:
Setter function for creation metadata attributes and copies the metadata to core dump
area in RPA.
 */
void
dcd::RWCoreDumpMetadataRecord::startDump
    (
    CoreDumpManagement::DumpType type,
    COREDUMPINFO* info,
    const char* panicString,
    const char* scratchpad,
    UINT32 creationTime,
    UINT32 rebootReason,
    RWModuleInfo::ModuleInfoVector & moduleInfoVector,
    UINT32 firstSectionOffset
    )
{
    clear();
    populateBoardInfo(false);
    VKI_MEMCPY(m_Signature, COREDUMP_METADATA_SIGNATURE, COREDUMP_SIGNATURE_SIZE);

    m_DumpType = type;
    // If core dump triggered by assert, filename/line number are populated otherwise will be NULL 
    if(info)
    {
        VKI_STRNCPY(reinterpret_cast<char*>(m_File),info->filename,DCD_FILE_NAME_SIZE);
        m_File[sizeof(m_File)-1] = '\0';
        m_LineNumber = info->lineNumber;
    }
    // If core dump triggered by panic, panicString is populated otherwise will be NULL
    if(panicString)
    {
        VKI_STRNCPY(reinterpret_cast<char*>(m_PanicString), panicString, DCD_PANIC_STRING_SIZE);
        m_PanicString[sizeof(m_PanicString)-1] = '\0';
    }
    // The scratchpad provides debug info if there is a notable problem during successful collection
    if (scratchpad)
    {
        VKI_STRNCPY(reinterpret_cast<char*>(m_Scratchpad), scratchpad, DEV_SCRATCHPAD_SIZE);
        m_Scratchpad[sizeof(m_Scratchpad)-1] = '\0';
    }
    writeError(NO_ERROR);
    m_Valid = false;
    m_NeedsRetrieval = false;
    m_CheckPoint = CoreDumpManagement::COLLECTING_DATA;
    m_Version = METADATA_LAYOUT_VERSION;
    m_CoreDumpTime = creationTime;
    m_RebootReason = rebootReason;
    m_FirstSectionOffset = firstSectionOffset;
    m_ModuleSize = sizeof(RWModuleInfo);
    m_ModuleCount = setModuleInfo(moduleInfoVector);
    // Don't overwrite the previous valid state
    if (m_MajorEvent != CoreDumpManagement::OVER_WRITTEN)
        m_MajorEvent = CoreDumpManagement::CAPTURED;

//* BeginGearsBlock Cpp Feature_CacheCoreDump
    //* If this is a cache core dump, set the metadata appropriately;
    //* otherwise, clear the CcdType to prevent confusion/mishandling.
    if (rebootReason == static_cast<UINT32>(CCD_COREDUMP))
        m_Ccd = true;
    else
        m_CcdType = dcd::UNKNOWN;
//* EndGearsBlock Cpp Feature_CacheCoreDump

    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Setter function for completion metadata attributes and copies the metadata to core dump
area in RPA.
 */
void
dcd::RWCoreDumpMetadataRecord::finishDump
    (
    UINT32 dumpTime,
    CADDR  totalSize,
    CADDR  sharedRegionBase
    )
{
    m_Valid = true;
    if (m_DumpType == dcd::CoreDumpManagement::FULL_DUMP)
    {
        m_NeedsRetrieval = true;
        m_NeedsBackup = true;
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
        m_Ecd = true;
//* EndGearsBlock Cpp Feature_EnhancedCoreDump
    }

    m_CheckPoint = CoreDumpManagement::CAPTURE_COMPLETE;
    m_DumpTimeMSec = dumpTime;
    m_TotalSizeInMem = totalSize;
    m_TotalSizeOnDisk = 0;
    m_SharedRegionBase = sharedRegionBase;
    ++m_Tag;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Sets Needs Retrieval flag of metadata record.
 */
void
dcd::RWCoreDumpMetadataRecord::setNeedsRetrieval
    (
    bool needsRetrieval
    )
{
    m_NeedsRetrieval = needsRetrieval;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the valid flag for metadata record and writes it to RPA.
 */
void
dcd::RWCoreDumpMetadataRecord::setCoreValid
    (
    bool valid
    )
{
    if (!valid)
    {
        m_NeedsRetrieval = false;
        m_NeedsBackup = false;
    }
    m_Valid = valid;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the CaptureEventStatus of metadata record and writes to RPA
 */
void
dcd::RWCoreDumpMetadataRecord::setEventStatus
    (
    CoreDumpManagement::DumpMajorEvent majorEvent
    )
{
    m_MajorEvent = majorEvent;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the core dump timestamp of metadata record and writes to RPA
 */
void
dcd::RWCoreDumpMetadataRecord::setCoreDumpTime
    (
    UINT32 timestamp
    )
{
    m_CoreDumpTime = timestamp;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the tag  of metadata record and writes it to RPA.
 */
void
dcd::RWCoreDumpMetadataRecord::setCoreTag
    (
    UINT32 tag
    )
{
    m_Tag = tag;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the checkpoint in metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setCheckpoint
    (
    CoreDumpManagement::DumpCheckpoint checkpoint
    )
{
    m_CheckPoint = checkpoint;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the notes section size in metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setNotesSize
    (
    UINT32 notesSize
    )
{
    m_NotesSize = notesSize;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the core dump retrieval time in metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setRetrievalTime
    (
    UINT32 retrievalTime
    )
{
    m_CoreRetrievalTime = retrievalTime;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Sets Needs Backup flag of metadata record.
 */
void
dcd::RWCoreDumpMetadataRecord::setNeedsBackup
    (
    bool needsBackup
    )
{
    m_NeedsBackup = needsBackup;
    writeToRpa();
}
/******************************************************************************
DESCRIPTION:
Updates the hardware/firmware information into the metadata object.
 */
void
dcd::RWCoreDumpMetadataRecord::populateBoardInfo
    (
    bool persist
    )
{
    VKI_MEMCPY(m_BoardSerial, bcmArray.Board_Serial_Number, BOARD_SERIAL_NUMBER_SIZE);
    
    const char *versionStr = PRODUCT_VERSION;
    // The software version is parsed from the PRODUCT_VERSION string which
    // is of the format xx.xx.xx.xx. Use all 4 bytes since we know there is no null terminator.
    // Use standard C string parsing functions to get the data into the
    // proper format.
    BYTE* targetRevision = reinterpret_cast<BYTE*>(m_FirmwareVersion);
    targetRevision[0] = static_cast<BYTE>(VKI_STRNTOUL(&versionStr[0],16,2) & 0xFF);
    targetRevision[1] = static_cast<BYTE>(VKI_STRNTOUL(&versionStr[3],16,2) & 0xFF);
    targetRevision[2] = static_cast<BYTE>(VKI_STRNTOUL(&versionStr[6],16,2) & 0xFF);
    targetRevision[3] = static_cast<BYTE>(VKI_STRNTOUL(&versionStr[9],16,2) & 0xFF);

    VKI_MEMCPY(m_BoardIdentifier, bcmBoard.Board_Identifier, BOARD_IDENTIFIER_SIZE);
    m_Ctlr = static_cast<BYTE>(cmn::CtlrSlotMgmt::getInstance()->getThisCtlrSlotNumber());
    if (persist)
        writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Set an indicator for CRC support
 */
void
dcd::RWCoreDumpMetadataRecord::setCrcSupported
    (
    bool crcSupported
    )

{
    m_CrcSupported = crcSupported;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Save the CRC value
 */
void
dcd::RWCoreDumpMetadataRecord::setCrc
    (
    CoreCrc regionType,
    UINT32  crcValue
    )

{
    m_CrcValues[regionType] = crcValue;
}

/******************************************************************************
DESCRIPTION:
Updates the core dump reboot reason in metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setRebootReason
    (
    UINT32 rebootReason
    )
{
    m_RebootReason = rebootReason;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the block size in metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setBlockSize
    (
    UINT32 blockSize
    )
{
    m_BlockSize = blockSize;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the compressed flag in the metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setCompressed
    (
    bool compressed
    )
{
    m_Compressed = compressed;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the Core Dump raw size in the metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setCoreDumpSizeInMem
    (
    UINT64 size
    )
{
    m_TotalSizeInMem = size;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Updates the Core Dump compressed size in the metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setCoreDumpSizeOnDisk
    (
    UINT64 size
    )
{
    m_TotalSizeOnDisk = size;
    writeToRpa();
}


/******************************************************************************
DESCRIPTION:
Updates the Cache Core Dump type in the metadata and writes it to RPA.
*/
void
dcd::RWCoreDumpMetadataRecord::setCcdType
    (
    CcdType type
    )
{
    m_CcdType = type;
    writeToRpa();
}

/******************************************************************************
DESCRIPTION:
Writes an error to RPA that occurs during core dump collection.
To avoid DCD channel and CRC specific issues, it is written directly using RPA and is not
protected by CRC.
 */
void
dcd::RWCoreDumpMetadataRecord::writeError
    (
    DumpError err
    )
{
    m_Error = err;
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //RPA_MEMSET_ULONG(getRpaCoreDumpPhyMemBase() + METADATA_ERROR_OFFSET,
                     //static_cast<ULONG>(m_Error));
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    RPA_MEMSET_ULONG(rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP)
                     + METADATA_ERROR_OFFSET, static_cast<ULONG>(m_Error));
//* EndGearsBlock Cpp Feature_RPARefactor
}

/******************************************************************************
DESCRIPTION:
Writes the metadata record to RPA memory.
 */
void
dcd::RWCoreDumpMetadataRecord::writeToRpa
    (
    )
{
    // The source parameter is knowingly cast to UINT32 not CADDR. Casting a pointer
    // (this) to a larger integral type (CADDR) is subject to platform-specific
    // interpretation. The implicit conversion of UINT32 to CADDR will zero the
    // upper bits.
    UINT32 crcValue = 0;

//* BeginGearsBlock Cpp !Feature_RPARefactor
    //dqvkiWrite(dcdIoDqWriter, DCD_IO_METADATA_WRITE_RPA, getRpaCoreDumpPhyMemBase(),
               //reinterpret_cast<UINT32>(this), CRC_PROTECTION_CUTOFF);
    //if (rpaCopyCoreSectionWithCrc(getRpaCoreDumpPhyMemBase(), reinterpret_cast<UINT32>(this),
        //CRC_PROTECTION_CUTOFF, 0, &crcValue))
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    dqvkiWrite(dcdIoDqWriter, DCD_IO_METADATA_WRITE_RPA, 
               rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP),
               reinterpret_cast<UINT32>(this), CRC_PROTECTION_CUTOFF);
    if (!rpa::DMAMgr::getInstance()->
            copyCoreSectionWithCrc(rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP),
                                   reinterpret_cast<UINT32>(this), CRC_PROTECTION_CUTOFF, 0,
                                   &crcValue))
//* EndGearsBlock Cpp Feature_RPARefactor
    {
        // Write copy error to RPA directly
        writeError(METADATA_RPA_COPY_FAIL);
        return;
    }
    setCrc(METADATA, crcValue);
    // Write Metadata CRC to RPA directly
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //RPA_MEMSET_ULONG(getRpaCoreDumpPhyMemBase() + METADATA_CRC_OFFSET, crcValue);
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    RPA_MEMSET_ULONG(rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP)
                     + METADATA_CRC_OFFSET, crcValue);
//* EndGearsBlock Cpp Feature_RPARefactor
}

/******************************************************************************
DESCRIPTION:
Fills the module information into the metadata object.
 */
UINT32
dcd::RWCoreDumpMetadataRecord::setModuleInfo
    (
    RWModuleInfo::ModuleInfoVector & moduleInfoVector
    )
{
    UINT32 numModules = 0;
    for (RWModuleInfo::ModuleInfoVectorCIter iter = moduleInfoVector.begin();
         iter != moduleInfoVector.end(); ++iter)
    {
        //Construct it with placement new, set the values in memory, and destroy it.
        RWModuleInfo* modulePtr = new(&m_ModuleList[numModules]) RWModuleInfo(
            (*iter)->getModuleId(),
            (*iter)->getTextAddr(),
            (*iter)->getDataAddr(),
            (*iter)->getBssAddr(),
            (*iter)->getTextSize(),
            (*iter)->getDataSize(),
            (*iter)->getBssSize(),
            (*iter)->getName());
        modulePtr->~RWModuleInfo();
        numModules++;
    }
    return numModules;
}

