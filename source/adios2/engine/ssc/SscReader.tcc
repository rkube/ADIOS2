/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * SscReader.tcc
 *
 *  Created on: Nov 1, 2018
 *      Author: Jason Wang
 */

#ifndef ADIOS2_ENGINE_SSCREADER_TCC_
#define ADIOS2_ENGINE_SSCREADER_TCC_

#include "SscReader.h"
#include "adios2/helper/adiosMemory.h"
#include <iostream>

namespace adios2
{
namespace core
{
namespace engine
{

template <class T>
inline void SscReader::GetSyncCommon(Variable<T> &variable, T *data)
{
    TAU_SCOPED_TIMER_FUNC();
    GetDeferredCommon(variable, data);
    PerformGets();
}

template <class T>
void SscReader::GetDeferredCommon(Variable<T> &variable, T *data)
{
    TAU_SCOPED_TIMER_FUNC();
    for (const auto &i : m_AllReceivingWriterRanks)
    {
        const auto &v = m_GlobalWritePattern[i.first];
        for (const auto &b : v)
        {
            if (b.name == variable.m_Name)
            {
                if (b.start.size() == 1 and b.count.size() == 1 and
                    b.shape.size() == 1 and b.start[0] == 0 and
                    b.count[0] == 1 and b.shape[0] == 1)
                {
                    data[0] = reinterpret_cast<T *>(m_Buffer.data() +
                                                    b.bufferStart)[0];
                }
                else
                {
                    helper::NdCopy<T>(
                        m_Buffer.data() + b.bufferStart, b.start, b.count, true,
                        true, reinterpret_cast<char *>(data), variable.m_Start,
                        variable.m_Count, true, true);
                }
            }
        }
    }
}

template <typename T>
std::map<size_t, std::vector<typename Variable<T>::Info>>
SscReader::AllStepsBlocksInfoCommon(const Variable<T> &variable) const
{
    TAU_SCOPED_TIMER_FUNC();
    std::map<size_t, std::vector<typename Variable<T>::Info>> m;
    return m;
}

template <typename T>
std::vector<typename Variable<T>::Info>
SscReader::BlocksInfoCommon(const Variable<T> &variable,
                            const size_t step) const
{
    TAU_SCOPED_TIMER_FUNC();
    std::vector<typename Variable<T>::Info> ret;
    for (const auto &r : m_AllReceivingWriterRanks)
    {
        for (auto &v : m_GlobalWritePattern[r.first])
        {
            if (v.name != variable.m_Name)
            {
                continue;
            }
            if (v.overlapCount.empty())
            {
                continue;
            }
            typename Variable<T>::Info b;
            b.Start = v.overlapStart;
            b.Count = v.overlapCount;
            b.Shape = v.shape;
            b.IsValue = false;
            if (v.shape.size() == 1)
            {
                if (v.shape[0] == 1)
                {
                    b.IsValue = true;
                }
            }
            ret.push_back(b);
        }
    }
    return ret;
}

} // end namespace engine
} // end namespace core
} // end namespace adios2

#endif // ADIOS2_ENGINE_SSCREADER_TCC_
