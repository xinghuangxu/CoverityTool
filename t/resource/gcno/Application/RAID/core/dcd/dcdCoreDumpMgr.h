/*******************************************************************************

NAME            dcdCoreDumpMgr.h
VERSION         %version: 76 %
UPDATE DATE     %date_modified: Thu Oct 30 09:28:04 2014 %
PROGRAMMER      %created_by:    gkenneth %

        Copyright 2010-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION:
Private header for the CoreDumpManager singleton.

*******************************************************************************/

#ifndef __INCdcdCoreDumpMgr
#define __INCdcdCoreDumpMgr

#include "dcdCoreDumpMetadata.h"
#include "dcdCoreDumpMgmtP.h"
#include "dcdCoreDumpModuleInfo.h"
#include "dcdElf.h"
#include "dcdRebootReason.h"
#include "dcdStorageRecord.h"
#include "rpaLib.h"
#include "utlSingletonMgr.h"
#include "utlTemplateUtilities.h"
#include "vkiWrap.h"
#include <list>

void dcdCoreDumpMgrDq();

namespace dcd
{
class CoreDumpManager : public CoreDumpManagementP
{
public:
    enum FileType
    {
        BLOB_FILE = 0,
        ELF_FILE  = 1,
        DQ_FILE   = 2
    };

    CoreDumpManager();
    virtual ~CoreDumpManager();
    CoreDumpManager(const CoreDumpManager&);              // Not implemented
    CoreDumpManager operator=(const CoreDumpManager&);    // Not implemented

    //*******************************************************************************
    // CoreDumpMgmt interface
    //*******************************************************************************
    bool   triggerCoreDump(DumpType dumpType, int rebootReason = 0);
    void   addCoreDumpRegion(void* address, UINT32 size, DumpType type);
    inline bool   isEcdEnabled() const;
    void   registerCoreDumpListener(CoreDumpListener* listener);
    void   handlePICcdTrigger(SSID volSsid, LBA64 blockNo, UINT iocChannel, PIIoType ioType);
    void   handleNonIOCcdTrigger();
    UINT32 getStagedNotesAddr() const;
    CADDR  getRpaNotesAddr() const;
    UINT32 getNotesSize() const;

    //*******************************************************************************
    // CoreDumpMgmtP interface
    //*******************************************************************************
    void   setCoreDumpValid(bool valid);
    bool   getCoreDumpValid() const;
    void   setDcdDualFullCoreDump(bool enabled);
    bool   getDcdDualFullCoreDump();
    UINT64 getCoreDumpSizeInMem() const;
    UINT64 getCoreDumpSizeOnDisk() const;
    UINT32 getFirstSectionOffset() const;
    DumpType getCoreDumpType() const;
    bool   isValidFullCoreDump() const;
    bool   isValidLockCoreDump() const;
    bool   isValidCcd() const;
    bool   isCcdOverwriteAllowed();
    UINT32 getCoreDumpTag() const;
    void   setCoreDumpTag(UINT32 tag);
    void   getCtlrSerialNumber(BYTE* serialNumber);
    void   getCoreDumpInfo(DPLCoreDumpInfoReturn* result);
    void   setFileAndLine(COREDUMPINFO* info);
    void   setPanicString(const char* panicString);
    void   setDcdRegSet(REG_SET* regSet);
    bool   getNeedsRetrieval() const;
    void   setNeedsRetrieval(bool);
    void   setEventStatus(DumpMajorEvent majorEvent);
    dcd::CoreDumpMgmt::DumpMajorEvent getCoreDumpEventStatus() const;
    void   getCoreDumpEventDetails(COREDUMP_EVENT_DETAIL* coredumpEventDetail);
    void   setOverwriteThresholdTime(UINT32 overwriteThresholdTime);
    UINT32 getOverwriteThresholdTime() const;
    UINT32 getCoreDumpTime() const;
    void   setCoreDumpTime(UINT32 timestamp);
    dcd::ROCoreDumpMetadataRecord* getMetadata() const;
    void   setMetadata(dcd::ROCoreDumpMetadataRecord* metadata);
    dcd::StorageRecord* getStorageRecord() const;
    void   setStorageRecord(dcd::StorageRecord* record);
    UINT32 getRebootReason() const;
    void   setRebootReason(UINT32 rebootReason);
    void   setBlockSize(UINT32 blockSize);
    void   setCcdType(dcd::CcdType ccdType);
    void   setDcdRpaAccessPriority(DcdRpaAccessPriority priority);
    void   setSharedRegionInfo(CADDR secAddr,CADDR secSize);
    CADDR  getMetadataSharedRegionBase() const;
    bool   getNeedsBackup() const;
    void   setNeedsBackup(bool);
    void   clearStorageRecord();
    void   setCompressed(bool compressed);
    bool   isCompressed() const;
    bool   isCcd() const;
    void   setCoreDumpSizeInMem(UINT64 size);
    void   setCoreDumpSizeOnDisk(UINT64 size);
    bool   isSRValid() const;
    void   setSRValid(bool valid);
    UINT32 getSRVersion() const;
    void   setSRVersion(UINT32 version);
    void   setSRState(CcdState state);
    CcdState getSRState() const;
    void   setSRMode(CcdStorageMode storageMode);
    CcdStorageMode getSRMode() const;
    void   setSRVolumeSsid(UINT32 ssid);
    UINT32 getSRVolumeSsid() const;
    void   setSRAltVolumeSsid(UINT32 ssid);
    UINT32 getSRAltVolumeSsid() const;
    bool   setSRNetworkPath(const char* path);
    const char* getSRNetworkPath();
    bool   dcdRpaCopyCoreSectionWithCrc(CADDR dst, CADDR src, UINT32 count, bool carryForwardCrc,
                                        UINT32* crcValue,
                                        DcdRpaAccessPriority pri = NON_CAPTURE_PRIORITY);
    bool findSection(CoreSectionType secType, dcd::SectionMetadataRecord* section,
                     CADDR& offset) const;
    bool getSectionAttributes(CoreSectionType secType, CADDR& offset, UINT64& size) const;

    //*******************************************************************************
    // CoreDumpMgr public method
    //*******************************************************************************
    void initialize();
    bool moduleInfoStore(UINT32 moduleId);
    CADDR getCoreDumpDqAddress() const;
    UINT64 getDqLength() const;
    void writeDqBuffer(UINT32 srcBuf, UINT32 bytesToWrite);
    void writeError(DumpError errorType);
    void setCheckPoint(DumpCheckpoint checkPoint);
    bool isInitComplete()const;
    CADDR  getSharedRegionBase() const;

    inline UINT32 getMetadataSize() const;
    inline UINT32 getDqRegionFreeSize() const;

    /* This is a soft limit. Anything greater than the size of region RPA_DQ eats into our 20 MB 
     * overhead, which is primarily reserved space for future use.
     * The DQ segment consists of:
     * Label tables, print parameters, "trace" (~5MB) & "traceHP"(~1MB) bodies, and format strings.
     * The "trace" and "traceHP" bodies combined are the size of region RPA_DQ, so we are using
     * 4 MB of the reserved space.
     */
    static const UINT32 MAX_COREDUMP_DQ_SEGMENT_SIZE  = 10 MB;
    static const UINT32 COREDUMP_OVERWRITE_TIME       = (48*60*60); //Overwrite threshold of 48hours
    //*******************************************************************************
    // Public display methods for debugging purposes
    //*******************************************************************************
    void showMetadata() const;
    void showStorageRecord() const;
    void showRPAMetadata() const;
    void showRPAStorageRecord() const;
    void show(UINT32 level) const;
    void corruptRPARegion(int region);
    void corruptSharedRegionBase(int type);

private:
    class SegmentDescriptor
    {
    public:
        SegmentDescriptor();
        SegmentDescriptor(UINT64 baseAddr, UINT64 size);
        ~SegmentDescriptor();

        UINT64 getAddr() const;
        UINT64 getSize() const;
        void setAddr(UINT64 baseAddr);
        void setSize(UINT64 size);

        void show() const;

    private:
        UINT64 m_BaseAddr;
        UINT64 m_Size;
    };

    // A 128 KB region of processor memory is alllocated to stage the notes to RPA
    // memory, to avoid running past RPA page boundaries as may happen if it were built
    // in-place.  At implementation, notes were normally approx 16KB, and so 128 KB
    // should be more than enough space to ensure this for the foreseeable future.
    // We may exceed this size if we get more than 900 tasks... which shouldn't
    // be for a very long time.
    static const UINT32 NOTES_STAGING_REGION_SIZE   = (128 KB);
    static const UINT32 SHOW_BASICS        =  0;
    static const UINT32 SHOW_MODULE_INFO   = 98;
    static const UINT32 SHOW_DETAILED_INFO = 99;

    // The first loaded module comes in at just after 31 MB.  The segment prior to this contains
    // the kernel.  Avoid overlapping with loaded modules for sanity in post-analysis tools.
    static const UINT32 KERNEL_SEGMENT_END            = 31 MB;
    static const UINT32 STACK_SEGMENT_ALIGNMENT_BYTES = 512;
    // Restrict other module holding RPA engine for long, should be greater than RDM size (5MB)
    static const UINT32 NON_DCD_RPA_ACCESS_SIZE = 5 MB;

    static SegmentDescriptor m_StackSegments[MAX_TASKS];
    static VKI_TASK_ID m_TaskIds[MAX_TASKS];

    //Keep track of segments used during full coredump collection
    std::vector<SegmentDescriptor*>* m_FullSegmentVector;

    //Keep track of segments used during lock coredump collection
    std::vector<SegmentDescriptor*>* m_AltSegmentVector;

    //Keep track of memory segmenats registered with coredump
    std::vector<SegmentDescriptor*>* m_AltClientSegmentVector;

    RWModuleInfo::ModuleInfoVector m_ModuleInfoVector;
    RWCoreDumpMetadataRecord* m_Metadata;
    StorageRecord* m_StorageRecord;
    CADDR   m_DqWritePtr;
    UINT32  m_DqWriteIndex;
    UINT32  m_OverwriteThresholdTimeSec;
    REG_SET m_DcdSavedEntryTaskRegs;
    int*    m_LblTblUsedPtr;
    bool    m_InitComplete; //initialize() has completed
    bool    m_CoreDumpInProgress;
    char*   m_NotesStagingBuffer;
    UINT32  m_NotesOffset;

    CADDR   m_SharedRegionBase; // Shared RPA region base for enhanced core dump
    CADDR   m_SharedRegionSize; // Shared RPA region size for enhanced core dump

    // local copies of panic string and assert file/line number info pointers maintained
    // Required to keep from holding stale information in metadata record.
    COREDUMPINFO*                            m_DcdInfo;
    const char*                              m_DcdPanicString;
    REG_SET*                                 m_DcdRegSet;
    DcdRpaAccessPriority                     m_DcdRpaAccessPriority;

    bool                                     m_DcdDualFullCoreDump; // Used for CCD and testing
    std::list<CoreDumpListener*>             m_Listeners;

    CADDR  getCoreDumpBase() const;
    CADDR  getCoreDumpEnd() const;
    CADDR  getRegionAddr() const;
    void addCoreDumpRegion(void* address, UINT32 size, DumpType type, bool addToHead);
    bool copyWithCrc(CADDR dst, CADDR src, UINT32 count, bool carryForwardCrc, UINT32* crcValue);
    bool computeCrc(CADDR src, UINT32 count, bool carryForwardCrc, UINT32* crcValue) const;
    bool verifyCoreDumpCrc(bool ecdCrc);
    void createElfHeader32(Elf32_Ehdr* eheader, UINT numSegs);
    void createElfHeader64(Elf64_Ehdr* eheader, UINT numSegs);
    bool createLoadProgramHeader32(Elf32_Phdr* pHeader, SegmentDescriptor* seg, UINT32 startOffset);
    bool createLoadProgramHeader64(Elf64_Phdr* pHeader, SegmentDescriptor* seg, UINT64 startOffset);
    bool updateAltSegmentVector();
    int getStackSegments();
    UINT32 createNotes(char* pBuffer);
    bool createNoteProgramHeader32(Elf32_Phdr* pHeader, UINT32 fileOffset, UINT32 notesSize);
    bool createNoteProgramHeader64(Elf64_Phdr* pHeader, UINT64 fileOffset, UINT64 notesSize);
    bool captureCoreDump(DumpType type, int rebootReason);
    void moduleInfoStoreInit();
    UINT32 copyTraceDq(CADDR dqOffset, CADDR coreDumpEndAddr);
    UINT32 copyTraceDqBody(UINT32 dqSize, CADDR dqAddr);
    void assignRegisters(VKI_TASK_ID taskId, gregset_t& regSet);
    bool overwriteCoredump(int rebootReason);

    friend class utl::SingletonMgr<CoreDumpManager, CoreDumpManagement>;
    friend class utl::SingletonMgr<CoreDumpManager, CoreDumpManagementP>;

};
typedef utl::SingletonMgr<CoreDumpManager, CoreDumpManagement, CoreDumpManagementP> CoreDumpMgr;

}

/**
 * Returns free memory size of coredumpDq region.
 */
UINT32
dcd::CoreDumpManager::getDqRegionFreeSize
    (
    ) const
{
    return (MAX_COREDUMP_DQ_SEGMENT_SIZE - m_DqWriteIndex);
}

/**
 * Returns metadata size.
 */
UINT32
dcd::CoreDumpManager::getMetadataSize
    (
    ) const
{
   return sizeof(RWCoreDumpMetadataRecord);
}
/**
  * Returns true if EnhancedCoreDump is enabled
  * Otherwise, return false
  */
bool
dcd::CoreDumpManager::isEcdEnabled
    (
    ) const
{
//* BeginGearsBlock Cpp Feature_EnhancedCoreDump
    return true;
//* ElseGearsBlock Cpp Feature_EnhancedCoreDump
    //return false;
//* EndGearsBlock Cpp Feature_EnhancedCoreDump
}

#endif        /* End of __INCdcdCoreDumpMgr */

