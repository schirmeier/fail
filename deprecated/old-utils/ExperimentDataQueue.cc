#include "ExperimentDataQueue.hpp"

#include <assert.h>

// FIXME: This is deprecated stuff. Remove it.

namespace fi
{

void ExperimentDataQueue::addData(ExperimentData* exp)
{
    assert(exp != 0);
    m_queue.push_front(exp);
}

ExperimentData* ExperimentDataQueue::getData()
{
    ExperimentData* ret = m_queue.back();
    m_queue.pop_back();
    return ret;
}


}