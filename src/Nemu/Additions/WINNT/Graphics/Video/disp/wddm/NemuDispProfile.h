/* $Id: NemuDispProfile.h $ */

/** @file
 * NemuVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuDispProfile_h__
#define ___NemuDispProfile_h__
#include "NemuDispD3DCmn.h"

#include <iprt/ctype.h>
#include <iprt/time.h>

#define NEMUDISPPROFILE_MAX_SETSIZE 512
#define NEMUDISPPROFILE_GET_TIME_NANO() RTTimeNanoTS()
#define NEMUDISPPROFILE_GET_TIME_MILLI() RTTimeMilliTS()
#define NEMUDISPPROFILE_DUMP(_m) do {\
        LOGREL_EXACT(_m); \
    } while (0)

class NemuDispProfileEntry
{
public:
    NemuDispProfileEntry() :
        m_cCalls(0),
        m_cTime(0),
        m_pName(NULL)
    {}

    NemuDispProfileEntry(const char *pName) :
        m_cCalls(0),
        m_cTime(0),
        m_pName(pName)
    {}

    void step(uint64_t cTime)
    {
        ++m_cCalls;
        m_cTime+= cTime;
    }

    void reset()
    {
        m_cCalls = 0;
        m_cTime = 0;
    }

    uint64_t getTime() const
    {
        return m_cTime;
    }

    uint32_t getNumCalls() const
    {
        return m_cCalls;
    }

    const char* getName() const
    {
        return m_pName;
    }

    void dump(void *pvObj, uint64_t cTotalEntriesTime, uint64_t cTotalTime) const
    {
//        NEMUDISPPROFILE_DUMP((pDevice, "Entry '%s': calls(%d), time: nanos(%I64u), micros(%I64u), millis(%I64u)\n",
//                m_pName, m_cCalls,
//                m_cTime, m_cTime/1000, m_cTime/1000000));

//        NEMUDISPPROFILE_DUMP(("'%s' [0x%p]: \t%d\t%u\t%u\t%u\t%f\t%f", m_pName, pvObj,
//                m_cCalls,
//                (uint32_t)m_cTime, (uint32_t)(m_cTime/1000), (uint32_t)(m_cTime/1000000),
//                (((double)m_cTime)/cTotalEntriesTime),
//                (((double)m_cTime)/cTotalTime)));

        NEMUDISPPROFILE_DUMP(("'%s' [0x%p]: \t%d\t%u\t%f\t%f", m_pName, pvObj,
                m_cCalls,
                (uint32_t)(m_cTime/1000000),
                (((double)m_cTime)/cTotalEntriesTime),
                (((double)m_cTime)/cTotalTime)));

    }
private:
    uint32_t m_cCalls;
    uint64_t m_cTime;
    const char * m_pName;
};

class NemuDispProfileSet
{
public:
    NemuDispProfileSet(const char *pName) :
        m_cEntries(0),
        m_cIterations(0),
        m_pName(pName)
    {
        m_cTime = NEMUDISPPROFILE_GET_TIME_NANO();
    }

    NemuDispProfileSet() :
        m_cEntries(0),
        m_cIterations(0),
        m_pName("global")
    {
        m_cTime = NEMUDISPPROFILE_GET_TIME_NANO();
    }

    NemuDispProfileEntry * alloc(const char *pName)
    {
        if (m_cEntries < RT_ELEMENTS(m_Entries))
        {
            NemuDispProfileEntry * entry = &m_Entries[m_cEntries];
            ++m_cEntries;
            *entry = NemuDispProfileEntry(pName);
            return entry;
        }
        return NULL;
    }

    NemuDispProfileEntry * get(uint32_t u32Entry, const char *pName)
    {
        if (u32Entry < RT_ELEMENTS(m_Entries))
        {
            NemuDispProfileEntry * entry = &m_Entries[u32Entry];
            if (entry->getName())
                return entry;
            ++m_cEntries;
            *entry = NemuDispProfileEntry(pName);
            return entry;
        }
        return NULL;
    }

    uint32_t reportIteration()
    {
        return ++m_cIterations;
    }

    uint32_t getNumIterations() const
    {
        return m_cIterations;
    }

    uint32_t getNumEntries() const
    {
        return m_cEntries;
    }

#define NEMUDISPPROFILESET_FOREACHENTRY(_op) \
        for (uint32_t i = 0, e = 0; e < m_cEntries && i < RT_ELEMENTS(m_Entries); ++i) { \
            if (m_Entries[i].getName()) { \
                { \
                _op  \
                } \
                ++e; \
            } \
        } \

    void resetEntries()
    {
        NEMUDISPPROFILESET_FOREACHENTRY(  m_Entries[i].reset(); );
        m_cTime = NEMUDISPPROFILE_GET_TIME_NANO();
    }

    void reset()
    {
        m_cEntries = 0;
        m_cTime = NEMUDISPPROFILE_GET_TIME_NANO();
    }

    void dump(void *pvObj)
    {
        uint64_t cEntriesTime = 0;
        NEMUDISPPROFILESET_FOREACHENTRY( cEntriesTime += m_Entries[i].getTime(); );

        NEMUDISPPROFILE_DUMP((">>>> '%s' [0x%p]: Start of Nemu Disp Dump: num entries(%d), et(%u), tt(%u) >>>>>", m_pName, pvObj, m_cEntries,
                (uint32_t)(cEntriesTime / 1000000), (uint32_t)(m_cTime / 1000000)));
        NEMUDISPPROFILE_DUMP(("Name\tCalls\tNanos\tMicros\tMillis\tentries_quota\ttotal_quota"));
        NEMUDISPPROFILESET_FOREACHENTRY(
                if (m_Entries[i].getNumCalls())
                    m_Entries[i].dump(pvObj, cEntriesTime, m_cTime); );
        NEMUDISPPROFILE_DUMP(("<<<< '%s' [0x%p]: End of Nemu Disp Dump <<<<<", m_pName, pvObj));
    }

private:
    NemuDispProfileEntry m_Entries[NEMUDISPPROFILE_MAX_SETSIZE];
    uint32_t m_cEntries;
    uint32_t m_cIterations;
    uint64_t m_cTime;
    const char * m_pName;
};

class NemuDispProfileDummyPostProcess
{
public:
    void postProcess(){}
};

template<typename T, typename P> class NemuDispProfileScopeLogger
{
public:
    NemuDispProfileScopeLogger(T *pEntry, P PostProcess) :
        m_pEntry(pEntry),
        m_PostProcess(PostProcess),
        m_bDisable(FALSE)
    {
        m_cTime = NEMUDISPPROFILE_GET_TIME_NANO();
    }

    ~NemuDispProfileScopeLogger()
    {
        if (!m_bDisable)
        {
            logStep();
        }
    }

    void disable()
    {
        m_bDisable = TRUE;
    }

    void logAndDisable()
    {
        logStep();
        disable();
    }

private:
    void logStep()
    {
        m_PostProcess.postProcess();
        uint64_t cNewTime = NEMUDISPPROFILE_GET_TIME_NANO();
        m_pEntry->step(cNewTime - m_cTime);
    }
    T *m_pEntry;
    P m_PostProcess;
    uint64_t m_cTime;
    BOOL m_bDisable;
};


class NemuDispProfileFpsCounter
{
public:
    NemuDispProfileFpsCounter(uint32_t cPeriods)
    {
        init(cPeriods);
    }

    NemuDispProfileFpsCounter()
    {
        memset(&m_Data, 0, sizeof (m_Data));
    }

    ~NemuDispProfileFpsCounter()
    {
        term();
    }

    void term()
    {
        if (m_Data.mpaPeriods)
        {
            RTMemFree(m_Data.mpaPeriods);
            m_Data.mpaPeriods = NULL;
        }
        if (m_Data.mpaCalls)
        {
            RTMemFree(m_Data.mpaCalls);
            m_Data.mpaCalls = NULL;
        }
        if (m_Data.mpaTimes)
        {
            RTMemFree(m_Data.mpaTimes);
            m_Data.mpaTimes = NULL;
        }
        m_Data.mcPeriods = 0;
    }

    /* to be called in case fps counter was created with default constructor */
    void init(uint32_t cPeriods)
    {
        memset(&m_Data, 0, sizeof (m_Data));
        m_Data.mcPeriods = cPeriods;
        if (cPeriods)
        {
            m_Data.mpaPeriods = (uint64_t *)RTMemAllocZ(sizeof (m_Data.mpaPeriods[0]) * cPeriods);
            m_Data.mpaCalls = (uint32_t *)RTMemAllocZ(sizeof (m_Data.mpaCalls[0]) * cPeriods);
            m_Data.mpaTimes = (uint64_t *)RTMemAllocZ(sizeof (m_Data.mpaTimes[0]) * cPeriods);
        }
    }

    void ReportFrame()
    {
        uint64_t cur = NEMUDISPPROFILE_GET_TIME_NANO();

        if(m_Data.mPrevTime)
        {
            uint64_t curPeriod = cur - m_Data.mPrevTime;

            m_Data.mPeriodSum += curPeriod - m_Data.mpaPeriods[m_Data.miPeriod];
            m_Data.mpaPeriods[m_Data.miPeriod] = curPeriod;

            m_Data.mCallsSum += m_Data.mCurCalls - m_Data.mpaCalls[m_Data.miPeriod];
            m_Data.mpaCalls[m_Data.miPeriod] = m_Data.mCurCalls;

            m_Data.mTimeUsedSum += m_Data.mCurTimeUsed - m_Data.mpaTimes[m_Data.miPeriod];
            m_Data.mpaTimes[m_Data.miPeriod] = m_Data.mCurTimeUsed;

            ++m_Data.miPeriod;
            m_Data.miPeriod %= m_Data.mcPeriods;
        }
        m_Data.mPrevTime = cur;
        ++m_Data.mcFrames;

        m_Data.mCurTimeUsed = 0;
        m_Data.mCurCalls = 0;
    }

    void step(uint64_t Time)
    {
        m_Data.mCurTimeUsed += Time;
        ++m_Data.mCurCalls;
    }

    uint64_t GetEveragePeriod()
    {
        return m_Data.mPeriodSum / m_Data.mcPeriods;
    }

    double GetFps()
    {
        return ((double)1000000000.0) / GetEveragePeriod();
    }

    double GetCps()
    {
        return GetFps() * m_Data.mCallsSum / m_Data.mcPeriods;
    }

    double GetTimeProcPercent()
    {
        return 100.0*m_Data.mTimeUsedSum/m_Data.mPeriodSum;
    }

    uint64_t GetNumFrames()
    {
        return m_Data.mcFrames;
    }
private:
    struct
    {
        uint64_t mPeriodSum;
        uint64_t *mpaPeriods;
        uint64_t mPrevTime;
        uint64_t mcFrames;
        uint32_t mcPeriods;
        uint32_t miPeriod;

        uint64_t mCallsSum;
        uint32_t *mpaCalls;

        uint64_t mTimeUsedSum;
        uint64_t *mpaTimes;

        uint64_t mCurTimeUsed;
        uint64_t mCurCalls;
    } m_Data;
};

#define NEMUDISPPROFILE_FUNCTION_LOGGER_DISABLE_CURRENT() do { \
        __nemuDispProfileFunctionLogger.disable();\
    } while (0)

#define NEMUDISPPROFILE_FUNCTION_LOGGER_LOG_AND_DISABLE_CURRENT() do { \
        __nemuDispProfileFunctionLogger.logAndDisable();\
    } while (0)

#ifdef NEMUDISPPROFILE_FUNCTION_LOGGER_GLOBAL_PROFILE
# define NEMUDISPPROFILE_FUNCTION_LOGGER_DEFINE(_p, _T, _v)  \
        static NemuDispProfileEntry * __pNemuDispProfileEntry = NULL; \
        if (!__pNemuDispProfileEntry) { __pNemuDispProfileEntry = _p.alloc(__FUNCTION__); } \
        NemuDispProfileScopeLogger<NemuDispProfileEntry, _T> __nemuDispProfileFunctionLogger(__pNemuDispProfileEntry, _v);
#else
# ifndef NEMUDISPPROFILE_FUNCTION_LOGGER_INDEX_GEN
#  error "NEMUDISPPROFILE_FUNCTION_LOGGER_INDEX_GEN should be fedined!"
# endif
# define NEMUDISPPROFILE_FUNCTION_LOGGER_DEFINE(_p, _T, _v)  \
        static uint32_t __u32NemuDispProfileIndex = NEMUDISPPROFILE_FUNCTION_LOGGER_INDEX_GEN(); \
        NemuDispProfileEntry * __pNemuDispProfileEntry = _p.get(__u32NemuDispProfileIndex, __FUNCTION__); \
        NemuDispProfileScopeLogger<NemuDispProfileEntry, _T> __nemuDispProfileFunctionLogger(__pNemuDispProfileEntry, _v);
#endif

#define NEMUDISPPROFILE_STATISTIC_LOGGER_DISABLE_CURRENT() do { \
        __nemuDispProfileStatisticLogger.disable();\
    } while (0)

#define NEMUDISPPROFILE_STATISTIC_LOGGER_LOG_AND_DISABLE_CURRENT() do { \
        __nemuDispProfileStatisticLogger.logAndDisable();\
    } while (0)


#define NEMUDISPPROFILE_STATISTIC_LOGGER_DEFINE(_p, _T, _v)  \
        NemuDispProfileScopeLogger<NemuDispProfileFpsCounter, _T> __nemuDispProfileStatisticLogger(_p, _v);

//#define NEMUDISPPROFILE_FUNCTION_PROLOGUE(_p) \
//        NEMUDISPPROFILE_FUNCTION_LOGGER_DEFINE(_p)

#endif /* #ifndef ___NemuDispProfile_h__ */
