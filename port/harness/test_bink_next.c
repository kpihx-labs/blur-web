#include "bink_hle.h"
#include <assert.h>

int main(void) {
    assert(bk_next_frame_number(4, 10, 0) == 5);
    assert(bk_next_frame_number(10, 10, 0) == 10);
    assert(bk_next_frame_number(10, 10, BK_FLAG_WILL_LOOP) == 1);
    return 0;
}
