/*******************************************************************************

NAME            dcdSod.cc
SUMMARY         %description%
VERSION         %version: 5 %
UPDATE DATE     %date_modified: Fri Jul 12 15:29:49 2013 %
PROGRAMMER      %created_by:    agreg %

        Copyright 2010-2013 NetApp, Inc. All Rights Reserved.

DESCRIPTION:
Instantiates and initializes the dcd component.

*******************************************************************************/

#include "dcdCoreDumpMgr.h"
#include "dcdSod.h"
#include "vkiWrap.h"
//* BeginGearsBlock Cpp Feature_RPARefactor
#include "rpaiRegionMgr.h"
//* EndGearsBlock Cpp Feature_RPARefactor

extern "C"
{
void dcdDqInit();
}

void
dcd::instantiate
    (
    )
{
    dcdDqInit();

    try
    {
        dcd::CoreDumpMgr::createInstance();
    }
    catch(std::exception& e)
    {
        VKI_CMN_ERR(CE_PANIC, "Caught %s instantiating DCD", e.what());
    }
}

void
dcd::initialize
    (
    )
{
    // only initialize if rpa core dump region has been allocated
//* BeginGearsBlock Cpp !Feature_RPARefactor
    //if (getCoreDumpMemorySize())
//* EndGearsBlock Cpp !Feature_RPARefactor
//* BeginGearsBlock Cpp Feature_RPARefactor
    if (rpai::RegionMgr::getInstance()->getLength(RPA_CORE_DUMP))
//* EndGearsBlock Cpp Feature_RPARefactor
        dcd::CoreDumpMgr::getInstance()->initialize();
    else
        VKI_CMN_ERR(CE_ERROR, "Core Dump memory size is zero\n");
}

