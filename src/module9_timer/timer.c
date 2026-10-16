#include "timer.h"

/* 初始化定时控制器。 */
void timer_init(TimerControl *timer, double step_interval_ms)
{
    if (timer == NULL) return;
    timer->source_id = 0;
    timer->last_frame_us = 0;
    timer->accumulator_ms = 0.0;
    timer->step_interval_ms = step_interval_ms;
}

/* 启动GTK周期定时器，重复启动不会创建第二个定时任务。 */
int timer_start(TimerControl *timer, guint frame_interval_ms,
                GSourceFunc callback, gpointer data)
{
    if (timer == NULL || timer->step_interval_ms <= 0.0 ||
        frame_interval_ms == 0 || callback == NULL) {
        return -1;
    }
    if (timer->source_id != 0) return 0;

    timer->last_frame_us = g_get_monotonic_time();
    timer->accumulator_ms = 0.0;
    timer->source_id = g_timeout_add(frame_interval_ms, callback, data);
    return timer->source_id == 0 ? -1 : 0;
}

/* 主动停止定时器并清空累计时间。 */
void timer_stop(TimerControl *timer)
{
    if (timer == NULL) return;
    if (timer->source_id != 0) {
        g_source_remove(timer->source_id);
    }
    timer->source_id = 0;
    timer->last_frame_us = 0;
    timer->accumulator_ms = 0.0;
}

/* 定时回调返回结束时，GTK会自行移除定时任务。 */
void timer_finish(TimerControl *timer)
{
    if (timer == NULL) return;
    timer->source_id = 0;
    timer->last_frame_us = 0;
    timer->accumulator_ms = 0.0;
}

/* 把真实经过时间换算为固定仿真步数。 */
int timer_collect_steps(TimerControl *timer)
{
    gint64 now;
    double elapsed_ms;
    int steps;

    if (!timer_is_running(timer) || timer->step_interval_ms <= 0.0) {
        return 0;
    }
    now = g_get_monotonic_time();
    elapsed_ms = (now - timer->last_frame_us) / 1000.0;
    timer->last_frame_us = now;
    if (elapsed_ms < 0.0) elapsed_ms = 0.0;
    if (elapsed_ms > 100.0) elapsed_ms = 100.0;

    timer->accumulator_ms += elapsed_ms;
    steps = (int)(timer->accumulator_ms / timer->step_interval_ms);
    timer->accumulator_ms -= steps * timer->step_interval_ms;
    return steps;
}

/* 返回两个仿真时间步之间的动画进度。 */
double timer_progress(const TimerControl *timer)
{
    double progress;

    if (timer == NULL || timer->step_interval_ms <= 0.0) return 0.0;
    progress = timer->accumulator_ms / timer->step_interval_ms;
    if (progress < 0.0) return 0.0;
    if (progress > 1.0) return 1.0;
    return progress;
}

/* 判断定时器是否正在运行。 */
int timer_is_running(const TimerControl *timer)
{
    return timer != NULL && timer->source_id != 0;
}
