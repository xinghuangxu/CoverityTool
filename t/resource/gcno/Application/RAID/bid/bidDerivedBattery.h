/*******************************************************************************

NAME            bidBBUSmartSingle.h
VERSION         %version: WIC~44 %
UPDATE DATE     %date_modified: Thu Oct 30 09:05:37 2014 %
PROGRAMMER      %created_by:    zackeryf %

        Copyright 2008-2014 NetApp, Inc. All Rights Reserved.

DESCRIPTION: Matterhorn/Snowmass/Pikes Peak/Soyuz/Tahoe Battery class definition

*******************************************************************************/


#ifndef __INCbidBBUSmartSingle
#define __INCbidBBUSmartSingle

#include "vkiWrap.h"

#include "bidBattery.h"
#include "bidCharger.h"
#include "bidGasGauge.h"

namespace bid
{
    // Various smart bbu related thresholds
    // Since Gears is doing the variation and thus Matterhorn is not used as part of
    // the enumerations so that base class can use the same name with different
    // values.
    enum BBUThreshold
    {
        I2C_DEBOUNCE            = 3,

        SYSTEM_LOAD             = 38,
        MINIMUM_APPL_HOLDTIME   = 319,
        REPLACEMENT_HOLDTIME    = 545,

        DEFAULT_TEMP_THRESHOLD = 60,   // degree Celsius(HW suggestion)
        RESUME_TEMP_DELTA      = 5,    // 5 degree Celsius(HW suggestion)

        DEFAULT_LEARN_INTERVAL = 8,    // 8 weeks for Matterhorn
        DEFAULT_LEARN_PERCENT  = 60,    // Default learn % of full charge
        MAXIMUM_LEARN_PERCENT  = 90,    // Maximum learn % of full charge

        MAX_CHARGING_INTERVAL  = (150 * ONE_MIN_IN_SEC), // 2.5 hours
        RETRY_RESTING_INTERVAL = (150 * ONE_MIN_IN_SEC)  // 2.5 hrs
    }   ;

    enum BBUDeepLearnThreshold
    {
        DEEP_COMMAND_TIMER       = ( 6 * ONE_MIN_IN_SEC),   // 6 mins
        DEEP_REST_INTERVAL       = ( 425 * ONE_MIN_IN_SEC), // 7 hours 5 mins
    }    ;

    enum LearnPhaseInterval
    {
        DISCHARGE_TO_EMPTY_THRESHOLD      = ( 50 ), // default to (Full discharge if RelativeSOC < 50%)

        CHARGING_FROM_EMPTY_INTERVAL      = ( 300 * ONE_MIN_IN_SEC), // 5.0 hrs
        DISCHARGING_TO_EMPTY_INTERVAL     = ( 300 * ONE_MIN_IN_SEC), // 5.0 hrs

        RETRY_INITIAL_DISCHARGE_THRESHOLD = ( 10 ), // initial discharge to (RelativeSOC - 10%)
        RETRY_DISCHARGING_GROWTH          = ( 5 ), // repeatidly added to discharge threshold
        RETRY_DISCHARGING_INTERVAL        = ( 150 * ONE_MIN_IN_SEC), // 2.5 hrs
        RETRY_RECOVERING_INTERVAL         = ( 15  * ONE_MIN_IN_SEC), // 15 mins
        RETRY_CHARGING_INTERVAL           = ( 150 * ONE_MIN_IN_SEC), // 2.5 hrs
        RETRY_BALANCE_ATTEMPTS            = ( 6 ), // Number of Retries

        LEARN_PREPARING_INTERVAL          = ( 150 * ONE_MIN_IN_SEC ), // 2.5 hrs
        LEARN_CALIBRATING_INTERVAL        = ( 7 * ONE_MIN_IN_SEC ),   // 7 mins
        LEARN_CALIBRATION_CMD_DELAY       = (   4 * ONE_MIN_IN_SEC ), // 4 mins
        LEARN_DISCHARGE_CMD_DELAY         = (                   20 ), // 20 secs
        LEARN_DISCHARGING_INTERVAL        = ( 300 * ONE_MIN_IN_SEC ), // 5.0 hrs
        LEARN_RECOVERING_INTERVAL         = ( 15 * ONE_MIN_IN_SEC ),  // 15 mins
        LEARN_CHARGING_INTERVAL           = ( 150 * ONE_MIN_IN_SEC ), // 2.5 hrs
        LEARN_RELAXING_INTERVAL           = (  10 * ONE_MIN_IN_SEC ), // 10 mins


        LEARN_DEEP_MIN_CALIBRATING_TIME   = (  12 * ONE_HOUR_IN_SEC), // 12  hrs
        LEARN_DEEP_CALIBRATING_INTERVAL   = (  24 * ONE_HOUR_IN_SEC), // 24  hrs
        LEARN_DEEP_RELAXING_INTERVAL      = (  10 * ONE_HOUR_IN_SEC), // 10  hrs

        LEARN_SOD_DELAY_INTERVAL          = (  24 * ONE_HOUR_IN_SEC), // 24  hrs

        LEARN_CYCLE_ATTEMPTS              = ( 3 ), // Number of Retries
        LEARN_CELL_IMBALANCE_THRESHOLD    = ( 10 ) // 10 mV
    }   ;

    class DerivedBattery    : public Battery
    {
    public:
                        DerivedBattery(BCM_READ_LINE_DATA_TABLE*    bcmTable,
                                       BYTE*                        bcmIndex,
                                       BYTE                         bcmFuncsCt,
                                       GasGauge*                    gasGauge,
                                       Charger*                     charger,
                                       BatterySlot                  slot);

        virtual         ~DerivedBattery();

        virtual Charger*     getChargerInstance();
        virtual GasGauge*    getGasGaugeInstance();

        virtual void    showStates(BatteryType type);

        virtual const   LearnMethod getLearnMethod();

        virtual bool    updateRegisterStates();
        virtual void    updateFunctionalStates();
        virtual void    updateFeatureStates();
        virtual void    updateLearnStates();
        void            updateLogTemp();

        bool            updateIsFailed();
        bool            updateCharging();
        bool            updateSleeping();
        virtual void    resetCharging();

        virtual bool    isUsable();
        virtual bool    isSmart() const;

        bool            isErrorRecoveryExhausted();

        virtual bool    isUsableDuringLearnCycle() const;
        virtual bool    isReadyForLearnCycle();
        virtual bool    isLearnCycleCompleted();

        virtual void    startLearnCycle(SEM_ID test = 0);
        virtual void    failLearnCycle();
        virtual void    stopLearnCycle();
        virtual void    resetLearnCycle();

        LearnCycleEvent isLearnCycleError(LearnCycleState learnState);

        virtual bool    startPreparing();
        LearnCycleEvent isPreparingDone();
        virtual bool    stopPreparing();

        virtual bool    startCalibrating();
        LearnCycleEvent isCalibratingDone();
        virtual bool    stopCalibrating();

        virtual bool    startDischarging();
        LearnCycleEvent isDischargingDone();
        virtual bool    stopDischarging();

        virtual bool    startRecovering();
        LearnCycleEvent isRecoveringDone();
        virtual bool    stopRecovering();

        virtual bool    startCharging();
        LearnCycleEvent isChargingDone();
        virtual bool    stopCharging();

        virtual bool    startRelaxing();
        LearnCycleEvent isRelaxingDone();
        virtual bool    stopRelaxing();

        virtual bool    finishLearning();
        virtual UINT32  getRelativeSOC();

        virtual bool    isReadyForSleep();

        // from ShowableObject
        virtual const char* toStringHeading(int level=0) const;
        virtual const char* toString(int level=0) const;
        virtual void showDetails() const;

        // from Battery
        virtual void disableLearnSODCheck();
        virtual void enableLearnSODCheck();

    private:
        bool            m_IsTaxonomyComplete;
        UINT32          m_I2CError;
        UINT32          m_I2CTotal;
        bool            m_BattFailed;
        bool            m_PFStatus;
        bool            m_IsFailed;
        UINT32          m_CellBalanceRetry;
        UINT32          m_CellBalanceMax;
        UINT32          m_DischargingThreshold;
        UINT32          m_PreparingInterval;
        UINT32          m_CalibratingInterval;
        UINT32          m_DischargingInterval;
        UINT32          m_RecoveringInterval;
        UINT32          m_ChargingInterval;
        UINT32          m_RelaxingInterval;
        LearnMethod     m_LearnMethod;
        SEM_ID          m_LearnSignal;
        UINT32          m_InitialSOC;
        UINT32          m_ProgressSOC;
        UINT32          m_CellThreshold;
        bool            m_IsCalDisabled;
        bool            m_IsDischarging;
        bool            m_IsQmaxUpdateReady;
        UINT32          m_LearnCycleRetry;
        UINT32          m_LearnCycleMaxAttempts;
        bool            m_IsLearnTempered;  //Cells have relaxed/normalized after Full Charge of LC
        UINT32          m_LearnFailedState;
        bool            m_IsLearnTimeOut;
        bool            m_IsLogTempUpdateNeeded;
        GasGauge*       m_GasGauge;
        Charger*        m_Charger;
    };
}

#endif        /* End of __INCbidBBUSmartSingle */
