#include "responsetiming.h"

#include <cassert>

int main()
{
    assert(Ciqtek::responseReachedTimeout(49, 40));
    assert(!Ciqtek::responseReachedTimeout(39, 40));
    assert(Ciqtek::responseReachedTimeout(40, 40));
    return 0;
}
