#include "../../src/module9_timer/timer.h"

#include <assert.h>
#include <stdio.h>

/* 测试定时控制器的初始化、启动和停止。 */
static gboolean empty_tick(gpointer data)
{
    (void)data;
    return G_SOURCE_CONTINUE;
}

int main(void)
{
    TimerControl timer;

    timer_init(&timer, 300.0);
    assert(!timer_is_running(&timer));
    assert(timer_progress(&timer) == 0.0);
    assert(timer_start(&timer, 16, empty_tick, NULL) == 0);
    assert(timer_is_running(&timer));
    assert(timer_start(&timer, 16, empty_tick, NULL) == 0);
    timer_stop(&timer);
    assert(!timer_is_running(&timer));
    assert(timer_progress(&timer) == 0.0);

    printf("test_timer passed\n");
    return 0;
}
