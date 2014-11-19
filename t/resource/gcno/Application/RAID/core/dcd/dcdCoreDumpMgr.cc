/*******************************************************************************

NAME            dcdCoreDumpMgr.cc
VERSION         %version: 133 %
UPDATE DATE     %date_modified: Tue Nov  4 06:09:22 2014 %
PROGRAMMER      %created_by:    noopur %

        Copyright 2010-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION:
Implementation for the CoreDumpManager singleton.

*******************************************************************************/

#include "bcmArrayInfo.h"
#include "bcmBoardInfo.h"
#include "bcmHardware.h"
#include "bcmInquiry.h"
#include "bcmPlatformInfo.h"
#include "dbgMonLib.h"
#include "dcdCoreDumpMgr.h"
#include "dcdElf.h"
#include "dq.h"
#include "dqvki.h"
#include "ftdcrControl.h"
#include "moduleLib.h"          // VxWorks object module mgmt library
#include "psvLib.h"
#include "rpaDefs.h"
//* BeginGearsBlock Cpp Feature_RPARefactor
#include "rpaDMAMgr.h"
//* EndGearsBlock Cpp Feature_RPARefactor
#include "rpaLib.h"
#include "rpaiRegionMgr.h"
#include "scaprRegistrarMgmt.h"
#include "sxCallback.h"
#include "sxCoreDump.h"
#include "utlAltNmiDriver.h"
#include "utlFunctions.h"
#include "vkiWrap.h"
#include "vniWrap.h"
#include "utlCasts.h"

extern "C"
{
//* BeginGearsBlock Cpp HW_Processor_Pentium
extern void sysInit();
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
//extern void _sysInit();
//* EndGearsBlock Cpp HW_Processor_PPC

extern DqWriter* dcdDqWriter;
extern DqWriter* dcdIoDqWriter;
extern STATUS rpaCrcCompute(CADDR src, UINT32 count, UINT32 carryForwardCrc, UINT32 *crcValue);
static USHORT dcdCoreDumpMgrFstrBase;
static USHORT dcdIoCoreDumpMgrFstrBase;
bool expectResetHold = false;
static const UINT32 WAIT_TO_BE_HELD_IN_RESET_TIMEOUT = 2000;  

void dcdCoreDumpNmiHandler(altNmiMessage nmiMessage);
// Set the time, after expire of which CoreDump region can be overwritten
UINT32 dcdOverwriteThresholdTimeSec = dcd::CoreDumpManager::COREDUMP_OVERWRITE_TIME;

DQSize_t dcdDqDump(void *buf, size_t size, size_t count, FILE *fp);

// This variable can be used to force the controller to the debug monitor after
// executing a core dump.  It was added on request of mgmt to provide such a 
// way to compare debugability of systems via core dump vs. debug monitor.  
int dcdDebugOnCoreDump;

// This variable can be used to force the controller to the debug monitor when
// core dump execution fails.
int dcdDebugOnCoreDumpError;

// This variable forces full core dumps to be generated on both controllers
// when a core dump is triggered.  It is intended for test purposes only.
int dcdDualFullCoreDump = 0;

// This variable causes the dq store to the core dump blob to be skipped during
// core dump collection.
int dcdSkipDq;

// This variable causes dcd to corrupt the sharedRegionBase address in metadata
// during code dump capture. It is used to test that a corrupted/changed 
// shared region base address will be caught and the core dump will be discarded.
int dcdCorruptSharedRegionBaseInMetadata = 0;

// Setting this variable to 1 forces the size check to be exceeded during core dump capture.
// It adjusts the local size available for a core dump capture
int dcdForceCoreDumpSizeCheck = 0;

}

#ifdef CORE_DUMP_TEST
// Core Dump trigger delay test variable which can be set from shell to
//  induce a delay into the core dump trigger to allow testing of 
//  alt reset operation
UINT32 coreDumpTriggerDFTDelayStart = 0;
UINT32 coreDumpTriggerDFTDelayComplete = 0;
#endif
//==========================================================================================
// Debug Queue format strings
// DQ shall not be used during core dump trigger/collection but may be used otherwise.
//==========================================================================================
#define DCD_TRCQUE_INITIALIZE        (dcdCoreDumpMgrFstrBase + 0)
#define DCD_CORE_VALID_MISMATCH      (dcdCoreDumpMgrFstrBase + 1)
#define DCD_COREDUMP_CRC_INVALID     (dcdCoreDumpMgrFstrBase + 2)
#define DCD_MODULE_NO_MATCH          (dcdCoreDumpMgrFstrBase + 3)
#define DCD_MODULE_UNABLE_TO_OBTAIN  (dcdCoreDumpMgrFstrBase + 4)
#define DCD_MODULE_TOO_MANY          (dcdCoreDumpMgrFstrBase + 5)
#define DCD_MODULE_INCREASE_MAX      (dcdCoreDumpMgrFstrBase + 6)
#define DCD_MODULE_EXCEPTION         (dcdCoreDumpMgrFstrBase + 7)
#define DCD_SECONDARY_CACHE_INFO     (dcdCoreDumpMgrFstrBase + 8)
#define DCD_SECONDARY_CACHE_NULL     (dcdCoreDumpMgrFstrBase + 9)
#define DCD_REGISTER_LISTENER        (dcdCoreDumpMgrFstrBase + 10)
#define DCD_HANDLE_PI_CCD_TRIGGER    (dcdCoreDumpMgrFstrBase + 11)
#define DCD_HANDLE_NONIO_CCD_TRIGGER (dcdCoreDumpMgrFstrBase + 12)

#define DCD_IO_TRCQUE_COPY_CRC       (dcdIoCoreDumpMgrFstrBase + 0)

#define DCD_EXCEPTION_TASK_ID        (999999999)

/******************************************************************************
DESCRIPTION:
Initialization of Debug Queue format strings for DcdmManager.
*/
void
dcdCoreDumpMgrDq()
{
    const char* mgr = "dcd";
    const char* module = "mod";

    dcdCoreDumpMgrFstrBase = dqvkiNextFsn(dcdDqWriter);

    dqvkiAddFstr(dcdDqWriter, DCD_TRCQUE_INITIALIZE, FDL_NORMAL, mgr,
                 "%h CoreDumpMgr  initialize");
    dqvkiAddFstr(dcdDqWriter, DCD_CORE_VALID_MISMATCH, FDL_HI_PRI, mgr,
                 "%h CoreDumpMgr  Invaliding core dump, mismatch in expected crc or signature");
    dqvkiAddFstr(dcdDqWriter, DCD_COREDUMP_CRC_INVALID, FDL_HI_PRI, mgr,
                 "%h CoreDumpMgr  CRC mismatch in region:%d, expected:%u obtained:%u");
    dqvkiAddFstr(dcdDqWriter, DCD_MODULE_NO_MATCH, FDL_HI_PRI, module,
                 "%h ModuleInfo   No match for module");
    dqvkiAddFstr(dcdDqWriter, DCD_MODULE_UNABLE_TO_OBTAIN, FDL_HI_PRI, module,
                 "%h ModuleInfo   Unable to obtain module information");
    dqvkiAddFstr(dcdDqWriter, DCD_MODULE_TOO_MANY, FDL_HI_PRI, module,
                 "%h ModuleInfo   Too many modules to store");
    dqvkiAddFstr(dcdDqWriter, DCD_MODULE_INCREASE_MAX, FDL_HI_PRI, module,
                 "%h ModuleInfo   Increase max module info vector count");
    dqvkiAddFstr(dcdDqWriter, DCD_MODULE_EXCEPTION, FDL_HI_PRI, module,
                 "%h ModuleInfo   Encountered exception %s while storing module info");
    dqvkiAddFstr(dcdDqWriter, DCD_SECONDARY_CACHE_INFO, FDL_NORMAL, mgr,
                 "%h CoreDumpMgr  Shared Region base:" CADDR_DQ_FMT " size:" CADDR_DQ_FMT "");
    dqvkiAddFstr(dcdDqWriter, DCD_SECONDARY_CACHE_NULL, FDL_NORMAL, mgr,
                 "%h CoreDumpMgr  Shared region base address is Null. CRC check failed");
    dqvkiAddFstr(dcdDqWriter, DCD_REGISTER_LISTENER, FDL_NORMAL, mgr,
                 "%h CoreDumpMgr  Registered listener:0x%x");
    dqvkiAddFstr(dcdDqWriter, DCD_HANDLE_PI_CCD_TRIGGER, FDL_DETAIL, mgr,
                 "%h CoreDumpMgr  Handle PI trigger ssid:0x%x blockNo:0x%llx ioc:%d ioType:%d");
    dqvkiAddFstr(dcdDqWriter, DCD_HANDLE_NONIO_CCD_TRIGGER, FDL_DETAIL, mgr,
                 "%h CoreDumpMgr  Handle nonIO trigger");

    dcdIoCoreDumpMgrFstrBase = dqvkiNextFsn(dcdIoDqWriter);

    dqvkiAddFstr(dcdIoDqWriter, DCD_IO_TRCQUE_COPY_CRC, FDL_DETAIL, mgr,
                 "%h CDMgrIO      Copy with CRC - dst:0x%llx, src:0x%llx and count:%u");
}

dcd::CoreDumpManager::SegmentDescriptor dcd::CoreDumpManager::m_StackSegments[MAX_TASKS] = {};
VKI_TASK_ID dcd::CoreDumpManager::m_TaskIds[MAX_TASKS] = {};
//(0xCORE)
static char dcdScratchpad[dcd::DEV_SCRATCHPAD_SIZE] = "";
static const UINT32 CORE_XFER_CHUNK_SIZE = 8 MB;
//==========================================================================================
//==========================================================================================
// CoreDumpManager singleton class
//==========================================================================================
//==========================================================================================

/******************************************************************************
DESCRIPTION:
Constructor.  This instantiates CoreDumpManager objects.
*/
dcd::CoreDumpManager::CoreDumpManager
    (
    ): m_FullSegmentVector(0),
       m_AltSegmentVector(0),
       m_AltClientSegmentVector(0),
       m_ModuleInfoVector(0),
       m_Metadata(0),
       m_StorageRecord(0),
       m_DqWritePtr(0),
       m_DqWriteIndex(0),
       m_OverwriteThresholdTimeSec(0),
       m_LblTblUsedPtr(0),
       m_InitComplete(false),
       m_CoreDumpInProgress(false),
       m_NotesStagingBuffer(0),
       m_NotesOffset(0),
       m_SharedRegionBase(0),
       m_SharedRegionSize(0),
       m_DcdInfo(0),
       m_DcdPanicString(0),
       m_DcdRegSet(0),
       m_DcdRpaAccessPriority(NON_CAPTURE_PRIORITY),
       m_DcdDualFullCoreDump(false)
{
    VKI_MEMCLEAR(&m_DcdSavedEntryTaskRegs, sizeof(REG_SET));

    if (!ftdcr::Control::isCoreDumpEnabled())
        return;

    // Check if Dual Full CoreDump has been enabled.
    if (ftdcr::Control::isDualFullCoreDumpEnabled())
    {
        m_DcdDualFullCoreDump = true;
    }

    thisCtlCoreDumpHookAdd((FUNCPTR)dcdCoreDumpNmiHandler);

    // Set up the segment vectors.
    // The Lock Mgr needs this done for the alt segment vector prior to the execution
    // of its initialize().
    m_FullSegmentVector = new std::vector<SegmentDescriptor*>;
    m_AltSegmentVector = new std::vector<SegmentDescriptor*>;
    m_AltClientSegmentVector = new std::vector<SegmentDescriptor*>;
    // Ensure there is sufficient room in the vector to add task stacks on alt core dump.
    m_AltSegmentVector->reserve(MAX_TASKS);

    // Allocate our permanent copy of metadata.
    m_Metadata = new RWCoreDumpMetadataRecord;
    m_StorageRecord = new StorageRecord;

    // Load the Debug module, in order to ensure the debug utilities
    // are available for off-board core dump analysis.
    if (loadDebug(1) == FALSE)
    {
        VKI_CMN_ERR(CE_ERROR, "CoreDumpManager unable to load Debug module (errno 0x%x).",
                    VKI_ERRNO);
    }
}

/******************************************************************************
DESCRIPTION:
Destructor.  Assert since this should never happen.
*/
dcd::CoreDumpManager::~CoreDumpManager
    (
    )
{
    if (!ftdcr::Control::isCoreDumpEnabled())
        return;

    for (std::vector<SegmentDescriptor*>::iterator iter = m_FullSegmentVector->begin();
         iter != m_FullSegmentVector->end(); ++iter)
    {
        delete *iter;
    }
    delete m_FullSegmentVector;
    m_FullSegmentVector = 0;

    for (std::vector<SegmentDescriptor*>::iterator iter = m_AltSegmentVector->begin();
         iter != m_AltSegmentVector->end(); ++iter)
    {
        delete *iter;
    }
    delete m_AltSegmentVector;
    m_AltSegmentVector = 0;

    for (std::vector<SegmentDescriptor*>::iterator iter = m_AltClientSegmentVector->begin();
         iter != m_AltClientSegmentVector->end(); ++iter)
    {
        delete *iter;
    }
    delete m_AltClientSegmentVector;
    m_AltClientSegmentVector = 0;

    delete m_Metadata;
    m_Metadata = 0;
    delete m_StorageRecord;
    m_StorageRecord = 0;

    for (std::list<CoreDumpListener*>::iterator iter = m_Listeners.begin();
         iter != m_Listeners.end(); ++iter)
    {
        iter = m_Listeners.erase(iter);
    }

    assert(false);  //should never happen
}

/**
 * \param  moduleName, name of the module
 * 
 * Callback function to update loaded module information
 */
extern "C"
void 
dcdModuleInfo
    (
    void* moduleName // name of the module
    )
{
    MODULE_ID moduleId = moduleFindByName((char *)moduleName);
    if(moduleId == NULL)
    {
        dqvkiWrite(dcdDqWriter, DCD_MODULE_NO_MATCH);
        VKI_CMN_ERR(CE_ERROR, "dcdModuleInfo: No match for module %s",((char*)moduleName));
        return;
    }

    if (dcd::CoreDumpMgr::doesInstanceExist())
        dcd::CoreDumpMgr::getInstance()->moduleInfoStore((UINT32)moduleId);
    else
        VKI_CMN_ERR(CE_ERROR, "dcdModuleInfo: instance does not exist"); 
}


/*************************************************************************************
DESCRIPTION:
    Callback from platform code prior to reboot to allow core dump collection to be triggered. 
PARAMETERS:
    dumpInfo    - Core dump information. Contains dump type, panic string, file/line, reg set, etc.
    isRecursive - An indication whether this core dump is from the first instance of reboot in a
                  recursive reboot situation.

RETURNS: 
    Returns void 
************************************************************************************/
extern "C"
void 
dcdCoreDumpCallback(void* dumpInfo, int isRecursive, int rsv1, int rsv2)
{
    (void) rsv1;
    (void) rsv2;
    DCDTRIGGERINFO* dcdTriggerInfo = reinterpret_cast<DCDTRIGGERINFO *>(dumpInfo);

    // if fields for regSet, filename/lineNumber or panicString are not valid, set NULL
    REG_SET* regSet = 0;
    COREDUMPINFO* fwLocPtr = 0;
    const char* panicStr = "";

    if (dcdTriggerInfo->validFields & REGSET)
        regSet = &dcdTriggerInfo->regSet;
    if (dcdTriggerInfo->validFields & FILE_LINE)
        fwLocPtr = &dcdTriggerInfo->info;
    if (dcdTriggerInfo->validFields & PANICSTRING)
        panicStr = dcdTriggerInfo->panicString;

#ifdef CORE_DUMP_TEST
    // add delay for testing if DFT delay variable has been set.
    if( coreDumpTriggerDFTDelayStart )
    {
        watchdogSuspend();
        kprintf("DFT delay - start : CORE DUMP REQUESTED \n");  
        VKI_MSECWAIT(coreDumpTriggerDFTDelayStart);    
        kprintf("DFT delay - end   : CORE DUMP REQUESTED \n");  
        watchdogResume();
    }
#endif

    if(dcdTriggerInfo->altDump)
    {
        // notify alternate controller to capture lock core dump 
        utl::AltNmiMgr::getInstance()->altCtlSendNmi(ALT_NMI_LOCK_CORE_DUMP);
    }

    // notify alternate controller that I am capturing a core dump 
    utl::AltNmiMgr::getInstance()->altCtlSendNmi(ALT_NMI_CORE_DUMP_IN_PROGRESS);

    // full core dump requested 
    if(dcdTriggerInfo->fullDump)
    {
        if (isRecursive)
        {
            REG_SET curSet;
            CPU_SAVE_REG_SET(&curSet);
            VKI_SNPRINTF(dcdScratchpad, sizeof(dcdScratchpad),
                         "Attempting to dump core on recursive reboot, sp:0x%x pc:0x%x",
                         curSet.reg_sp, curSet.reg_pc);
        }

        // panicString valid if core dump triggered by panic or asset
        dcd::CoreDumpMgr::getInstance()->setPanicString(panicStr);

        // assert will provide file name and line number information 
        dcd::CoreDumpMgr::getInstance()->setFileAndLine(fwLocPtr);

        // REG_SET saved for exception context
        // Saved registers will be added as Notes section in core dump 
        dcd::CoreDumpMgr::getInstance()->setDcdRegSet(regSet);

        // If core dump trigger fails, add a small delay so we don't send the completion NMI 
        // too fast for the state machine to handle it.
        if (!dcd::CoreDumpMgr::getInstance()->triggerCoreDump(dcd::CoreDumpManagement::FULL_DUMP,
                                              bcmOperation.Reboot_Reason))
        {
            VKI_MSECWAIT(20);
        }

        // clear pointers in case reboot aborted so we don't carry stale data.
        dcd::CoreDumpMgr::getInstance()->setFileAndLine(0);
        dcd::CoreDumpMgr::getInstance()->setPanicString("");
        dcd::CoreDumpMgr::getInstance()->setDcdRegSet(0);

        VKI_MEMCLEAR(dcdScratchpad, sizeof(dcdScratchpad));

#ifdef CORE_DUMP_TEST
        // add delay for testing if DFT delay variable has been set.
        if( coreDumpTriggerDFTDelayComplete )
        {
            watchdogSuspend();
            kprintf("DFT delay - start : CORE DUMP COMPLETE \n");  
            VKI_MSECWAIT(coreDumpTriggerDFTDelayComplete);    
            kprintf("DFT delay - end   : CORE DUMP COMPLETE \n");  
            watchdogResume();
        }
#endif

        // notify alternate controller that I am finished 
        utl::AltNmiMgr::getInstance()->altCtlSendNmi(ALT_NMI_CORE_DUMP_COMPLETE);

        // if alt controller is pending 'altResetHold' we wait for him to reset us.
        // if we are not reset in a timely manner, we continue on and reset ourself.
        if( expectResetHold )
        {
            watchdogSuspend();
            VKI_MSECWAIT(WAIT_TO_BE_HELD_IN_RESET_TIMEOUT); 
            watchdogResume();
        }
    }

    if ( dcdDebugOnCoreDump )
    {
        // Unset the global to ensure we don't get "caught" in the debug
        // monitor on the attempt to exit or reboot from it.
        dcdDebugOnCoreDump = 0;

        // This entry to the debug monitor doesn't exactly reflect our 
        // intent here, but it does provide the needed functionality.
        DebugOnRequest("Debug requested after core dump");
    }
}

/*************************************************************************************
DESCRIPTION:
    Callback from the Alt NMI driver to receive notification of core dump NMIs issued
    by the alternate controller.
PARAMETERS:
    nmiMessage    - type of core dump NMI received.  Currently support the following:
                    ALT_NMI_FULL_CORE_DUMP - Perform a full core dump and reboot

RETURNS:
    Returns void
************************************************************************************/

extern "C"
void
dcdCoreDumpNmiHandler(altNmiMessage nmiMessage)
{
    switch(nmiMessage)
    {
    // altReset called with alt core dump bit set
    case ALT_NMI_FULL_CORE_DUMP:
    {
        // we have been requested to do a full core dump and reboot by our partner -
        // so we will request that he do a LOCK core dump...
        utl::AltNmiMgr::getInstance()->altCtlSendNmi(ALT_NMI_LOCK_CORE_DUMP);
        // honor the request only if the core dump manager is initialized
        // the alternate controller will reset us when the core dump request times out
        if(dcd::CoreDumpMgr::getInstance()->isInitComplete())
        {
            VKI_REBOOT(REBOOT_DCD_CORE_DUMP_ALT_NMI, 0);
        }
        break;
    }
    // altResetHold called with alt core dump bit set
    case ALT_NMI_CORE_DUMP_RESET_HOLD:
    {
        // we have been requested to do a full core dump and the alt will hold us in
        // reset after core dump is complete so we will request that he do a LOCK core dump...
        utl::AltNmiMgr::getInstance()->altCtlSendNmi(ALT_NMI_LOCK_CORE_DUMP);
        // honor the request only if the core dump manager is initialized
        // the alternate controller will reset us when the core dump request times out
        if(dcd::CoreDumpMgr::getInstance()->isInitComplete())
        {
            // alternate controller should hold us in reset after the core dump is complete
            expectResetHold=true;
            VKI_REBOOT(REBOOT_DCD_CORE_DUMP_ALT_NMI, 1);
        }
        break;
    }

    case ALT_NMI_LOCK_CORE_DUMP:
    {
        if (!dcd::CoreDumpMgr::getInstance()->getDcdDualFullCoreDump())
        {
            bool retStatus = dcd::CoreDumpMgr::getInstance()->triggerCoreDump(
                dcd::CoreDumpManagement::ALT_DUMP,
                REBOOT_DCD_LOCK_CORE_DUMP_ALT_NMI);
            if (!retStatus)
            {
                VKI_CMN_ERR(CE_WARN,
                    "dcdCoreDumpNmiHandler: ALT_NMI_LOCK_CORE_DUMP - triggerCoreDump failed.");
            }
        }
        break;
    }

    case ALT_NMI_CORE_DUMP_COMPLETE:     // mostly handled in dcdmDcdmMgr
    {
        // If we haven't got into debug by the time we get this signal from
        // the alternate, and the global is set to do so, drop into debug monitor.
        if (dcdDebugOnCoreDump)
        {
            DebugOnRequest("Alt Core Dump Complete");
        }

        break;
    }

    case ALT_NMI_CORE_DUMP_IN_PROGRESS:  // handled in dcdmDcdmMgr
    case ALT_NMI_NONE:
    case ALT_NMI_IOC_FAULT_LOCKDOWN:
    case ALT_NMI_RESET_WARNING:
    case ALT_NMI_DEBUG:
    default:
        break;
    }
}

/******************************************************************************
DESCRIPTION:
Called from SOD.
*/
void
dcd::CoreDumpManager::initialize
    (
    )
{
    dqvkiWrite(dcdDqWriter, DCD_TRCQUE_INITIALIZE);

    // Add all of CPU memory to the full segment vector
    addCoreDumpRegion((BCM_GET_PRIVATE_MEMORY_BASE()), (BCM_GET_PRIVATE_MEMORY_SIZE()),
                      FULL_DUMP, true);

    // Add kernel segment to the alt segment vector. This avoids overlapping the loaded module
    // segments.
//* BeginGearsBlock Cpp HW_Processor_Pentium
    unsigned int startKernelSpace = reinterpret_cast<unsigned int>(sysInit);
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
    //unsigned int startKernelSpace = reinterpret_cast<unsigned int>(_sysInit);
//* EndGearsBlock Cpp HW_Processor_PPC

    addCoreDumpRegion(reinterpret_cast<void*>(startKernelSpace),
                      KERNEL_SEGMENT_END - startKernelSpace,
                      ALT_DUMP, true);

    moduleInfoStoreInit();

    //register the moduleInfo call back from sxFlashFileLoad
    sxCallbackAdd(CB_MODULE_LOAD, reinterpret_cast<VOIDFUNCPTR>(dcdModuleInfo));

    //register the core dump trigger callback.
    sxCallbackAdd(CB_SX_TRIGGER_COREDUMP, (VOIDFUNCPTR)dcdCoreDumpCallback);

    //Register dcdShow with scapr for state capture.
    if (!scapr::CaptureRegistrarMgmt::getInstance()->
           registerCapture("DCD_SHOW", "dcdShow", scapr::CAPTURE_NORMAL))
    {
        VKI_CMN_ERR(CE_WARN, "dcd:Unable to register dcdShow with SCAPR");
        //don't return, continue.
    }

//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //m_SharedRegionBase = getRpaCoreDumpSharedPhyMemBase();
    //m_SharedRegionSize = getCoreDumpSharedMemorySize();
//* EndGearsBlock Cpp !Feature_RPARefactor

//* BeginGearsBlock Cpp Feature_RPARefactor
    m_SharedRegionBase = rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP_SHARED);
    m_SharedRegionSize = rpai::RegionMgr::getInstance()->getLength(RPA_CORE_DUMP_SHARED);
//* EndGearsBlock Cpp Feature_RPARefactor

//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    // Make a copy of RPA metadata to our local copy.
    RPA_MEMCPY(utl::Casts::castFromPtrToCADDR(m_Metadata), getCoreDumpBase(),
               sizeof(RWCoreDumpMetadataRecord));

    // Check for coredump integrity
    if (getCoreDumpValid())
    {
        bool ecdCrc = false; // flag to indicate the coredump crc for enhanced full core dump
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
        if (m_Metadata->getCoreDumpType() == FULL_DUMP && !m_Metadata->getNeedsBackup())
            ecdCrc = true;
//* EndGearsBlock Cpp Feature_EnhancedCoreDump
        if (!m_Metadata->checkCoreDumpSignature() || !m_Metadata->checkCoreDumpVersion()
            || (m_Metadata->getNeedsRetrieval() && !verifyCoreDumpCrc(ecdCrc)))
        {
            dqvkiWrite(dcdDqWriter, DCD_CORE_VALID_MISMATCH);
            setCoreDumpValid(false);
            // If there isn't a core dump, populate the board information so it can be peered.
            m_Metadata->populateBoardInfo(true);
        }
    }
    else
    {
        // If there isn't a core dump, populate the board information so it can be peered.
        m_Metadata->populateBoardInfo(true);
    }

    DumpMajorEvent eventStatus = m_Metadata->getCoreDumpEventStatus();
    // Update event flag based on check point
    if ((m_Metadata->getCheckPointStatus() != CAPTURE_COMPLETE) &&
        (eventStatus != EVENT_CLEAR && eventStatus != NOT_CAPTURED &&
         eventStatus != CAPTURE_FAILED))
    {
        m_Metadata->setEventStatus(CAPTURE_FAILED);
    }

    m_NotesStagingBuffer = reinterpret_cast<char*>(VKI_PMZALLOC(NOTES_STAGING_REGION_SIZE, VKI_KMNOSLEEP));
    if (!m_NotesStagingBuffer)
    {
        VKI_CMN_ERR(CE_PANIC, "Unable to allocate m_NotesStagingBuffer\n");
        return;
    }

    UINT lblTblUsedSize = (LBLTBLNUMBER_MAX+1) * sizeof(int);
    m_LblTblUsedPtr  = reinterpret_cast<int*>(VKI_PMZALLOC(lblTblUsedSize, VKI_KMNOSLEEP));
    if (!m_LblTblUsedPtr)
    {
        VKI_CMN_ERR(CE_PANIC, "%s: Unable to allocate m_LblTblUsedPtr\n", __FUNCTION__);
        return;
    }
    setOverwriteThresholdTime(dcdOverwriteThresholdTimeSec);

// Make a copy of the RPA StorageRecord to our local copy, if not valid or current, clear the record
//* BeginGearsBlock Cpp Feature_CacheCoreDump
    RPA_MEMCPY(reinterpret_cast<UINT32>(m_StorageRecord),
               getCoreDumpBase() + sizeof(RWCoreDumpMetadataRecord), sizeof(StorageRecord));

    if (!m_StorageRecord->isValid() || !m_StorageRecord->checkSignature()
        || !m_StorageRecord->checkVersion())
    {
        m_StorageRecord->clearStorageRecord();
    }
//* EndGearsBlock Cpp Feature_CacheCoreDump
    // set flag indicating that it is safe to capture a core dump
    m_InitComplete=true;
}

/******************************************************************************
* DESCRIPTION:
*    Returns the core dump base address
*/
CADDR
dcd::CoreDumpManager::getCoreDumpBase
    (
    ) const
{
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //return getRpaCoreDumpPhyMemBase();
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    return rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP);
//* EndGearsBlock Cpp Feature_RPARefactor
}

/******************************************************************************
* DESCRIPTION:
*    Returns the core dump end address
*/
CADDR
dcd::CoreDumpManager::getCoreDumpEnd
    (
    ) const
{
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //return getRpaCoreDumpPhyMemBase() + getCoreDumpMemorySize();
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    return rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_CORE_DUMP) +
           rpai::RegionMgr::getInstance()->getLength(RPA_CORE_DUMP);
//* EndGearsBlock Cpp Feature_RPARefactor
}

/******************************************************************************
* DESCRIPTION:
*    Returns the shared region base address
*/
CADDR
dcd::CoreDumpManager::getRegionAddr
    (
    ) const
{
    CADDR memBase = getCoreDumpBase() + getFirstSectionOffset();
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    if (m_Metadata->getEcd() && m_Metadata->getCoreDumpType() == FULL_DUMP)
    {
        //* if shared region base is not initialized, return failure
        if (!m_Metadata->getSharedRegionBase())
        {
            dqvkiWrite(dcdDqWriter, DCD_SECONDARY_CACHE_NULL);
            return 0;
        }

        memBase = m_Metadata->getSharedRegionBase();
    }
//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    return memBase;
}

/******************************************************************************
* DESCRIPTION:
*    find a given section header in RPA
*
* RETURNS:
*       true: On success
*       false: Otherwise
*/
bool
dcd::CoreDumpManager::findSection
    (
    CoreSectionType sectionType,
    SectionMetadataRecord* returnSection,
    CADDR& offset
    ) const
{
    if (!returnSection)
        return false;

    // Find the range within which we'll constrain our search
    CADDR baseAddr = getRegionAddr();
    CADDR memSize = m_Metadata->getCoreDumpSizeInMem();
    CADDR endAddr = baseAddr + memSize;

    dcd::CoreSectionEntry* entry = dcd::getStaticEntry(sectionType);
    if (!entry || !baseAddr || !memSize || !endAddr)
        return false;

    CADDR seekAddr = baseAddr;
    bool found = false;
    // Save the current page
    int intLevel = VKI_INT_DISABLE();
    INT32 saveRpaPage = RPA_GET_PAGE_NUMBER();
    while (seekAddr < endAddr)
    {
        RPA_SET_PAGE_NUMBER(RPA_GET_PAGENO(seekAddr));
        UINT32 mappedAddr = RPA_MAPPED_ADDR(seekAddr);

        SectionMetadataRecord* rec = reinterpret_cast<SectionMetadataRecord*>(mappedAddr);

        // If we find an invalid section bail out
        if (!rec->isValid())
            break;

        // Found the next section that matches the type
        CADDR recSize = rec->getSize();
        if (rec->doesSignatureMatch(entry->label))
        {
            CADDR tempAddr = seekAddr + sizeof(SectionMetadataRecord);
            if (tempAddr > endAddr)
                break;

            found = true;
            VKI_MEMCPY(returnSection, rec, sizeof(SectionMetadataRecord));
            offset = tempAddr - baseAddr;
            break;
        }

        seekAddr += sizeof(SectionMetadataRecord) + recSize;
    }

    RPA_SET_PAGE_NUMBER(saveRpaPage);
    VKI_INT_RESTORE(intLevel);

    // A section header was invalid or we went off the end and didn't find it
    if (!found)
    {
        VKI_CMN_ERR(CE_ERROR, "dcd: Error traversing sections to find section %d", sectionType);
        return false;
    }

    return true;
}

/******************************************************************************
* DESCRIPTION:
*    Sets the offset and size for the given section type in RPA
*
* RETURNS:
*       true: On success
*       false: Otherwise
*/
bool
dcd::CoreDumpManager::getSectionAttributes
    (
    CoreSectionType sectionType,
    CADDR& offset,
    UINT64& size
    ) const
{
    SectionMetadataRecord rec(sectionType, 0, dcd::NO_ID);
    if (!findSection(sectionType, &rec, offset))
        return false;

    size = rec.getSize();
    return true;
}

/******************************************************************************
* DESCRIPTION:
*    Copy RPA with Crc
*
* RETURNS:
*       true: On success
*       false: Otherwise
*/
bool
dcd::CoreDumpManager::copyWithCrc
    (
    CADDR   dst,
    CADDR   src,
    UINT32  count,
    bool    carryForwardCrc,
    UINT32* crcValue
    )
{
    dqvkiWrite(dcdIoDqWriter, DCD_IO_TRCQUE_COPY_CRC, dst, src, count);
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //STATUS retStatus = rpaCopyCoreSectionWithCrc(dst, src, count, carryForwardCrc, crcValue);
    //return (retStatus == OK)? true: false;
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    return rpa::DMAMgr::getInstance()->copyCoreSectionWithCrc(dst, src, count, carryForwardCrc,
                                                              crcValue);
//* EndGearsBlock Cpp Feature_RPARefactor
}

/******************************************************************************
* DESCRIPTION:
*    Computes the CRC
*
* RETURNS:
*       true: On success
*       false: Otherwise
*/
bool
dcd::CoreDumpManager::computeCrc
    (
    CADDR src,
    UINT32 count,
    bool carryForwardCrc,
    UINT32* crcValue
    ) const
{
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //if (rpaCrcCompute(src, count, carryForwardCrc? 1: 0, crcValue))
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    if (!rpa::DMAMgr::getInstance()->crcCompute(src, count, carryForwardCrc? 1: 0, crcValue))
//* EndGearsBlock Cpp Feature_RPARefactor
        return false;

    return true;
}

/******************************************************************************
* DESCRIPTION:
*    Computes the CRC's and verifies with Metadata CRC informations
*
* RETURNS:
*       true: If CRC value matchs with CRC value in Metadata for all regions
*       false:If any of the CRC value of the region does not match with CRC value in metadata
*/
bool
dcd::CoreDumpManager::verifyCoreDumpCrc
    (
    bool ecdCrc
    )
{
    UINT32 crcValue = 0;
    // Compute CRC of Metadata region
    if (!computeCrc(getCoreDumpBase(), RWCoreDumpMetadataRecord::METADATA_CRC_OFFSET, false,
                    &crcValue))
    {
        VKI_CMN_ERR(CE_WARN, "Error in rpa service during CRC computation of Metadata region");
        return true;
    }

    if (m_Metadata->getCrcValue(METADATA) != crcValue)
    {
        dqvkiWrite(dcdDqWriter, DCD_COREDUMP_CRC_INVALID, METADATA,
                   m_Metadata->getCrcValue(METADATA), crcValue);
        return false;
    }

    // Calculate CRC for ELF/DQ only when it has not been persisted to the backup device
    if (!ecdCrc)
    {
        CADDR elfOffset;
        UINT64 elfLength;
        if (!getSectionAttributes(CPU_ELF, elfOffset, elfLength))
        {
            VKI_CMN_ERR(CE_WARN, "Error in getSectionAttributes for elf");
            return false;
        }

        CADDR elfBase = getRegionAddr() + elfOffset;

        // Compute CRC of ELF region
        crcValue = 0;
        if (!computeCrc(elfBase, static_cast<UINT32>(elfLength), false, &crcValue))
        {
            VKI_CMN_ERR(CE_WARN, "Error in rpa service during CRC computation of ELF region");
            return true;
        }

        if (m_Metadata->getCrcValue(ELF_CORE) != crcValue)
        {
            dqvkiWrite(dcdDqWriter, DCD_COREDUMP_CRC_INVALID, ELF_CORE,
                       m_Metadata->getCrcValue(ELF_CORE), crcValue);
            return false;
        }

        CADDR dqOffset;
        UINT64 dqLength;
        if (!getSectionAttributes(DQ_TRACE, dqOffset, dqLength))
        {
            VKI_CMN_ERR(CE_WARN, "Error in getSectionAttributes for dq");
            return false;
        }

        CADDR dqBase = getRegionAddr() + dqOffset;

        // Compute CRC of DQ region
        crcValue = 0;
        if (!computeCrc(dqBase, static_cast<UINT32>(dqLength), false, &crcValue))
        {
            VKI_CMN_ERR(CE_WARN, "Error in rpa service during CRC computation of DQ region");
            return true;
        }

        if (m_Metadata->getCrcValue(DQ) != crcValue)
        {
            dqvkiWrite(dcdDqWriter, DCD_COREDUMP_CRC_INVALID, DQ,
                       m_Metadata->getCrcValue(DQ), crcValue);
            return false;
        }
    }

    return true;
}

/*******************************************************************************
* DESCRIPTION: 
* Returns start address of coredumpDq region 
*/
CADDR
dcd::CoreDumpManager::getCoreDumpDqAddress
    (
    ) const
{
    CADDR offset;
    UINT64 size;
    if (!getSectionAttributes(DQ_TRACE, offset, size))
    {
        VKI_CMN_ERR(CE_WARN, "Error in getSectionAttributes for dq address");
        return 0;
    }

//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    if (m_Metadata->getCoreDumpType() == FULL_DUMP)
        return (getSharedRegionBase() + offset);
//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    return (getCoreDumpBase() + offset);
}

/*******************************************************************************
* DESCRIPTION:
* Returns length of coredumpDq region
*/
UINT64
dcd::CoreDumpManager::getDqLength
    (
    ) const
{
    CADDR offset;
    UINT64 size;
    if (!getSectionAttributes(DQ_TRACE, offset, size))
    {
        VKI_CMN_ERR(CE_WARN, "Error in getSectionAttributes for dq length");
        return 0;
    }

    return size;
}

/**
 * \return true, if success
 *         false, otherwise
 *
 * This function provides access to the core dump RPA region based on priority passed
 * It makes sure that non-capturing access will not hold the RPA engine for long
 */
bool
dcd::CoreDumpManager::dcdRpaCopyCoreSectionWithCrc
    (
    CADDR   dst,
    CADDR   src,
    UINT32  count,
    bool    carryForwardCrc,
    UINT32* crcValue,
    DcdRpaAccessPriority priority
    )
{
    // Already being accessed by a higher priority copy
    if (m_DcdRpaAccessPriority < priority)
        return false;

    // Not a critical access; limit the size to be transferred in case we need to be interrupted.
    if (priority != CAPTURE_PRIORITY)
    {
        // Current setting accounts for RDM's buffer size
        if (count > NON_DCD_RPA_ACCESS_SIZE)
            return false;
    }

    return copyWithCrc(dst, src, count, carryForwardCrc, crcValue);
}

/*******************************************************************************
* DESCRIPTION: writes traceDq data in coredump RPA region.
*/
void
dcd::CoreDumpManager::writeDqBuffer
    (
    UINT32  srcBuf,
    UINT32  bytesToWrite
    )
{
    UINT32 crcValue;
    if (!copyWithCrc(m_DqWritePtr, srcBuf, bytesToWrite, true, &crcValue))
    {
        writeError(DQ_RPA_COPY_FAIL);
        return;
    }

    m_DqWritePtr   += bytesToWrite;
    m_DqWriteIndex += bytesToWrite;
}

/*******************************************************************************
* DESCRIPTION: Writes error to metadata. Called during collection; avoids DCD
* channel and CRC computation.
*/
void
dcd::CoreDumpManager::writeError
    (
    DumpError err
    )
{
    m_Metadata->writeError(err);

    if ( (dcdDebugOnCoreDumpError & DEBUG_ON_DETECTED_ERROR) && err != NO_ERROR )
    {
        // Unset the global to ensure we don't get "caught" in the debug
        // monitor on the attempt to exit or reboot from it.
        dcdDebugOnCoreDumpError = 0;

        // This entry to the debug monitor doesn't exactly reflect our 
        // intent here, but it does provide the needed functionality.
        DebugOnRequest("Debug requested on core dump error");
    }
}

/*******************************************************************************
* DESCRIPTION:
*       copies the Dq body specified by the address & size to the coreDump 
* RETURNS:
*       1 on success and 0 on failure
*/
UINT32
dcd::CoreDumpManager::copyTraceDqBody
    (
    UINT32 dqSize,
    CADDR  dqAddr
    )
{
    // copy DQ body 
    m_Metadata->setCheckpoint(STORING_DQ_BODY);
    if ( !dataBlockFwriteStart(dcdDqDump, reinterpret_cast<FILE*>(0), ENDIAN, dqSize) )
    {
        writeError(DQ_BODY_START_STORE_FAIL);
        return 0;
    }

    if (dqSize > getDqRegionFreeSize())
    {
        writeError(DQ_INSUF_FREE_SIZE);
        return 0;
    }

    m_Metadata->setCheckpoint(WRITING_DQ_BODY);
    UINT32 crcValue;
    if (!copyWithCrc(m_DqWritePtr, dqAddr, dqSize, true, &crcValue))
    {
        writeError(DQ_BODY_STORE_FAIL);
        return 0;
    }

    m_DqWritePtr   += dqSize;
    m_DqWriteIndex += dqSize;

    return 1;
}

/*******************************************************************************
* DESCRIPTION:
*       copy the existing traceDq to the coreDump 
* RETURNS:
*       dq region size on success and  0 on failure
*/
UINT32
dcd::CoreDumpManager::copyTraceDq
    (
    CADDR dqOffset,
    CADDR coreDumpEndAddr
    )
{
    Dq *dq = dqFind("trace");
    if(!dq)
    {
        writeError(DQ_NO_TRACE);
        return 0;
    }
    m_DqWriteIndex = 0;
    m_DqWritePtr = getCoreDumpBase() + dqOffset;

//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    if (m_Metadata->getCoreDumpType() == FULL_DUMP)
        m_DqWritePtr = m_SharedRegionBase + dqOffset;
//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    m_Metadata->setCheckpoint(STORING_DQ_LBLTBL);
    // copy LblTbl block
    if (!dqStoreMe_LblTblBlock1(dq, dcdDqDump, m_LblTblUsedPtr, reinterpret_cast<FILE*>(0)))
    {
        writeError(DQ_LBL_STORE_FAIL);
        return 0;
    }

    m_Metadata->setCheckpoint(STORING_DQ_PRINTPARAMS);
    // copy print parms
    if (!dqStoreMe_PrintParmsBlock2(dq, dcdDqDump, reinterpret_cast<FILE*>(0)))
    {
        writeError(DQ_PRPM_STORE_FAIL);
        return 0;
    }

    // Copy the count of the Dq bodies that will be appended
    UINT32 dqBodyCount = 1;

    if (dq->levelOneDq)
        dqBodyCount++;

    if (!dataBlockFwrite(dcdDqDump, reinterpret_cast<FILE*>(0), ENDIAN, &dqBodyCount,
                         sizeof(UINT32)))
    {
        writeError(DQ_BODY_START_STORE_FAIL);
        return 0;
    }

    UINT32 dqSize = dqGetHeaderAndWriterSize(dq->writercnt) + dq->size;

    // Copy High Priority DQ body if present
    if (dq->levelOneDq)
    {
        UINT32 dqHpSize = dqGetHeaderAndWriterSize(dq->levelOneDq->writercnt) +
                          dq->levelOneDq->size;
        CADDR  dqHpAddr = rpaGetRegionPhysicalBase(RPA_DQ) + dqSize;
        if (m_DqWritePtr + dqHpSize > coreDumpEndAddr)
        {
            writeError(DQ_INSUF_FREE_SIZE);
            return 0;
        }
        if (!copyTraceDqBody(dqHpSize, dqHpAddr))
            return 0;
    }
    // Copy trace DQ body
    if (m_DqWritePtr + dqSize > coreDumpEndAddr)
    {
        writeError(DQ_INSUF_FREE_SIZE);
        return 0;
    }
    CADDR  dqAddr = rpaGetRegionPhysicalBase(RPA_DQ);
    if (!copyTraceDqBody(dqSize, dqAddr))
        return 0;

    m_Metadata->setCheckpoint(WRITING_DQ_FORMATSTRINGS);

    // Copy format string block
    int i;
    DqWriter* dqw;
    for (i = 0, dqw = dq->writers; i < dq->writercnt; i++, dqw++)
    {
        if (!dqStoreMe_WriterBlock4(dq, dqw, dcdDqDump, reinterpret_cast<FILE*>(0)))
        {
            writeError(DQ_FS_STORE_FAIL);
            return 0;
        }
    }

    if (!dqStoreTerminator(dcdDqDump, reinterpret_cast<FILE*>(0)))
    {
        writeError(DQ_FS_STORE_FAIL);
        return 0;
    }

    return m_DqWriteIndex;
}

/*******************************************************************************
* DESCRIPTION:
*     Saves the file/lineNumber information passed to dcdCoreDumpCallback function 
*       from asserts.
* RETURNS:
*     None
*/
void 
dcd::CoreDumpManager::setFileAndLine( COREDUMPINFO* info )
{
    m_DcdInfo = info;
}

/*******************************************************************************
* DESCRIPTION:
*     Saves the panic string passed to dcdCoreDumpCallback function from panic.
*
* RETURNS:
*     None
*/
void 
dcd::CoreDumpManager::setPanicString(const char* panicString)
{
    m_DcdPanicString=panicString;
}

/*******************************************************************************
* DESCRIPTION:
*     Saves the exception REG_SET context registers
*
* RETURNS:
*     None
*/
void
dcd::CoreDumpManager::setDcdRegSet(REG_SET* regSet)
{
    m_DcdRegSet = regSet;
}

/*******************************************************************************
* RETURNS:
*       ERROR: If Invalid Memory Pointer or in other error conditions.
*       Otherwise returns the number of bytes 
*/
DQSize_t
dcdDqDump
    (
    void *vbuf,      // Buffer to be dumped
    size_t size,     // Size of the buffer
    size_t count,    // Number of buffers
    FILE  *fp
    )
{
    UINT32 srcBuffer    = reinterpret_cast<UINT32>(vbuf);
    UINT32 bytesToWrite = static_cast<UINT32>(size * count);
    dcd::CoreDumpManager* mgr = dcd::CoreDumpMgr::getInstance();
    if (!srcBuffer && bytesToWrite)
    {
        mgr->writeError(dcd::DQ_NO_SRC_BUFFER);
        return 0;
    }
    if (bytesToWrite > mgr->getDqRegionFreeSize())
    {
        mgr->writeError(dcd::DQ_INSUF_FREE_SIZE);
        return 0;
    }

    mgr->writeDqBuffer(srcBuffer, bytesToWrite);
    return static_cast<DQSize_t>(bytesToWrite);
}

/******************************************************************************
DESCRIPTION:
Triggers the core dump collection.
Returns true if success, false if failure
*/
bool
dcd::CoreDumpManager::triggerCoreDump
    (
    dcd::CoreDumpMgmt::DumpType dumpType,
    int rebootReason
    )
{
    if (!ftdcr::Control::isCoreDumpEnabled())
        return false;

    bool retStatus = false;

    switch(dumpType)
    {
    case FULL_DUMP:
    case ALT_DUMP:
    {
        // Initialize has not completed, so core dump is not available yet.
        if(!m_InitComplete)
        {
            VKI_CMN_ERR(CE_WARN, "Core dump trigger before core dump manager initialized : dump type %d", dumpType);
            return false;
        }
        // We're already collecting a core dump on this controller.
        if (m_CoreDumpInProgress)
        {
            return false;
        }
        m_CoreDumpInProgress = true;

        if (!overwriteCoredump(rebootReason))
        {
            break;
        }

        m_DcdRpaAccessPriority = CAPTURE_PRIORITY;
        retStatus = captureCoreDump(dumpType, rebootReason);
        if (!retStatus)
        {
            m_Metadata->setEventStatus(CAPTURE_FAILED);
        }
        break;
    }
    default:
        VKI_CMN_ERR(CE_ERROR, "Core dump type not handled in trigger:%d", dumpType);
        break;
    }

    m_CoreDumpInProgress = false;
    return retStatus;
}

/******************************************************************************
DESCRIPTION:
    Adds coredump Region to end of the list.
*/
void
dcd::CoreDumpManager::addCoreDumpRegion
    (
    void*                       address,
    UINT32                      size,
    dcd::CoreDumpMgmt::DumpType dumpType
    )
{
    addCoreDumpRegion(address, size, dumpType, false);
}

/******************************************************************************
DESCRIPTION:
    Adds coredump Region, with an argument to add it to the beginning of the 
    list.  This is mostly to ensure the kernel region is first in the elf file, 
    as is necessary so that the alternate core dump runs properly under 
    off-board emulation.
*/
void
dcd::CoreDumpManager::addCoreDumpRegion
    (
    void*                       address,
    UINT32                      size,
    dcd::CoreDumpMgmt::DumpType dumpType,
    bool                        addToHead
    )
{
    if (!ftdcr::Control::isCoreDumpEnabled())
        return;

    if ((address == 0) || (size == 0))
    {
        VKI_CMN_ERR(CE_WARN, "addCoreDumpRegion invalid input parameter address:0x%0"
                    PRIxPTR " size:%d", PTR_FMT_CAST(address), size);
        return;
    }

    SegmentDescriptor* ldSeg = 0;
    try
    {
        // Note that we only register addresses in the first 32-bits.
        // We support 64-bit addresses in the segment descriptor (i.e. data from RPA) only for CCD.
        // We can't just change the reinterpret cast to 64-bits as it may leave the upper 32-bits
        // garbage from the void pointer.
        switch(dumpType)
        {
        case FULL_DUMP:
        {
            ldSeg = new SegmentDescriptor(reinterpret_cast<UINT32>(address), size);
            m_FullSegmentVector->push_back(ldSeg);
            break;
        }
        case ALT_DUMP:
        {
            ldSeg = new SegmentDescriptor(reinterpret_cast<UINT32>(address), size);
            if (addToHead)
            {
                m_AltClientSegmentVector->insert(m_AltClientSegmentVector->begin(), ldSeg);
            }
            else
            {
                m_AltClientSegmentVector->push_back(ldSeg);
            }

            // Make sure there's enough room for the task stacks in the alt core dump. 
            m_AltSegmentVector->reserve(MAX_TASKS + m_AltClientSegmentVector->size() * 2);
            break;
        }
        default:
            VKI_CMN_ERR(CE_WARN, "addCoreDumpRegion dumpType:%d is not handled", dumpType);
            break;
        }
    }
    catch(std::exception& e)
    {
        delete ldSeg;
        ldSeg = 0;
        VKI_CMN_ERR(CE_ERROR, "Caught %s in CoreDumpManager::addCoreDumpRegion ", e.what());
    }
}

/*************************************************************************************
DESCRIPTION:
    Create ELF Header
PARAMETER:
    eheader:     O/P parameter. ELF header pointer
    numSegs:     I/P parameter. Number of Segments
RETURNS:
    Returns void
************************************************************************************/
void
dcd::CoreDumpManager::createElfHeader32
    (
    Elf32_Ehdr* eheader,
    UINT        numSegs
    )
{
    eheader->e_ident[EI_MAG0]       = ELFMAG0;
    eheader->e_ident[EI_MAG1]       = ELFMAG1;
    eheader->e_ident[EI_MAG2]       = ELFMAG2;
    eheader->e_ident[EI_MAG3]       = ELFMAG3;
    eheader->e_ident[EI_CLASS]      = ELFCLASS32;
    eheader->e_ident[EI_DATA]       = ELFDATA2LSB;
    eheader->e_ident[EI_VERSION]    = EV_CURRENT;
    eheader->e_ident[EI_OSABI]      = ELFOSABI_FREEBSD;
    eheader->e_ident[EI_ABIVERSION] = EI_DEFAULT;
    
    for(UINT32 i = EI_PAD;i < EI_NIDENT;i++)
       eheader->e_ident[i] = EI_DEFAULT; 
    
    eheader->e_type      = ET_CORE;
//* BeginGearsBlock Cpp HW_Processor_Pentium
    eheader->e_machine   = EM_386;
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
    //eheader->e_machine   = EM_PPC;
//* EndGearsBlock Cpp HW_Processor_PPC
    eheader->e_version   = EV_CURRENT;
    eheader->e_entry     = reinterpret_cast<Elf32_Addr>(DebugCoreDumpEntry);
    eheader->e_phoff     = sizeof(Elf32_Ehdr);
    eheader->e_shoff     = ES_NOSECTION;
    eheader->e_flags     = EF_NOFLAGS;
    eheader->e_ehsize    = sizeof(Elf32_Ehdr);
    eheader->e_phentsize = sizeof(Elf32_Phdr);
    eheader->e_phnum     = numSegs;
    eheader->e_shentsize = ES_NOSECTION;
    eheader->e_shnum     = ES_NOSECTION;
    eheader->e_shstrndx  = SHN_UNDEF;
}

/*************************************************************************************
DESCRIPTION:
    Create ELF Header
PARAMETER:
    eheader:     O/P parameter. ELF header pointer
    numSegs:     I/P parameter. Number of Segments
RETURNS:
    Returns void
************************************************************************************/
void
dcd::CoreDumpManager::createElfHeader64
    (
    Elf64_Ehdr* eheader,
    UINT        numSegs
    )
{
    eheader->e_ident[EI_MAG0]       = ELFMAG0;
    eheader->e_ident[EI_MAG1]       = ELFMAG1;
    eheader->e_ident[EI_MAG2]       = ELFMAG2;
    eheader->e_ident[EI_MAG3]       = ELFMAG3;
    eheader->e_ident[EI_CLASS]      = ELFCLASS64;
    eheader->e_ident[EI_DATA]       = ELFDATA2LSB;
    eheader->e_ident[EI_VERSION]    = EV_CURRENT;
    eheader->e_ident[EI_OSABI]      = ELFOSABI_FREEBSD;
    eheader->e_ident[EI_ABIVERSION] = EI_DEFAULT;

    for(UINT32 i = EI_PAD;i < EI_NIDENT;i++)
       eheader->e_ident[i] = EI_DEFAULT;

    eheader->e_type      = ET_CORE;
//* BeginGearsBlock Cpp HW_Processor_Pentium
    eheader->e_machine   = EM_386;
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
    //eheader->e_machine   = EM_PPC;
//* EndGearsBlock Cpp HW_Processor_PPC
    eheader->e_version   = EV_CURRENT;
    eheader->e_entry     = reinterpret_cast<Elf32_Addr>(DebugCoreDumpEntry);
    eheader->e_phoff     = sizeof(Elf64_Ehdr);
    eheader->e_shoff     = ES_NOSECTION;
    eheader->e_flags     = EF_NOFLAGS;
    eheader->e_ehsize    = sizeof(Elf64_Ehdr);
    eheader->e_phentsize = sizeof(Elf64_Phdr);
    eheader->e_phnum     = numSegs;
    eheader->e_shentsize = ES_NOSECTION;
    eheader->e_shnum     = ES_NOSECTION;
    eheader->e_shstrndx  = SHN_UNDEF;
}

/*************************************************************************************
DESCRIPTION:
    Create program header for LOAD section.
PARAMETER:
    pHeader:     O/P parameter. program header pointer
    seg:         I/P parameter. SegmentDescriptor pointer 
    startOffset: I/P parameter. starting offset
RETURNS: 
    Returns true on success.
************************************************************************************/
bool
dcd::CoreDumpManager::createLoadProgramHeader32
    (
    Elf32_Phdr*        pHeader,
    SegmentDescriptor* seg,
    UINT32             startOffset
    )
{
    if (!pHeader || !seg)
        return false;

    pHeader->p_type   = PT_LOAD;
    pHeader->p_offset = startOffset;
    pHeader->p_vaddr  = static_cast<UINT32>(seg->getAddr());
    pHeader->p_paddr  = static_cast<UINT32>(seg->getAddr());
    pHeader->p_filesz = static_cast<UINT32>(seg->getSize());
    pHeader->p_memsz  = static_cast<UINT32>(seg->getSize());
    pHeader->p_flags  = PF_X | PF_W |PF_R;
    pHeader->p_align  = sizeof(UINT32);
    return true;
}

/*************************************************************************************
DESCRIPTION:
    Create program header for LOAD section.
PARAMETER:
    pHeader:     O/P parameter. program header pointer
    seg:         I/P parameter. SegmentDescriptor pointer 
    startOffset: I/P parameter. starting offset
RETURNS: 
    Returns true on success.
************************************************************************************/
bool
dcd::CoreDumpManager::createLoadProgramHeader64
    (
    Elf64_Phdr*        pHeader,
    SegmentDescriptor* seg,
    UINT64             startOffset
    )
{
    if (!pHeader || !seg)
        return false;

    pHeader->p_type   = PT_LOAD;
    pHeader->p_offset = startOffset;
    pHeader->p_vaddr  = seg->getAddr();
    pHeader->p_paddr  = seg->getAddr();
    pHeader->p_filesz = seg->getSize();
    pHeader->p_memsz  = seg->getSize();
    pHeader->p_flags  = PF_X | PF_W |PF_R;
    pHeader->p_align  = sizeof(UINT64);
    return true;
}

/******************************************************************************
DESCRIPTION:
This method add segment entries from client segment vector to alternate segment
vector and remove stale entries from alternate segment vector.

RETURNS:
    Returns true on success.
************************************************************************************/
bool 
dcd::CoreDumpManager::updateAltSegmentVector
    (
    )
{
    try
    {
        // First add entries from m_AltClientSegmentVector to m_AltSegmentVector
        // and then remove stale entries otherwise capacity of m_AltSegmentVector become zero. 
        m_AltSegmentVector->insert(m_AltSegmentVector->begin(), m_AltClientSegmentVector->begin(),
                                   m_AltClientSegmentVector->end());

        UINT clientSegVecSize = m_AltClientSegmentVector->size();
        if (m_AltSegmentVector->size() != clientSegVecSize)
        {
            // Remove stale (TCB/stack segments) entries from alternate segment vector
            m_AltSegmentVector->erase(m_AltSegmentVector->begin() + clientSegVecSize,
                                      m_AltSegmentVector->end());
        }
    }
    catch(std::exception& e)
    {
        VKI_CMN_ERR(CE_ERROR, "Caught %s in CoreDumpManager::updateAltSegmentVector", e.what());
        return false;
    }

    return true;
}

/******************************************************************************
DESCRIPTION:
Obtain stack segments from TCBs for an alt core dump, saving them into a local array.

RETURNS: 
    Returns int - number of tasks found/segments generated. 
************************************************************************************/
int
dcd::CoreDumpManager::getStackSegments
    (
    )
{
    static WIND_TCB* tcb;

    VKI_MEMCLEAR(m_TaskIds, sizeof(m_TaskIds));
    VKI_MEMCLEAR(m_StackSegments, sizeof(m_StackSegments));
    int level = VKI_INT_DISABLE();
    int numTasks = VKI_TASK_LIST_GET(m_TaskIds, MAX_TASKS);
    for (int i = 0; i < numTasks; ++i)
    {
        tcb = reinterpret_cast<WIND_TCB*>(m_TaskIds[i]);
        SegmentDescriptor* seg = &m_StackSegments[i];
        if (tcb->pStackBase > tcb->pStackEnd) 
        {
            seg->setAddr(utl::utlFloor(reinterpret_cast<UINT32>(tcb->pStackEnd),
                                       STACK_SEGMENT_ALIGNMENT_BYTES));
            seg->setSize(utl::utlCeiling(reinterpret_cast<UINT32>(tcb->pStackBase) -
                                         static_cast<UINT32>(m_StackSegments[i].getAddr()) +
                                         sizeof(WIND_TCB),
                                         STACK_SEGMENT_ALIGNMENT_BYTES));
        }
        else 
        {
            seg->setAddr(utl::utlFloor(reinterpret_cast<UINT32>(tcb->pStackBase),
                                       STACK_SEGMENT_ALIGNMENT_BYTES));
            seg->setSize(utl::utlCeiling(reinterpret_cast<UINT32>(tcb->pStackEnd) -
                                         static_cast<UINT32>(m_StackSegments[i].getAddr()) +
                                         sizeof(WIND_TCB),
                                         STACK_SEGMENT_ALIGNMENT_BYTES));
        }

        // This prevents overlapping memory regions with the kernel space which is always
        // collected.  Some VxWorks tasks may be in this space.
        if (seg->getAddr() >= KERNEL_SEGMENT_END)
            m_AltSegmentVector->push_back(seg);
    }

    VKI_INT_RESTORE(level);
    return numTasks;
}

/*************************************************************************************
DESCRIPTION:
    Create the Notes Section of the ELF file
PARAMETER:
    pBuffer:     O/P parameter. Buffer 
RETURNS: 
    Returns notesOffset.
************************************************************************************/
UINT32
dcd::CoreDumpManager::createNotes
    (
    char* pBuffer
    )
{
    /* 
     * Note: the input pBuffer must be zero'd out prior to populating the notes section
     *       so that any padding between the note name string and alignmenment boundary 
     *       is zero-filled.
     */
    UINT32 notesNameSize = VKI_STRLEN(NOTES_NAME) + 1;
    UINT32 notesNameFillSize = ROUND_UP(notesNameSize, sizeof(size_t));

    Elf32_Nhdr* pNotesHeader = reinterpret_cast<Elf32_Nhdr *>(pBuffer);
    pNotesHeader->n_namesz   = notesNameSize;
    pNotesHeader->n_descsz   = sizeof(prpsinfo_t);
    pNotesHeader->n_type     = NT_PRPSINFO;

    UINT32 notesOffset = sizeof(Elf32_Nhdr);

    char* pNotesName   = pBuffer + notesOffset;
    VKI_STRNCPY(pNotesName, NOTES_NAME, notesNameSize);
    notesOffset += notesNameFillSize;

    prpsinfo_t* prInfo = reinterpret_cast<prpsinfo_t *>(pBuffer + notesOffset);

    prInfo->pr_version  = PRPSINFO_VERSION;
    prInfo->pr_psinfosz = sizeof(prpsinfo_t);
    VKI_STRNCPY(prInfo->pr_fname, "dcd Coredump",MAXCOMLEN);   
    VKI_STRNCPY(prInfo->pr_psargs, "dcd Coredump Args", PRARGSZ); //Coredump reason has to be filled here 

    notesOffset += sizeof(prpsinfo_t);

    // The order of the reg sets are important here.  The first one will be the
    // default task when GDB is invoked, so we want to make the most likely context
    // of concern the first notes section.  If there's an explicit register set,
    // that should be used.  If it's not set, the current task is likely the 
    // task of interest, so that will be noted first in the absence of an explicit
    // register set.

    // If register information has been saved by exception handler, add a notes 
    // section.  The Task ID is set to an arbitrary number (DCD_EXCEPTION_TASK_ID)
    // so it will be loaded and easily identified in GDB.
    if (m_DcdRegSet)
    {
        // fill in notes section for exception REG_SET
        // Notes Header
        pNotesHeader           = reinterpret_cast<Elf32_Nhdr *>(pBuffer + notesOffset);
        pNotesHeader->n_namesz = notesNameSize;
        pNotesHeader->n_descsz = sizeof(prstatus_t);
        pNotesHeader->n_type   = NT_PRSTATUS;

        // Notes Name
        notesOffset += sizeof(Elf32_Nhdr);
        pNotesName   = pBuffer + notesOffset;
        VKI_STRNCPY(pNotesName, NOTES_NAME, notesNameSize);
        notesOffset += notesNameFillSize;

        // Notes Description
        prstatus_t* pStatus = reinterpret_cast<prstatus_t *>(pBuffer + notesOffset);

        pStatus->pr_version    = PRSTATUS_VERSION;
        pStatus->pr_statussz   = sizeof(prstatus_t);
        pStatus->pr_gregsetsz  = sizeof(gregset_t);
        pStatus->pr_fpregsetsz = 0;
        pStatus->pr_osreldate  = PR_OSRELDATE;
        pStatus->pr_cursig     = 0; // Put zero now 
        pStatus->pr_pid        = DCD_EXCEPTION_TASK_ID;

        assignRegisters(DCD_EXCEPTION_TASK_ID, pStatus->pr_reg);

        notesOffset += sizeof(prstatus_t);
    }

    // Always include the current task context info here, and exclude it from the
    // task list below.  This gets it on the start of the thread list, where GDB
    // will pick up the default thread.
    VKI_TASK_ID currentTid = VKI_TASK_MY_ID();
    if (currentTid)
    {
        // fill in notes section for exception REG_SET
        // Notes Header
        pNotesHeader           = reinterpret_cast<Elf32_Nhdr *>(pBuffer + notesOffset);
        pNotesHeader->n_namesz = notesNameSize;
        pNotesHeader->n_descsz = sizeof(prstatus_t);
        pNotesHeader->n_type   = NT_PRSTATUS;

        // Notes Name
        notesOffset += sizeof(Elf32_Nhdr);
        pNotesName   = pBuffer + notesOffset;
        VKI_STRNCPY(pNotesName, NOTES_NAME, notesNameSize);
        notesOffset += notesNameFillSize;

        // Notes Description
        prstatus_t* pStatus = reinterpret_cast<prstatus_t *>(pBuffer + notesOffset);

        pStatus->pr_version    = PRSTATUS_VERSION;
        pStatus->pr_statussz   = sizeof(prstatus_t);
        pStatus->pr_gregsetsz  = sizeof(gregset_t);
        pStatus->pr_fpregsetsz = 0;
        pStatus->pr_osreldate  = PR_OSRELDATE;
        pStatus->pr_cursig     = 0; // Put zero now 
        pStatus->pr_pid        = currentTid;

        assignRegisters( currentTid, pStatus->pr_reg);

        notesOffset += sizeof(prstatus_t);
    }

    // Filling of m_TaskIds is temporary. This should actually be filled in the
    // calling function
    VKI_MEMCLEAR(m_TaskIds, sizeof(m_TaskIds));
    int numTasks = VKI_TASK_LIST_GET(m_TaskIds, MAX_TASKS);

    for (int tidCount = 0; tidCount < numTasks; ++tidCount)
    {
        // Notes Description
        VKI_TASK_ID taskId  = m_TaskIds[tidCount];

        // Avoid double-entry of the current task and invalid tasks.
        if (currentTid == taskId || !taskId)
            continue;

        // Notes Header
        pNotesHeader           = reinterpret_cast<Elf32_Nhdr *>(pBuffer + notesOffset);
        pNotesHeader->n_namesz = notesNameSize;
        pNotesHeader->n_descsz = sizeof(prstatus_t);
        pNotesHeader->n_type   = NT_PRSTATUS;

        // Notes Name
        notesOffset += sizeof(Elf32_Nhdr);
        pNotesName   = pBuffer + notesOffset;
        VKI_STRNCPY(pNotesName, NOTES_NAME, notesNameSize);
        notesOffset += notesNameFillSize;


        prstatus_t* pStatus = reinterpret_cast<prstatus_t *>(pBuffer + notesOffset);

        pStatus->pr_version    = PRSTATUS_VERSION;
        pStatus->pr_statussz   = sizeof(prstatus_t);
        pStatus->pr_gregsetsz  = sizeof(gregset_t);
        pStatus->pr_fpregsetsz = 0;
        pStatus->pr_osreldate  = PR_OSRELDATE;
        pStatus->pr_cursig     = 0; // Put zero now 
        pStatus->pr_pid        = taskId;

        assignRegisters( taskId, pStatus->pr_reg );

        notesOffset += sizeof(prstatus_t);

        // Check to ensure we don't exceed the provided buffer and cause more problems.
        if (notesOffset >= NOTES_STAGING_REGION_SIZE)
        {
            writeError(NOTES_BUFFER_OVERFLOW);
            break;
        }
    }

    return notesOffset;
}

/*************************************************************************************
DESCRIPTION:
    Create the Program Header for the Notes Section
PARAMETER:
    pHeader:     O/P parameter. Program Header Pointer
    fileOffset:  I/P parameter. File Offset
    notesSize:   I/P parameter. Notes Size    
RETURNS: 
    Returns true on Success.
************************************************************************************/
bool
dcd::CoreDumpManager::createNoteProgramHeader32
    (
    Elf32_Phdr* pHeader,
    UINT32      fileOffset,
    UINT32      notesSize
    )
{
    if (!pHeader)
       return false;

    pHeader->p_type   = PT_NOTE;
    pHeader->p_offset = fileOffset;
    pHeader->p_vaddr  = 0;
    pHeader->p_paddr  = 0;
    pHeader->p_filesz = notesSize;
    pHeader->p_memsz  = 0;
    pHeader->p_flags  = 0;
    pHeader->p_align  = 0;
    return true;
}

/*************************************************************************************
DESCRIPTION:
    Create the Program Header for the Notes Section
PARAMETER:
    pHeader:     O/P parameter. Program Header Pointer
    fileOffset:  I/P parameter. File Offset
    notesSize:   I/P parameter. Notes Size    
RETURNS: 
    Returns true on Success.
************************************************************************************/
bool
dcd::CoreDumpManager::createNoteProgramHeader64
    (
    Elf64_Phdr* pHeader,
    UINT64      fileOffset,
    UINT64      notesSize
    )
{
    if (!pHeader)
       return false;

    pHeader->p_type   = PT_NOTE;
    pHeader->p_offset = fileOffset;
    pHeader->p_vaddr  = 0;
    pHeader->p_paddr  = 0;
    pHeader->p_filesz = notesSize;
    pHeader->p_memsz  = 0;
    pHeader->p_flags  = 0;
    pHeader->p_align  = 0;
    return true;
}

/*************************************************************************************
DESCRIPTION:
    Registers a listener for core dump events.
PARAMETER:
    listener: I/P parameter. The listener pointer to register.
THROWS:
    std::bad_alloc()
************************************************************************************/
void
dcd::CoreDumpManager::registerCoreDumpListener
    (
    CoreDumpListener* listener
    )
{
    dqvkiWrite(dcdDqWriter, DCD_REGISTER_LISTENER, listener);

    m_Listeners.push_back(listener);
}

/*************************************************************************************
DESCRIPTION:
    Handles the PI CCD trigger notification event to registered listeners.
    This function is called from the IO Path, so no listener should do anything that
    could cause the current task to block.
PARAMETER:
    volSsid:    I/P parameter. The volume SSID of the detected corruption.
    blockNo:    I/P parameter. The block number of the detected corruption.
    iocChannel: I/P parameter. The IOC channel of the detected corruption.
    ioType:     I/P parameter. The PI IO type of the detected corruption.
************************************************************************************/
void
dcd::CoreDumpManager::handlePICcdTrigger
    (
    SSID volSsid,
    LBA64 blockNo,
    UINT iocChannel,
    PIIoType ioType
    )
{
    dqvkiWrite(dcdDqWriter, DCD_HANDLE_PI_CCD_TRIGGER, volSsid, blockNo, iocChannel, ioType);

    for (std::list<CoreDumpListener*>::iterator iter = m_Listeners.begin();
         iter != m_Listeners.end(); ++iter)
    {
        (*iter)->triggerPICcd(volSsid, blockNo, iocChannel, ioType);
    }
}

/*************************************************************************************
DESCRIPTION:
    Handles the nonIO CCD trigger notification event to registered listeners.
************************************************************************************/
void
dcd::CoreDumpManager::handleNonIOCcdTrigger
    (
    )
{
    dqvkiWrite(dcdDqWriter, DCD_HANDLE_NONIO_CCD_TRIGGER);

    for (std::list<CoreDumpListener*>::iterator iter = m_Listeners.begin();
         iter != m_Listeners.end(); ++iter)
    {
        (*iter)->triggerNonIOCcd();
    }
}

/*******************************************************************************
DESCRIPTION:
    Set the overwrite time for coredump
PARAMETER:
    overwriteThresholdTime: I/P parameter. Overwrite Threshold Time
************************************************************************************/
void
dcd::CoreDumpManager::setOverwriteThresholdTime
    (
    UINT32 overwriteThresholdTime
    )
{
    m_OverwriteThresholdTimeSec = overwriteThresholdTime;
}

/*******************************************************************************
DESCRIPTION:
    get the overwrite threshold time for coredump
RETURNS:    
    Overwrite Threshold Time
************************************************************************************/
UINT32
dcd::CoreDumpManager::getOverwriteThresholdTime
    (
    ) const
{
    return m_OverwriteThresholdTimeSec;
}

/*******************************************************************************
DESCRIPTION:
    Set the CoreDumpEventStatus for coredump 
PARAMETER:
    majorEvent: I/P parameter. Coredump major event
************************************************************************************/
void
dcd::CoreDumpManager::setEventStatus
    (
    DumpMajorEvent majorEvent
    )
{
    m_Metadata->setEventStatus(majorEvent);
}

/*******************************************************************************
DESCRIPTION:
    Set check point for coredump 
PARAMETER:
    checkPoint: I/P parameter. Coredump check point
************************************************************************************/
void
dcd::CoreDumpManager::setCheckPoint
    (
    DumpCheckpoint checkPoint
    )
{
    m_Metadata->setCheckpoint(checkPoint);
}

/*************************************************************************************
DESCRIPTION:
    Check if we can overwrite coredump region
PARAMETER:
    rebootReason: I/P parameter. Check the reason for capturing/rebooting
RETURNS: 
    Returns true: coredump region can be overwritten
            false: coredump region cannot be overwirtten
************************************************************************************/
bool
dcd::CoreDumpManager::overwriteCoredump
    (
    int rebootReason
    )
{
    if (m_Metadata->getNeedsRetrieval())
    {
        m_Metadata->setEventStatus(OVER_WRITTEN);
        if (rebootReason == COREDUMP_ON_DEMAND || rebootReason == CCD_COREDUMP)
        {
            return true;
        }

        UINT32 lastEventTime = m_Metadata->getCoreDumpTime();
        if((VKI_TIME(0) - lastEventTime) < (m_OverwriteThresholdTimeSec))
        {
            m_Metadata->setEventStatus(NOT_CAPTURED);
            return false;
        }
    }
    return true;
}

/*******************************************************************************
DESCRIPTION:
get CoreDumpEventStatus for MEL 
PARAMETER:
   None 
RETURNS: CoreDumpMajorEvent 
************************************************************************************/
dcd::CoreDumpMgmt::DumpMajorEvent
dcd::CoreDumpManager::getCoreDumpEventStatus
    (
    )const 
{
    return m_Metadata->getCoreDumpEventStatus();
}

/******************************************************************************
DESCRIPTION:
Capture the core dump.  Must not give up control of the CPU if the type indicates a full core dump.
Returns true if success, false if failure
*/
bool
dcd::CoreDumpManager::captureCoreDump
    (
    dcd::CoreDumpMgmt::DumpType type,
    int rebootReason
    )
{
    // Before doing anything involving stack operations,
    // save off the current register set for later use.
    CPU_SAVE_REG_SET(&m_DcdSavedEntryTaskRegs);

    // Register set location is provided to debug monitor so there
    // is a relevant context when using the emulator for analysis.
    if (m_DcdRegSet)
        dbgSetCoreDumpContext(m_DcdRegSet);
    else
        dbgSetCoreDumpContext(&m_DcdSavedEntryTaskRegs);

    m_Metadata->setCoreValid(false);
    std::vector<SegmentDescriptor*>* segVector = 0;
    if (type == FULL_DUMP)
        segVector = m_FullSegmentVector;
    else if (type == ALT_DUMP)
        segVector = m_AltSegmentVector;
    else // Just return, doing nothing, since we don't know the context.
        return false;

    UINT64 startTime = VKI_TIMESTAMP_GET();
    // There are two metadata headers
    UINT32 metadataSize = sizeof(RWCoreDumpMetadataRecord) + sizeof(StorageRecord);
    m_Metadata->startDump(type, m_DcdInfo, m_DcdPanicString, dcdScratchpad, VKI_TIME(0),
                          rebootReason, m_ModuleInfoVector, metadataSize);

    CADDR coreDumpBase = getCoreDumpBase() + metadataSize;
    CADDR coreDumpEnd  = getCoreDumpEnd();
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    if (type == FULL_DUMP)
    {
        if (!m_SharedRegionBase)
        {
            writeError(SECCACHE_BASE_NULL);
            return false;
        }

        coreDumpBase = m_SharedRegionBase;
        coreDumpEnd = m_SharedRegionBase + m_SharedRegionSize;
    }
//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    if (dcdForceCoreDumpSizeCheck == 1)
    {
        // Force a size check by reducing the available size
        coreDumpEnd = coreDumpBase + 0x8000;
    }

    m_Metadata->setCheckpoint(WRITING_ELF_DIR_HDR);
    SectionMetadataRecord elfDirRec(CORE_ELF_DIR, 1, dcd::NO_ID);
    elfDirRec.setDataAttributes(0, 0);
    UINT crcValue = 0;
    if (!copyWithCrc(coreDumpBase, utl::Casts::castFromPtrToCADDR(&elfDirRec),
                     sizeof(SectionMetadataRecord), true, &crcValue))
    {
        writeError(ELF_DIR_HEADER_FAIL);
        return false;
    }

    coreDumpBase += sizeof(SectionMetadataRecord); // Advance past directory we just wrote

    // Save the address and advance the address past the section metadata we'll write as soon as
    // we know the section's size and CRC
    CADDR elfSectionAddr = coreDumpBase;
    coreDumpBase += sizeof(SectionMetadataRecord);

    UINT32 cfPtr = RPA_MAPPED_ADDR(coreDumpBase);   // Pointer to the base of the ELF file.

    // Used by non-CCD (32 bit ELF)
    Elf32_Ehdr* EH32_ptr = reinterpret_cast<Elf32_Ehdr*>(cfPtr);
    Elf32_Phdr* PH32_ptr = reinterpret_cast<Elf32_Phdr*>(cfPtr + sizeof(Elf32_Ehdr));

    // Used by CCD (64 bit ELF)
    Elf64_Ehdr* EH64_ptr = reinterpret_cast<Elf64_Ehdr*>(cfPtr);
    Elf64_Phdr* PH64_ptr = reinterpret_cast<Elf64_Phdr*>(cfPtr + sizeof(Elf64_Ehdr));

    if (type == ALT_DUMP)
    {
        VKI_TASK_LOCK();
        if (!updateAltSegmentVector())
        {
            writeError(ALT_SEGMENT_UPDATE_FAIL);
            VKI_TASK_UNLOCK();
            return false;
        }

        // Collect the TCB/stack segments for alt core dump,
        // adding them to the alt core dump vector in the process.
        getStackSegments();
    }

    // The note program header is always present.  Any additional program headers
    // should be in the appropriate segment vector.  This must be done after
    // getStackSegments() so we have all vector elements accounted for.
    UINT numHeaders = 1 + segVector->size();
    if (isCcd()) // we'll add a section to represent the extended (RPA) memory
        numHeaders++;

    UINT32 loadSectionsPtr = reinterpret_cast<UINT32>(PH32_ptr + numHeaders);
    if (isCcd())
        loadSectionsPtr = reinterpret_cast<UINT32>(PH64_ptr + numHeaders);

    int saveIntState = VKI_INT_DISABLE();
    UINT32 saveRpaPage = RPA_GET_PAGE_CONTROL();
    RPA_SET_PAGE_CONTROL(RPA_GET_PAGENO(coreDumpBase));

    UINT32 loadSectionsBaseOffset = utl::utlCeiling(loadSectionsPtr - cfPtr, sizeof(UINT64));
    UINT64 fileOffset = loadSectionsBaseOffset;
    for (std::vector<SegmentDescriptor*>::const_iterator cIter = segVector->begin();
         cIter != segVector->end(); ++cIter)
    {
        SegmentDescriptor* seg = *cIter;
        if (isCcd())
            createLoadProgramHeader64(PH64_ptr++, seg, fileOffset);
        else
            createLoadProgramHeader32(PH32_ptr++, seg, static_cast<UINT32>(fileOffset));

        fileOffset += seg->getSize();
    }

    // The load sections come before the notes to allow the kernel memory space to 
    // be close enough (within 8K) of the beginning of the file to boot properly
    // under the emulator.  If the emulator cannot find the "multiboot" signature in 
    // this space, it assumes a BSD format, which uses only 24 bits of address to
    // specify section location.  This is OK for full core dump, but most regions
    // defined for the alternate core dump require 32 bit addressing... so the notes
    // are pushed to the end of the ELF file to make that work.

    UINT32 noteSectionSz = createNotes(m_NotesStagingBuffer);

    if (isCcd()) // 64-bit ELF
    {
        createNoteProgramHeader64(PH64_ptr++, fileOffset, noteSectionSz);
        fileOffset += noteSectionSz;

        // Add the final load program header, which is the entire RPA memory segment
        SegmentDescriptor seg;
        seg.setAddr(rpai::RegionMgr::getInstance()->getPhysicalBase(RPA_INFO));
        seg.setSize(static_cast<CADDR>(rpai::RegionMgr::getInstance()->getRpaMemorySizeMB()) MB);
        createLoadProgramHeader64(PH64_ptr++, &seg, fileOffset);

        createElfHeader64(EH64_ptr, numHeaders);
    }
    else // 32-bit ELF
    {
        createNoteProgramHeader32(PH32_ptr++, static_cast<UINT32>(fileOffset), noteSectionSz);
        createElfHeader32(EH32_ptr, numHeaders);
    }

    RPA_SET_PAGE_CONTROL(saveRpaPage);
    VKI_INT_RESTORE(saveIntState);

    m_Metadata->setNotesSize(noteSectionSz);
    m_Metadata->setCheckpoint(WRITING_CORE);

    // Compute the CRC across the elf file region, copying out the portions
    // of CPU memory that should go into the elf file in the process.
    CADDR dest = 0;
    fileOffset = loadSectionsBaseOffset;
    crcValue = 0;
    if (!computeCrc(coreDumpBase, static_cast<UINT32>(fileOffset), false, &crcValue))
    {
        writeError(CORE_CRC_COMPUTE_ERROR);
        if (type == ALT_DUMP)
            VKI_TASK_UNLOCK();

        return false;
    }

    for (std::vector<SegmentDescriptor*>::const_iterator cIter = segVector->begin();
         cIter != segVector->end(); ++cIter)
    {
        SegmentDescriptor* seg = *cIter;
        dest = coreDumpBase + fileOffset;

        // Check that seg size does not exceed region
        if (dest + seg->getSize() > coreDumpEnd)
        {
            writeError(CORE_SIZE_ERROR);
            if (type == ALT_DUMP)
                VKI_TASK_UNLOCK();

            return false;
        }

        if (!copyWithCrc(dest, seg->getAddr(), static_cast<UINT32>(seg->getSize()), true,
                         &crcValue))
        {
            writeError(CORE_COPY_ERROR);
            if (type == ALT_DUMP)
                VKI_TASK_UNLOCK();

            return false;
        }

        fileOffset += seg->getSize();
    }

    dest = coreDumpBase + fileOffset;
    if (dest + noteSectionSz > coreDumpEnd)
    {
        writeError(CORE_SIZE_ERROR);
        if (type == ALT_DUMP)
            VKI_TASK_UNLOCK();
        
        return false;
    }

    m_NotesOffset = static_cast<UINT32>(fileOffset);

    if (!copyWithCrc(dest, utl::Casts::castFromPtrToCADDR(m_NotesStagingBuffer),
                     noteSectionSz, true, &crcValue))
    {
        writeError(NOTES_COPY_ERROR);
        if (type == ALT_DUMP)
            VKI_TASK_UNLOCK();
        
        return false;
    }

    if (type == ALT_DUMP)
    {
        VKI_TASK_UNLOCK();
    }

#ifdef CORE_DUMP_TEST
    /* check notes for validity */
    int noteCheck = RPA_MEMCMP(dest, reinterpret_cast<UINT32>(m_NotesStagingBuffer), noteSectionSz);
    if (noteCheck != 0)
        VKI_CMN_ERR(CE_PANIC, "Note miscompare after copy: %d", noteCheck);
#endif /* CORE_DUMP_TEST */

    fileOffset += noteSectionSz;

//* BeginGearsBlock Cpp HW_Processor_Pentium
    m_Metadata->setCrcSupported(true);
//* EndGearsBlock Cpp HW_Processor_Pentium 
//* BeginGearsBlock Cpp HW_Processor_PPC 
    //m_Metadata->setCrcSupported(false);
//* EndGearsBlock Cpp HW_Processor_PPC

    m_Metadata->setCrc(ELF_CORE, crcValue);

    // Back up to the section metadata and write the section header now that we know the size & crc
    m_Metadata->setCheckpoint(WRITING_ELF_SEC_HDR);
    SectionMetadataRecord elfSecRec(CPU_ELF, 0, dcd::NO_ID);
    elfSecRec.setDataAttributes(fileOffset, crcValue);
    if (!copyWithCrc(elfSectionAddr, utl::Casts::castFromPtrToCADDR(&elfSecRec),
                     sizeof(SectionMetadataRecord), true, &crcValue))
    {
        writeError(ELF_SECTION_HEADER_FAIL);
        return false;
    }

    fileOffset += sizeof(SectionMetadataRecord) * 2; // Advance past section headers
    CADDR dqOffsetInMemory  = metadataSize + fileOffset;
    CADDR dqBase = getCoreDumpBase() + dqOffsetInMemory;
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    if (type == FULL_DUMP)
    {
        dqOffsetInMemory = fileOffset;
        dqBase = m_SharedRegionBase + dqOffsetInMemory;
    }
//* EndGearsBlock Cpp Feature_EnhancedCoreDump

    m_Metadata->setCheckpoint(WRITING_DQ_DIR_HDR);
    SectionMetadataRecord dqDirRec(DQ_DIR, 1, dcd::NO_ID);
    dqDirRec.setDataAttributes(0, 0);
    if (!copyWithCrc(dqBase, utl::Casts::castFromPtrToCADDR(&dqDirRec),
                     sizeof(SectionMetadataRecord), true, &crcValue))
    {
        writeError(DQ_DIR_HEADER_FAIL);
        return false;
    }

    // Advance past the directory metadata we just wrote
    dqBase += sizeof(SectionMetadataRecord);
    dqOffsetInMemory += sizeof(SectionMetadataRecord);

    // Save the address and advance the address past the section metadata we'll write as soon
    // as we know the section's size and CRC
    CADDR dqSectionAddr = dqBase;
    dqBase += sizeof(SectionMetadataRecord);
    dqOffsetInMemory += sizeof(SectionMetadataRecord);

    if (dqvkiIsFTDCPending(dqFind("trace")))
    {
        writeError(DQ_FLUSH_INPROGRESS);
        dcdSkipDq=1;
    }

    m_Metadata->setCheckpoint(WRITING_DQ);

    UINT32 dqDataSize = 0;
    if (!dcdSkipDq)
    {
        dqDataSize = copyTraceDq(dqOffsetInMemory, coreDumpEnd);
        if (dqDataSize)
        {
            crcValue = 0;
            if (!computeCrc(dqBase, dqDataSize, false, &crcValue))
            {
                writeError(CORE_CRC_COMPUTE_ERROR);
                return false;
            }

            m_Metadata->setCrc(DQ, crcValue);
        }
    }

    // Back up to the section metadata and write the section header now that we know the size & crc
    m_Metadata->setCheckpoint(WRITING_DQ_SEC_HDR);
    SectionMetadataRecord dqSecRec(DQ_TRACE, 0, dcd::NO_ID);
    dqSecRec.setDataAttributes(dqDataSize, crcValue);
    if (!copyWithCrc(dqSectionAddr, utl::Casts::castFromPtrToCADDR(&dqSecRec),
                     sizeof(SectionMetadataRecord), true, &crcValue))
    {
        writeError(DQ_SECTION_HEADER_FAIL);
        return false;
    }

    dqDataSize += sizeof(SectionMetadataRecord) * 2;  // Bookkeeping so total size is accurate
    CADDR sharedBase = m_SharedRegionBase;

    // corrupt shared region base address in metadata 
    // (was secondary cache base)
    if (dcdCorruptSharedRegionBaseInMetadata)
        sharedBase += 0x300;

    // Calculate dump time in msec - should be much less than 32-bits worth of msec.
    UINT64 dumpTime = (VKI_TIMESTAMP_GET() - startTime) / (VKI_TIMESTAMP_RATE() / 1000);

    CADDR totalSize = static_cast<CADDR>(metadataSize) + fileOffset + dqDataSize;
    m_Metadata->finishDump(static_cast<UINT32>(dumpTime), totalSize, sharedBase);
    return true;
}

/******************************************************************************
DESCRIPTION:
Show routine called from shell.
*/
void
dcd::CoreDumpManager::show
    (
    UINT32 level
    ) const
{
    showMetadata();
    if (level >= SHOW_MODULE_INFO )
    {
        bool heading = true;
        RWModuleInfo::ModuleInfoVector::const_iterator moduleInfoCIter;
        for(moduleInfoCIter = m_ModuleInfoVector.begin();
            moduleInfoCIter != m_ModuleInfoVector.end();
            moduleInfoCIter++)
        {
            if (!*moduleInfoCIter)
                continue;

            if (heading)
            {
                (*moduleInfoCIter)->showHeading(false);
                heading = false;
            }
            (*moduleInfoCIter)->show();
        }
    }
    if (level >= SHOW_DETAILED_INFO)
    {
        VKI_PRINTF("Task control block information:\n");
        for (int i = 0; i < MAX_TASKS; i++)
        {
            if (!m_StackSegments[i].getAddr())
                break;
            VKI_PRINTF("Task %d: ", i);
            m_StackSegments[i].show();
        }
        VKI_PRINTF("Full segment vector information:\n");
        for (std::vector<SegmentDescriptor*>::const_iterator cIter = m_FullSegmentVector->begin();
             cIter != m_FullSegmentVector->end(); ++cIter)
        {
            SegmentDescriptor* seg = *cIter;
            if (!seg)
                break;
            seg->show();
        }
        VKI_PRINTF("Alt segment vector information:\n");
        for (std::vector<SegmentDescriptor*>::const_iterator cIter = m_AltSegmentVector->begin();
             cIter != m_AltSegmentVector->end(); ++cIter)
        {
            SegmentDescriptor* seg = *cIter;
            if (!seg)
                break;
            seg->show();
        }
        VKI_PRINTF("Alt client segment vector information:\n");
        for (std::vector<SegmentDescriptor*>::const_iterator cIter = m_AltClientSegmentVector->begin();
             cIter != m_AltClientSegmentVector->end(); ++cIter)
        {
            SegmentDescriptor* seg = *cIter;
            if (!seg)
                break;
            seg->show();
        }
    }
    VKI_PRINTF("m_DqWritePtr :  0x" CADDR_FMT"\n", m_DqWritePtr);
    VKI_PRINTF("m_DqWriteIndex: %u\n", m_DqWriteIndex);
    VKI_PRINTF("m_LblTblUsedPtr:0x%" PRIxPTR "\n", PTR_FMT_CAST(m_LblTblUsedPtr));
    VKI_PRINTF("m_DcdRpaAccessPriority     : %u\n", m_DcdRpaAccessPriority);
    VKI_PRINTF("m_OverwriteThresholdTimeSec: %u\n", m_OverwriteThresholdTimeSec);
    VKI_PRINTF("m_InitComplete: %s\n", m_InitComplete ? "true" : "false");
    VKI_PRINTF("m_CoreDumpInProgress: %s\n", m_CoreDumpInProgress ? "true" : "false");
    VKI_PRINTF("m_SharedRegionBase : 0x" CADDR_FMT"\n", m_SharedRegionBase);
    VKI_PRINTF("m_SharedRegionSize : 0x" CADDR_FMT"\n", m_SharedRegionSize);
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    VKI_PRINTF("Enhanced Core Dump : Enabled\n");
//* ElseGearsBlock Cpp Feature_EnhancedCoreDump
    //VKI_PRINTF("Enhanced Core Dump : Disabled\n");
//* EndGearsBlock Cpp Feature_EnhancedCoreDump
    VKI_PRINTF("m_DcdDualFullCoreDump: %s\n", m_DcdDualFullCoreDump ? "true" : "false");
    VKI_PRINTF("Listeners: (");
    for (std::list<CoreDumpListener*>::const_iterator cIter = m_Listeners.begin();
         cIter != m_Listeners.end(); ++cIter)
    {
        VKI_PRINTF("0x%" PRIxPTR "", PTR_FMT_CAST(*cIter));
    }
    VKI_PRINTF(")\n");
}

/******************************************************************************
DESCRIPTION:
Show routine called from shell.
*/
void
dcd::CoreDumpManager::showRPAMetadata
    (
    ) const
{
    ROCoreDumpMetadataRecord* metadata = 0;
    try
    {
        metadata = new ROCoreDumpMetadataRecord;
        CADDR rpaAddr = getCoreDumpBase();
        RPA_MEMCPY(utl::Casts::castFromPtrToCADDR(metadata), rpaAddr,
                   sizeof(ROCoreDumpMetadataRecord));
        VKI_PRINTF("RPA metadata (0x%llx)\n", rpaAddr);
        metadata->show();
        delete metadata;
        metadata = 0;
    }
    catch(std::exception& e)
    {
        delete metadata;
        metadata = 0;
        VKI_CMN_ERR(CE_WARN, "Caught %s in CoreDumpManager showRPAMetadata", e.what());
    }
}

/******************************************************************************
DESCRIPTION:
Show routine called from shell.
*/
void
dcd::CoreDumpManager::showRPAStorageRecord
    (
    ) const
{
    StorageRecord* record = 0;
    try
    {
        record = new StorageRecord;
        CADDR rpaAddr = getCoreDumpBase() + sizeof(ROCoreDumpMetadataRecord);
        RPA_MEMCPY(utl::Casts::castFromPtrToCADDR(record), rpaAddr, sizeof(StorageRecord));
        VKI_PRINTF("RPA Storage Record (0x%llx)\n", rpaAddr);
        record->show();
        delete record;
        record = 0;
    }
    catch(std::exception& e)
    {
        delete record;
        record = 0;
        VKI_CMN_ERR(CE_WARN, "Caught %s in CoreDumpManager showRPAStorageRecord", e.what());
    }
}


// #################################################################################################
// *************************************************************************************************
//                         Implementation of Inner Class SegmentDescriptor
// *************************************************************************************************
// #################################################################################################

/******************************************************************************
DESCRIPTION:
Constructor. Initializes the SegmentDescriptor object.
*/
dcd::CoreDumpManager::SegmentDescriptor::SegmentDescriptor
    (
    ) : m_BaseAddr(0),
        m_Size(0)
{
}

/******************************************************************************
DESCRIPTION:
Constructor. Initializes the SegmentDescriptor object with base address and size.
*/
dcd::CoreDumpManager::SegmentDescriptor::SegmentDescriptor
    (
    UINT64 baseAddr,
    UINT64 size
    ) : m_BaseAddr(baseAddr),
        m_Size(size)
{
}

/******************************************************************************
DESCRIPTION:
Destructor.
*/
dcd::CoreDumpManager::SegmentDescriptor::~SegmentDescriptor
    (
    )
{
}

/******************************************************************************
DESCRIPTION:
Get the base address of the segment descriptor.
*/
UINT64
dcd::CoreDumpManager::SegmentDescriptor::getAddr
    (
    ) const
{
    return m_BaseAddr;
}

/******************************************************************************
DESCRIPTION:
Get the size of the segment descriptor.
*/
UINT64
dcd::CoreDumpManager::SegmentDescriptor::getSize
    (
    ) const
{
    return m_Size;
}

/******************************************************************************
DESCRIPTION:
Set the base address of the segment descriptor.
*/
void
dcd::CoreDumpManager::SegmentDescriptor::setAddr
    (
    UINT64 baseAddr
    )
{
    m_BaseAddr = baseAddr;
}

/******************************************************************************
DESCRIPTION:
Set the size of the segment descriptor.
*/
void
dcd::CoreDumpManager::SegmentDescriptor::setSize
    (
    UINT64 size
    )
{
    m_Size = size;
}

/******************************************************************************
DESCRIPTION:
Show the members of the segment descriptor.
*/
void
dcd::CoreDumpManager::SegmentDescriptor::show
    (
    ) const
{
    VKI_PRINTF("baseAddr:0x%llx size:0x%llx\n", m_BaseAddr, m_Size);
}

/**
 * \param   The moduleId whose information needs to be stored
 * \returns true,  if success
 *          false, if module info cannot be obtained or exception in creating new object
 * 
 * Store given moduleId's module information
 */
bool
dcd::CoreDumpManager::moduleInfoStore
    (
    UINT32 moduleId  // The moduleId whose information need to be stored
    )
{
    MODULE_INFO  moduleInfo;

    if (moduleInfoGet(reinterpret_cast<MODULE_ID>(moduleId), &moduleInfo))
    {
        dqvkiWrite(dcdDqWriter, DCD_MODULE_UNABLE_TO_OBTAIN);
        return false;
    }
    if (m_ModuleInfoVector.size() == MAX_MODULE_INFO_VECTOR)
    {
        dqvkiWrite(dcdDqWriter, DCD_MODULE_TOO_MANY);
        return false;
    }

    try
    {
        RWModuleInfo* moduleData = new RWModuleInfo( UINT32(moduleId),
                moduleInfo.segInfo.textAddr,
                moduleInfo.segInfo.dataAddr,
                moduleInfo.segInfo.bssAddr,
                moduleInfo.segInfo.textSize,
                moduleInfo.segInfo.dataSize,
                moduleInfo.segInfo.bssSize,
                moduleInfo.name
                );

        m_ModuleInfoVector.push_back(moduleData);

        // Add the text, data, and bss segments to the alt client segment vector.
        // They will be collected as part of the lock core dump.
        addCoreDumpRegion(moduleData->getTextAddr(), moduleData->getTextSize(), ALT_DUMP);
        addCoreDumpRegion(moduleData->getDataAddr(), moduleData->getDataSize(), ALT_DUMP);
        addCoreDumpRegion(moduleData->getBssAddr(),  moduleData->getBssSize(),  ALT_DUMP);
    }
    catch(std::exception& e)
    {
        dqvkiWrite(dcdDqWriter, DCD_MODULE_EXCEPTION, e.what());
        return FALSE;
    }

    return TRUE;
}


/**
 * Store loaded modules information[s] at the time of 
 * initialization
 */
void
dcd::CoreDumpManager::moduleInfoStoreInit
    (
    )
{
    MODULE_ID idList[MAX_MODULE_INFO_VECTOR]; 

    UINT32    listCount = moduleIdListGet(idList, MAX_MODULE_INFO_VECTOR);

    if(listCount == MAX_MODULE_INFO_VECTOR)
    {
        dqvkiWrite(dcdDqWriter, DCD_MODULE_INCREASE_MAX);
        VKI_CMN_ERR(CE_PANIC,"dcd: Increase MAX_MODULE_INFO_VECTOR > %d\n", listCount);
    }

    for( UINT count=0; count< listCount; count++)
    {
        if (!moduleInfoStore(UINT32(idList[count])))
        {
            VKI_CMN_ERR(CE_ERROR,"dcd: Can\'t get . %#" PRIxPTR ".\n",
                        PTR_FMT_CAST(idList[count]));
            // Better to continue to get other module Info
        }
    }
}

/******************************************************************************
DESCRIPTION:
Metadata Show routine called from shell.
 */
void 
dcd::CoreDumpManager::showMetadata
    ( 
    ) const
{
    VKI_PRINTF("m_Metadata (0x%" PRIxPTR ")\n", PTR_FMT_CAST(m_Metadata));
    m_Metadata->show();
}

/******************************************************************************
DESCRIPTION:
Storage Record Show routine called from shell.
 */
void
dcd::CoreDumpManager::showStorageRecord
    (
    ) const
{
    VKI_PRINTF("m_StorageRecord (0x%" PRIxPTR ")\n", PTR_FMT_CAST(m_StorageRecord));
    m_StorageRecord->show();
}

/******************************************************************************
DESCRIPTION: 
Retruns the Coredump valid flag  
 */
bool 
dcd::CoreDumpManager::getCoreDumpValid
    (
    ) const 
{
     return m_Metadata->getCoreValid();
} 

/******************************************************************************
DESCRIPTION:
Returns Coredump tag value
 */
UINT32
dcd::CoreDumpManager::getCoreDumpTag
    (
    ) const
{
    return m_Metadata->getCoreTag();
}

/******************************************************************************
DESCRIPTION:
Returns Coredump size in raw bytes in memory
 */
CADDR
dcd::CoreDumpManager::getCoreDumpSizeInMem
    (
    ) const
{
    return m_Metadata->getCoreDumpSizeInMem();
}

/******************************************************************************
DESCRIPTION:
Returns Coredump size in raw/compressed bytes on disk
 */
CADDR
dcd::CoreDumpManager::getCoreDumpSizeOnDisk
    (
    ) const
{
    return m_Metadata->getCoreDumpSizeOnDisk();
}

/******************************************************************************
DESCRIPTION:
Returns offset to the first section metadata
 */
UINT32
dcd::CoreDumpManager::getFirstSectionOffset
    (
    ) const
{
    return m_Metadata->getFirstSectionOffset();
}

/******************************************************************************
DESCRIPTION:
Returns core dump type on this controller.
 */
dcd::CoreDumpManagement::DumpType
dcd::CoreDumpManager::getCoreDumpType
    (
    ) const
{
    return m_Metadata->getCoreDumpType();
}

/******************************************************************************
DESCRIPTION:
Returns whether the core dump on this controller is a valid full core dump.
 */
bool
dcd::CoreDumpManager::isValidFullCoreDump
    (
    ) const
{
    return m_Metadata->isValidFullCoreDump();
}

/******************************************************************************
DESCRIPTION:
Returns whether the core dump on this controller is a valid lock core dump.
 */
bool
dcd::CoreDumpManager::isValidLockCoreDump
    (
    ) const
{
    return m_Metadata->isValidLockCoreDump();
}

/******************************************************************************
DESCRIPTION:
Returns whether the core dump on this controller is a valid cache core dump.
 */
bool
dcd::CoreDumpManager::isValidCcd
    (
    ) const
{
    return m_Metadata->isValidCcd();
}

/******************************************************************************
DESCRIPTION:
Returns whether the core dump on this controller can be overwritten by a cache core dump
 */
bool
dcd::CoreDumpManager::isCcdOverwriteAllowed
    (
    )
{
    // A CCD can only overwrite another CCD if it has expired. All other core dump types
    // can be overwritten without waiting.
    if (m_Metadata->isValidCcd() && m_Metadata->getNeedsRetrieval())
    {
        UINT32 lastEventTime = m_Metadata->getCoreDumpTime();
        if((VKI_TIME(0) - lastEventTime) < (m_OverwriteThresholdTimeSec))
            return false;
    }

    return true;
}

/******************************************************************************
DESCRIPTION:
Returns additional event details for coredump MEL
 */
void
dcd::CoreDumpManager::getCoreDumpEventDetails
    (
    COREDUMP_EVENT_DETAIL* coredumpEventDetail
    )
{
    m_Metadata->getCoreDumpEventDetails(coredumpEventDetail);
}

/******************************************************************************
DESCRIPTION:
Returns core dump capture time
 */
UINT32
dcd::CoreDumpManager::getCoreDumpTime
    (
    ) const
{
    return m_Metadata->getCoreDumpTime();
}

/******************************************************************************
DESCRIPTION:
Returns core dump reboot reason 
 */
UINT32
dcd::CoreDumpManager::getRebootReason
    (
    ) const
{
    return m_Metadata->getRebootReason();
}

/******************************************************************************
DESCRIPTION:
Sets core dump capture time
 */
void
dcd::CoreDumpManager::setCoreDumpTime
    (
    UINT32 timestamp
    )
{
    m_Metadata->setCoreDumpTime(timestamp);
}

/******************************************************************************
DESCRIPTION:
Sets core dump reboot reason 
 */
void
dcd::CoreDumpManager::setRebootReason
    (
    UINT32 rebootReason
    )
{
    m_Metadata->setRebootReason(rebootReason);
}

/******************************************************************************
DESCRIPTION:
Sets Core Dump compressed flag
 */
void
dcd::CoreDumpManager::setCompressed
    (
    bool compressed
    )
{
    m_Metadata->setCompressed(compressed);
}

/******************************************************************************
DESCRIPTION:
Gets Core Dump compressed flag
 */
bool
dcd::CoreDumpManager::isCompressed
    (
    ) const
{
    return m_Metadata->isCompressed();
}

/******************************************************************************
DESCRIPTION:
Gets Cache Core Dump flag
 */
bool
dcd::CoreDumpManager::isCcd
    (
    ) const
{
    return m_Metadata->isCcd();
}

/******************************************************************************
DESCRIPTION:
Sets Core Dump size in memory
 */
void
dcd::CoreDumpManager::setCoreDumpSizeInMem
    (
    UINT64 size
    )
{
    m_Metadata->setCoreDumpSizeInMem(size);
}

/******************************************************************************
DESCRIPTION:
Sets Core Dump size on disk
 */
void
dcd::CoreDumpManager::setCoreDumpSizeOnDisk
    (
    UINT64 size
    )
{
    m_Metadata->setCoreDumpSizeOnDisk(size);
}

/******************************************************************************
DESCRIPTION:
Sets core dump block size
 */
void
dcd::CoreDumpManager::setBlockSize
    (
    UINT32 blockSize
    )
{
    return m_Metadata->setBlockSize(blockSize);
}

/******************************************************************************
DESCRIPTION:
Sets core dump Ccd type
 */
void
dcd::CoreDumpManager::setCcdType
    (
    dcd::CcdType ccdType
    )
{
    return m_Metadata->setCcdType(ccdType);
}

/******************************************************************************
DESCRIPTION:
Clear the Storage Record, set the values to defaults
 */
void
dcd::CoreDumpManager::clearStorageRecord
    (
    )
{
    return m_StorageRecord->clearStorageRecord();
}

/******************************************************************************
DESCRIPTION:
Set the Storage Record valid flag
 */
void
dcd::CoreDumpManager::setSRValid
    (
    bool valid
    )
{
    return m_StorageRecord->setValid(valid);
}

/******************************************************************************
DESCRIPTION:
Returns the Storage Record valid flag
 */
bool
dcd::CoreDumpManager::isSRValid
    (
    ) const
{
    return m_StorageRecord->isValid();
}

/******************************************************************************
DESCRIPTION:
Sets the Storage Record version number
 */
void
dcd::CoreDumpManager::setSRVersion
    (
    UINT32 version
    )
{
    return m_StorageRecord->setVersion(version);
}

/******************************************************************************
DESCRIPTION:
Gets the Storage Record version number
 */
UINT32
dcd::CoreDumpManager::getSRVersion
    (
    ) const
{
    return m_StorageRecord->getVersion();
}

/******************************************************************************
DESCRIPTION:
Sets the Storage Record CCD state
 */
void
dcd::CoreDumpManager::setSRState
    (
    dcd::CcdState state
    )
{
    return m_StorageRecord->setState(state);
}

/******************************************************************************
DESCRIPTION:
Get the Storage Record CCD state
 */
dcd::CcdState
dcd::CoreDumpManager::getSRState
    (
    ) const
{
    return m_StorageRecord->getState();
}

/******************************************************************************
DESCRIPTION:
Sets the Storage Record storage mode
 */
void
dcd::CoreDumpManager::setSRMode
    (
    dcd::CcdStorageMode storageMode
    )
{
    return m_StorageRecord->setMode(storageMode);
}

/******************************************************************************
DESCRIPTION:
Get the Storage Record storage mode
 */
dcd::CcdStorageMode
dcd::CoreDumpManager::getSRMode
    (
    ) const
{
    return m_StorageRecord->getMode();
}

/******************************************************************************
DESCRIPTION:
Set the Storage Record volume ssid for this controller
 */
void
dcd::CoreDumpManager::setSRVolumeSsid
    (
    UINT32 ssid
    )
{
    return m_StorageRecord->setVolumeSsid(ssid);
}

/******************************************************************************
DESCRIPTION:
Get the Storage Record volume ssid for this controller
 */
UINT32
dcd::CoreDumpManager::getSRVolumeSsid
    (
    ) const
{
    return m_StorageRecord->getVolumeSsid();
}

/******************************************************************************
DESCRIPTION:
Set the Storage Record volume ssid for the alternate controller
 */
void
dcd::CoreDumpManager::setSRAltVolumeSsid
    (
    UINT32 ssid
    )
{
    return m_StorageRecord->setAltVolumeSsid(ssid);
}

/******************************************************************************
DESCRIPTION:
Get the Storage Record volume ssid for the alternate controller
 */
UINT32
dcd::CoreDumpManager::getSRAltVolumeSsid
    (
    ) const
{
    return m_StorageRecord->getAltVolumeSsid();
}

/******************************************************************************
DESCRIPTION:
Set the Storage Record network path
 */
bool
dcd::CoreDumpManager::setSRNetworkPath
    (
    const char* path
    )
{
    return m_StorageRecord->setNetworkPath(path);
}

/******************************************************************************
DESCRIPTION:
Get the Storage Record network path
 */
const char*
dcd::CoreDumpManager::getSRNetworkPath
    (
    )
{
    return m_StorageRecord->getNetworkPath();
}

/******************************************************************************
DESCRIPTION:
Returns SharedRegionBase from metadata 
 */
CADDR
dcd::CoreDumpManager::getMetadataSharedRegionBase
    (
    ) const 
{
   return m_Metadata->getSharedRegionBase();
}

/**
 * @param priority, Priority of core dump RPA region access
 *
 * Set the RPA access priority 
 * Note: Should not use this API to set CAPTURE_PRIORITY
 */
void
dcd::CoreDumpManager::setDcdRpaAccessPriority
    (
    DcdRpaAccessPriority priority
    )
{
    m_DcdRpaAccessPriority = priority;
}

/******************************************************************************
DESCRIPTION:
Returns the core dump metadata.
 */
dcd::ROCoreDumpMetadataRecord*
dcd::CoreDumpManager::getMetadata
    (
    ) const
{
    return m_Metadata;
}

/******************************************************************************
DESCRIPTION:
Set in-memory core dump metadata record
 */
void
dcd::CoreDumpManager::setMetadata
    (
    dcd::ROCoreDumpMetadataRecord* metadata
    )
{
    VKI_MEMCPY(m_Metadata, metadata, sizeof(dcd::ROCoreDumpMetadataRecord));
}

/******************************************************************************
DESCRIPTION:
Returns the Storage Record
 */
dcd::StorageRecord*
dcd::CoreDumpManager::getStorageRecord
    (
    ) const
{
    return m_StorageRecord;
}

/******************************************************************************
DESCRIPTION:
Set in-memory Storage Record
 */
void
dcd::CoreDumpManager::setStorageRecord
    (
    dcd::StorageRecord* record
    )
{
    VKI_MEMCPY(m_StorageRecord, record, sizeof(dcd::StorageRecord));
}

/******************************************************************************
DESCRIPTION: 
reads the metadata record from RPA, updates the valid flag for metadata record
and writes it back to RPA
 */
void
dcd::CoreDumpManager::setCoreDumpValid
    (
    bool valid
    )
{
    m_Metadata->setCoreValid(valid);

    if (!valid)
    {    
        //We don't want MEL to be generated if coredump is invalidated
        m_Metadata->setEventStatus(EVENT_CLEAR);
    }    
}    

/******************************************************************************
 * Set the dual full coredump flag
 */
void
dcd::CoreDumpManager::setDcdDualFullCoreDump
    (
    bool enabled
    )
{
    ftdcr::Control::setDualFullCoreDumpEnabled(enabled);
    m_DcdDualFullCoreDump = enabled;
}

/******************************************************************************
 * Return true if dual full coredump is enabled
 */
bool
dcd::CoreDumpManager::getDcdDualFullCoreDump
    (
    )
{
    return m_DcdDualFullCoreDump;
}

/**
 * Returns  coredump  tag
 */
void
dcd::CoreDumpManager::getCtlrSerialNumber
    (
    BYTE* serialNumber
    )
{
    m_Metadata->getCtlrSerialNumber(serialNumber);
}

/**
 * sets  coredump  tag
 */
void
dcd::CoreDumpManager::setCoreDumpTag
    (
    UINT32 tag
    )
{
    m_Metadata->setCoreTag(tag);
}

/**
 * Return true if DCD initialization is complete
 */
bool
dcd::CoreDumpManager::isInitComplete
    (
    ) const
{
    return m_InitComplete;
}

/******************************************************************************
DESCRIPTION:Fills the register data for "notes" section.  If the passed in taskId
is the currently active task, then read the current register values, otherwise
use the TCB contents.
PARAMETER:
    taskId:      task Id to get register data for.
    regSet:      gregset_t to populate
RETURNS: 
    Returns:     None
 */
void
dcd::CoreDumpManager::assignRegisters
    (
    VKI_TASK_ID taskId,
    gregset_t& regSet
    )
{
    // if this is the 'active' task, then we need to use the current register data,
    // otherwise use the TCB contents.
    // Fill in task registers
    REG_SET taskRegs;
    if( (taskId == VKI_TASK_MY_ID()) && (VKI_AM_I_ISR() == FALSE) )
    {
        VKI_MEMCPY(&taskRegs, &m_DcdSavedEntryTaskRegs, sizeof(REG_SET));
    }
    else if( (taskId == DCD_EXCEPTION_TASK_ID) && (m_DcdRegSet) )
    {
        VKI_MEMCPY(&taskRegs, m_DcdRegSet, sizeof(REG_SET));
    }
    else
    {
        taskRegsGet(taskId, &taskRegs);
    }

//* BeginGearsBlock Cpp HW_Processor_Pentium
    regSet.r_fs     = 0;
    regSet.r_es     = 0;
    regSet.r_ds     = 0;
    regSet.r_edi    = taskRegs.edi;
    regSet.r_esi    = taskRegs.esi;
    regSet.r_ebp    = taskRegs.ebp;
    regSet.r_isp    = 0;
    regSet.r_ebx    = taskRegs.ebx;
    regSet.r_edx    = taskRegs.edx;
    regSet.r_ecx    = taskRegs.ecx;
    regSet.r_eax    = taskRegs.eax;
    regSet.r_trapno = 0;
    regSet.r_err    = 0;
    regSet.r_eip    = reinterpret_cast<UINT32>(taskRegs.pc);
    regSet.r_cs     = 0;
    regSet.r_eflags = taskRegs.eflags;
    regSet.r_esp    = taskRegs.esp;
    regSet.r_ss     = 0;
    regSet.r_gs     = 0;
//* EndGearsBlock Cpp HW_Processor_Pentium
//* BeginGearsBlock Cpp HW_Processor_PPC
    //for(int i=0; i<32; i++)
    //{
        //regSet.r_gpr[i] = taskRegs.gpr[i];
    //}

    //regSet.r_msr      = taskRegs.msr;
    //regSet.r_lr       = taskRegs.lr;
    //regSet.r_cr       = taskRegs.cr;
    //regSet.r_xer      = taskRegs.xer;
    //regSet.r_ctr      = taskRegs.ctr;
    //regSet.r_pc       = taskRegs.pc;
//* EndGearsBlock Cpp HW_Processor_PPC
}

/******************************************************************************
DESCRIPTION: 
Wrapper to  get Coredump metadata information. SYMbol uses this to get information 
about coredump
 */
void
dcd::CoreDumpManager::getCoreDumpInfo
    (
    DPLCoreDumpInfoReturn* result
    ) 
{
    m_Metadata->getCoreDumpInfo(result);
}    

/******************************************************************************
DESCRIPTION: 
Wrapper to get the needs retrieval flag.
 */

bool 
dcd::CoreDumpManager::getNeedsRetrieval
    (
    ) const
{
    return m_Metadata->getNeedsRetrieval();
}

/******************************************************************************
DESCRIPTION: 
Wrapper to set the needs retrieval flag.
 */

void 
dcd::CoreDumpManager::setNeedsRetrieval
    (
    bool needsRetrieval
    ) 
{
    m_Metadata->setNeedsRetrieval(needsRetrieval);
}

/******************************************************************************
DESCRIPTION: 
Wrapper to get the needs backup flag.
 */

bool 
dcd::CoreDumpManager::getNeedsBackup
    (
    ) const
{
    return m_Metadata->getNeedsBackup();
}

/******************************************************************************
DESCRIPTION: 
Wrapper to set the needs backup flag.
 */

void
dcd::CoreDumpManager::setNeedsBackup
    (
    bool needsBackup
    )
{
    m_Metadata->setNeedsBackup(needsBackup);
}

/******************************************************************************
DESCRIPTION: 
Wrapper to get the address of the staged notes.
 */

UINT32
dcd::CoreDumpManager::getStagedNotesAddr
    (
    ) const
{
    return reinterpret_cast<UINT32>(m_NotesStagingBuffer);
}

/******************************************************************************
DESCRIPTION: 
Wrapper to get the address of notes in RPA memory.
 */

CADDR
dcd::CoreDumpManager::getRpaNotesAddr
    (
    ) const
{
    return getCoreDumpBase() + m_NotesOffset;
}

/******************************************************************************
DESCRIPTION: 
Wrapper to get the used notes size.
 */

UINT32
dcd::CoreDumpManager::getNotesSize
    (
    ) const
{
    return m_Metadata->getNotesSize();
}

/******************************************************************************
DESCRIPTION: 
Test helper function to corrupt region of RPA.
 */
void
dcd::CoreDumpManager::corruptRPARegion
    (
    int region
    )
{
    switch (region)
    {
    case METADATA:
    {
        rpaMemSet(getCoreDumpBase() + 0x10, '-', 4);
        break;
    }
    case ELF_CORE:
    {
        rpaMemSet(getCoreDumpBase() + getFirstSectionOffset() + 0x10, '-', 4);
        break;
    }
    case DQ:
    {
        CADDR addr = getCoreDumpDqAddress();
        rpaMemSet(addr + 0x10, '-', 4);
        break;
    }
    default:
        break;
    }
}


/******************************************************************************
DESCRIPTION:
Sets the shared RPA region base address and size for enhanced core dump
*/

void
dcd::CoreDumpManager::setSharedRegionInfo
    (
    CADDR secAddr,
    CADDR secSize
    )
{
    m_SharedRegionBase = secAddr;
    m_SharedRegionSize = secSize;
    dqvkiWrite(dcdDqWriter, DCD_SECONDARY_CACHE_INFO, m_SharedRegionBase, m_SharedRegionSize);
}

/******************************************************************************
DESCRIPTION:
Gets the shared region base address
*/
CADDR
dcd::CoreDumpManager::getSharedRegionBase
    (
    ) const
{
    return m_SharedRegionBase;
}

/******************************************************************************
DESCRIPTION: 
Test helper function to corrupt shared region cache base address 
type 1 - corrupt dcd local variable m_sharedRegionBase 
type 2 - corrupt address saved in core dump metadata 
 */
void
dcd::CoreDumpManager::corruptSharedRegionBase
    (
    int type
    )
{
    if (type == 1)
    {
        //* Change the local var holding secondaryCacheBase.
        //* This will cause a core dump capture to go to the wrong addr
        //* and should result in a address miscompare after reboot.
        m_SharedRegionBase += 0x200;
        m_SharedRegionSize -= 0x200;
    }
    else if (type == 2)
    {
        //* Change the shared region base address that is stored in
        //* metadata. This should cause a crc error after reboot
        //* and the core dump will be discarded.
        dcdCorruptSharedRegionBaseInMetadata = 1;
    }
}

//******************************************************************************
//******************************************************************************
// extern  C routines
//******************************************************************************
//******************************************************************************

extern "C" 
{

/******************************************************************************
DESCRIPTION:
External shell routine to update coredump valid flag
*/
void
dcdSetCoreValid
    (
    UINT32 tag
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setCoreDumpValid(tag ? true:false);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine to set dual full coredump flag
*/
void
dcdSetDualFullCoreDump
    (
    UINT32 enabled
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setDcdDualFullCoreDump(enabled ? true : false);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine to update coredump tag
*/
void
dcdSetTag
    (
    UINT32 tag
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setCoreDumpTag(tag);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/**
 * @param nr is 1 to set needsRetrieval to true; 0 to set it to 
 *           false.
 *  
 * Set the needsRetrieval field to true or false in the metadata
 * and in RPA memory. 
 */
void
dcdSetNeedsRetrieval
    (
    UINT32 nr
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setNeedsRetrieval(nr);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}   // dcdSetNeedsRetrieval()

/**
 * @param priority, Priority of core dump RPA region access
 *  
 * Set the DcdRpaAccessPriority, 0 is highest priority  
 */
void
dcdSetRpaAccessPriority
    (
    UINT32 priority
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setDcdRpaAccessPriority
            (static_cast<dcd::CoreDumpMgmtP::DcdRpaAccessPriority>(priority));
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}   // dcdSetRpaAccessPriority()

/******************************************************************************
DESCRIPTION:
External shell routine for showing DCD's private members.
*/
void
dcdShow
    (
    UINT32 level = 0
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->show(level);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine for showing CoreDumpMetadata private members.
 */
void
dcdShowMetadata
    (
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->showMetadata();
//* BeginGearsBlock Cpp Feature_CacheCoreDump
        dcd::CoreDumpMgr::getInstance()->showStorageRecord();
//* EndGearsBlock Cpp Feature_CacheCoreDump
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine for showing metadata from coredump RPA region
*/
void
dcdShowRPAMetadata
    (
    )
{
    if (!rpai::RegionMgr::doesInstanceExist())
    {
        VKI_PRINTF("RPAI instance does not exist.\n");
        return;
    }
    if (!dcd::CoreDumpMgr::doesInstanceExist() || !dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        VKI_PRINTF("DCD instance does not exist or is not initialized.\n");
        return;
    }

    dcd::CoreDumpMgr::getInstance()->showRPAMetadata();
//* BeginGearsBlock Cpp Feature_CacheCoreDump
    dcd::CoreDumpMgr::getInstance()->showRPAStorageRecord();
//* EndGearsBlock Cpp Feature_CacheCoreDump
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setEventStatus() to be
    invoked from shell.
PARAMETER:
    majorEvent: I/P parameter. Set the coredump event status
************************************************************************************/
void
dcdSetEventStatus
    (
    dcd::CoreDumpManagement::DumpMajorEvent majorEvent
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setEventStatus(majorEvent);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setCheckpoint() to be
    invoked from shell.
PARAMETER:
    checkPoint : I/P parameter. Set the coredump check point
************************************************************************************/
void
dcdSetCheckPoint
    (
    dcd::CoreDumpManagement::DumpCheckpoint checkPoint
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setCheckPoint(checkPoint);
    }
    else
    {
        VKI_PRINTF("DCD initialization not completed.\n");
    }
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setOverwriteThresholdTime() to be
    invoked from shell.
PARAMETER:
    overwriteThresholdTime: I/P parameter. Get Threshold Time after which Coredump 
    can be overwritten
************************************************************************************/
void
dcdSetOverwriteThresholdTimeSec
    (
     UINT32 overwriteThresholdTime
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        dcd::CoreDumpMgr::getInstance()->setOverwriteThresholdTime(overwriteThresholdTime);
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
    }
}

//* BeginGearsBlock Cpp Feature_CacheCoreDump
/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method clearStorageRecord() to be
    invoked from shell.
************************************************************************************/
void
dcdClearStorageRecord
    (
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->clearStorageRecord();
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRValid() to be
    invoked from shell.
PARAMETER:
    valid: The value for the valid flag
************************************************************************************/
void
dcdSetSRValid
    (
    bool valid
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setSRValid(valid);
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRVersion() to be
    invoked from shell.
PARAMETER:
    version: The value for the version number
************************************************************************************/
void
dcdSetSRVersion
    (
    UINT32 version
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setSRVersion(version);
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRMode() to be
    invoked from shell.
PARAMETER:
    mode: The value for the storage mode
************************************************************************************/
void
dcdSetSRMode
    (
    dcd::CcdStorageMode mode
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setSRMode(mode);
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRVolumeSsid() to be
    invoked from shell.
PARAMETER:
    ssid: The value for the volume ssid
************************************************************************************/
void
dcdSetSRVolumeSsid
    (
    UINT32 ssid
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setSRVolumeSsid(ssid);
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRAltVolumeSsid() to be
    invoked from shell.
PARAMETER:
    ssid: The value for the alternate controller's volume ssid
************************************************************************************/
void
dcdSetSRAltVolumeSsid
    (
    UINT32 ssid
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        dcd::CoreDumpMgr::getInstance()->setSRAltVolumeSsid(ssid);
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}

/*******************************************************************************
DESCRIPTION:
    This is the C wrapper for class method setSRNetworkPath() to be
    invoked from shell.
PARAMETER:
    path: The value for the storage network path
************************************************************************************/
void
dcdSetSRNetworkPath
    (
    const char* path
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist() && dcd::CoreDumpMgr::getInstance()->isInitComplete())
    {
        if (!dcd::CoreDumpMgr::getInstance()->setSRNetworkPath(path))
            VKI_PRINTF("dcdSetSRNetworkPath failed.\n");
    }
    else
        VKI_PRINTF("DCD initialization not completed.\n");
}
//* EndGearsBlock Cpp Feature_CacheCoreDump

/******************************************************************************
DESCRIPTION:
External shell routine to get the address of the staged notes
 */
UINT32
dcdGetStagedNotesAddr
    (
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        return dcd::CoreDumpMgr::getInstance()->getStagedNotesAddr();
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
        return -1;
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine to get the low address of notes in RPA memory
 */
UINT32
dcdGetRpaNotesAddrLo
    (
    )
{
    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        // high 32 bits knowingly truncated from RPA address
        return static_cast<UINT32>(dcd::CoreDumpMgr::getInstance()->getRpaNotesAddr());
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
        return -1;
    }
}

/******************************************************************************
DESCRIPTION:
External shell routine to get the used size of the notes
 */
UINT32
dcdGetNotesSize()
{
    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        return dcd::CoreDumpMgr::getInstance()->getNotesSize();
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
        return -1;
    }
}

/*******************************************************************************
DESCRIPTION:
    This Sets the Overwite Threshold Time to 0 seconds and captures
 a full and Alt
    Core Dump. This is for use with the dq triggers.
    invoked from shell.
************************************************************************************/
extern "C"
void
dcdTriggerAllCoreDump
    (
    )
{
    dcdSetOverwriteThresholdTimeSec(0);
    VKI_REBOOT(CORE_DUMP_FULL_TRIGGER|CORE_DUMP_ALT_TRIGGER,0);
}


/**
 * Displays the help for dcd ctlr shell cmds.
 */
void
dcdHelp
    (
    )
{
    VKI_PRINTF("=============================\n");
    VKI_PRINTF("DCD Controller Shell Commands\n");
    VKI_PRINTF("=============================\n");
    VKI_PRINTF("dcdSetCoreValid [0=invalid|1=valid]\n");
    VKI_PRINTF("dcdSetNeedsRetrieval [0=false|1=true]\n");
    VKI_PRINTF("dcdSetTag [0=reset|>0=newTagValue]\n");
    VKI_PRINTF("dcdShow [0=basic|98=moduleInfo|99=detailed]\n");
    VKI_PRINTF("dcdShowMetadata\n");
    VKI_PRINTF("dcdSetRpaAccessPriority [0=highest|1=Backup/Restore]\n"); 
    VKI_PRINTF("dcdShowRPAMetadata\n");
    VKI_PRINTF("dcdSetEventStatus [0=EventClear|1=Captured|2=NotCaptured|3=OverWritten|4=CaptureFailed]\n");
    VKI_PRINTF("dcdSetCheckPoint [0=NotStarted|1=CollectingData|2=WritingCore|3=WritingDq|4=StoringDqLblTbl|\n");
    VKI_PRINTF("                  5=StoringDqPrintparams|6=StoringDqBody|7=WritingDqBody|\n");
    VKI_PRINTF("                  8=WritingDqFormatStrings|9=CaptureComplete]\n");
    VKI_PRINTF("dcdSetOverwriteThresholdTimeSec [TimeInSeconds]\n"); 
    VKI_PRINTF("dcdCorruptRPARegion [0=Elf|1=Dq|2=Metadata\n");
    VKI_PRINTF("dcdGetStagedNotesAddr\n");
    VKI_PRINTF("dcdGetRpaNotesAddrLo\n");
    VKI_PRINTF("dcdGetNotesSize\n");
    VKI_PRINTF("dcdTriggerAllCoreDump");
    VKI_PRINTF("\n");
}
/**
 * Test function to corrupt RPA metadata.  
 */
void 
dcdCorruptRPARegion
    (
    int region
    )
{

    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        dcd::CoreDumpMgr::getInstance()->corruptRPARegion(region);
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
    }
}

/**
 * Test function to corrupt shared region base address 
 */
void 
dcdCorruptSharedRegionBase
    (
    int type
    )
{

    if (dcd::CoreDumpMgr::doesInstanceExist())
    {
        dcd::CoreDumpMgr::getInstance()->corruptSharedRegionBase(type);
    }
    else
    {
        VKI_PRINTF("DCD instance does not exist.\n");
    }
}

}
