bidLearn.o: bidLearn.cc \
  $(BASEDIR)/.common/CommonDefs.h \
  $(BASEDIR)/Platform/System/BSP/proj/VxWorksDefs.h \
  $(BASEDIR)/Platform/Lib/vkiWrap.h \
  $(BASEDIR)/Platform/Lib/cmnDefs.h \
  $(BASEDIR)/Platform/Lib/cmnOS.h \
  /soft/windriver/pne/3.9.3.3-1.1.1/VSBs/nehalem_smp.3/h/config/vsbConfig.h \
  /soft/windriver/pne/3.9.3.3-1.1.1/VSBs/nehalem_smp.3/h/config/autoconf.h \
  $(BASEDIR)/Platform/System/BSP/proj/prjComps.h \
  $(BASEDIR)/Platform/Lib/cmnIO.h \
  $(BASEDIR)/Platform/Lib/cmnIOP.h \
  $(BASEDIR)/Platform/Lib/hwCpu.h \
  $(BASEDIR)/Platform/Lib/hwInline.h \
  $(BASEDIR)/Platform/Lib/hwCpuP.h \
  $(BASEDIR)/Platform/Lib/hwCpuCmn.h \
  $(BASEDIR)/Platform/Lib/bcmHardware.h \
  $(BASEDIR)/Platform/Lib/cmnPCI.h \
  $(BASEDIR)/Platform/Lib/cmnTypes.h \
  $(BASEDIR)/Platform/Lib/cmnDefines.h \
  $(BASEDIR)/Platform/Lib/bcmDefines.h \
  $(BASEDIR)/Platform/Lib/bcmProductDefs.h \
  $(BASEDIR)/Platform/Lib/Platform.h \
  $(BASEDIR)/Platform/System/BSP/itl_nehalem/xbb.h \
  $(BASEDIR)/Platform/Lib/flashDefs.h \
  $(BASEDIR)/Platform/Lib/flashOrg.h \
  $(BASEDIR)/Platform/Lib/i2cAddrs.h \
  $(BASEDIR)/Platform/Lib/platformRegs.h \
  $(BASEDIR)/Platform/System/BSP/itl_nehalem/configInum.h \
  $(BASEDIR)/Platform/Lib/bcmSystemP.h \
  $(BASEDIR)/Platform/Lib/bcmAdapter.h \
  $(BASEDIR)/Platform/Lib/modelConfigCmn.h \
  $(BASEDIR)/Platform/Lib/sysErrLog.h \
  $(BASEDIR)/Platform/Lib/sysNvsramLib.h \
  $(BASEDIR)/Platform/Lib/sysExtLib.h \
  $(BASEDIR)/Platform/Lib/cmnField.h \
  $(BASEDIR)/Platform/Lib/cmnFormat.h \
  /soft/windriver/pne/3.9.3.3-1.1.1/components/ip_net2-6.9/vxcoreip/include/net/uio.h \
  /soft/windriver/pne/3.9.3.3-1.1.1/components/ip_net2-6.9/vxcoreip/include/sys/cdefs.h \
  /soft/windriver/pne/3.9.3.3-1.1.1/components/ip_net2-6.9/vxcoreip/include/netVersion.h \
  $(BASEDIR)/Platform/Lib/psvLib.h \
  $(BASEDIR)/Platform/Lib/nmiLib.h \
  $(BASEDIR)/Platform/Lib/private/sxVKI.h \
  $(BASEDIR)/Platform/Lib/hwCpu.h \
  $(BASEDIR)/Platform/Lib/dbgMonLib.h \
  $(BASEDIR)/Platform/Lib/private/vkiMisc.h \
  $(BASEDIR)/Platform/Lib/private/vkiStr.h \
  $(BASEDIR)/Platform/Lib/private/vkiTime.h \
  $(BASEDIR)/Platform/Lib/private/vkiTask.h \
  $(BASEDIR)/Platform/Lib/private/vkiIntr.h \
  $(BASEDIR)/Platform/Lib/private/vkiSync.h \
  $(BASEDIR)/Platform/Lib/private/sxVKI.h \
  $(BASEDIR)/Platform/Lib/private/vkiTimer.h \
  $(BASEDIR)/Platform/Lib/private/vkiFile.h \
  $(BASEDIR)/Platform/Lib/private/vkiFmtIO.h \
  $(BASEDIR)/Platform/Lib/private/vkiMisc.h \
  $(BASEDIR)/Platform/Lib/private/vkiMultiCore.h \
  $(BASEDIR)/Platform/Lib/private/vkiMem.h \
  $(BASEDIR)/Platform/Lib/private/vkiState.h \
  bidLearn.h bidBattery.h bidBatteryComponentListener.h bidLib.h \
  $(BASEDIR)/Application/RAIDLib/bidBatteryStatus.h \
  $(BASEDIR)/Platform/Lib/cmnTypes.h \
  $(BASEDIR)/Platform/Lib/bcmDiscreteLines.h \
  $(BASEDIR)/Platform/Lib/bcmDiscreteLinesP.h \
  $(BASEDIR)/Platform/Lib/discreteLineTableP.h \
  $(BASEDIR)/Platform/Lib/subsystemLinesP.h \
  $(BASEDIR)/Platform/Lib/tempSensorMgr.h \
  $(BASEDIR)/Platform/Lib/discreteLinesP.h \
  $(BASEDIR)/Platform/Lib/private/supportCru.h \
  $(BASEDIR)/Platform/Lib/private/bbu.h \
  $(BASEDIR)/Platform/Lib/i2c.h \
  $(BASEDIR)/Platform/Lib/vkiWrap.h \
  $(BASEDIR)/Application/RAIDLib/utlTimerMgmt.h \
  $(BASEDIR)/Application/RAIDLib/utlSingletonMgr.h \
  $(BASEDIR)/Platform/Lib/assert.h \
  $(BASEDIR)/Application/RAIDLib/utlSingletonMgmt.h \
  $(BASEDIR)/Application/RAIDLib/utlTemplateUtilities.h \
  $(BASEDIR)/Application/RAIDLib/utlDll.h \
  $(BASEDIR)/Application/RAIDLib/utlStaticString.h \
  $(BASEDIR)/Application/RAIDLib/utlServerCore.h \
  bidDerivedCharger.h bidCharger.h \
  $(BASEDIR)/Platform/Lib/i2c.h \
  $(BASEDIR)/Application/RAIDLib/utlListenerMgr.h \
  $(BASEDIR)/Application/RAIDLib/utlCoreGroup.h \
  $(BASEDIR)/Application/RAIDLib/utlScopeGuard.h \
  $(BASEDIR)/Application/RAIDLib/utlExceptions.h \
  $(BASEDIR)/Application/RAIDLib/utlFunctionTraits.h \
  $(BASEDIR)/Application/RAIDLib/utlListenerMgmt.h \
  $(BASEDIR)/Application/RAIDLib/utlMapUtils.h \
  $(BASEDIR)/Application/RAIDLib/utlMapUtilities.h \
  $(BASEDIR)/Application/RAIDLib/utlMulticoreLocksTracer.h \
  $(BASEDIR)/Application/RAIDLib/utlPrioritySet.h \
  $(BASEDIR)/Application/RAIDLib/utlSingletonMgr.h \
  bidDerivedGasGauge.h bidGasGauge.h bidLog.h bidLogTemperature.h \
  $(BASEDIR)/Platform/Lib/bcmIOPathInfo.h \
  $(BASEDIR)/Application/RAIDLib/utlShowableObject.h \
  $(BASEDIR)/Application/RAIDLib/utlFilter.h \
  $(BASEDIR)/Application/RAIDLib/utlTypeInfo.h \
  $(BASEDIR)/Platform/Lib/lblTbl.h \
  bidTrace.h \
  $(BASEDIR)/Platform/Lib/dqvki.h \
  $(BASEDIR)/Platform/Lib/dq.h \
  $(BASEDIR)/Platform/Lib/lblTbl.h
