#include "eggDisposer.h"
#include "eggHeap.h"
#include <nw4r/ut/ut_list.h>

namespace EGG
{
    using namespace nw4r;

    Disposer::Disposer()
    {
        mHeap = Heap::findContainHeap(this);

        if (mHeap != NULL)
            mHeap->appendDisposer(this);
    }

    Disposer::~Disposer()
    {
        if (mHeap != NULL)
            mHeap->removeDisposer(this);
    }
}