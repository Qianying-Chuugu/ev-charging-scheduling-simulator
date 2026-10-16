#ifndef TIMER_H
#define TIMER_H

#include <glib.h>

/* 定时控制器，保存GTK定时任务和固定时间步的累计时间。 */
typedef struct {
    guint source_id;
    gint64 last_frame_us;
    double accumulator_ms;
    double step_interval_ms;
} TimerControl;

/* 初始化定时控制器。 */
void timer_init(TimerControl *timer, double step_interval_ms);

/* 启动GTK周期定时器，成功返回0。 */
int timer_start(TimerControl *timer, guint frame_interval_ms,
                GSourceFunc callback, gpointer data);

/* 主动停止定时器并清空累计时间。 */
void timer_stop(TimerControl *timer);

/* 定时回调自然结束时清除运行状态。 */
void timer_finish(TimerControl *timer);

/* 根据实际经过时间返回本次应该推进的固定时间步数量。 */
int timer_collect_steps(TimerControl *timer);

/* 返回当前时间步中已经经过的比例，范围为0到1。 */
double timer_progress(const TimerControl *timer);

/* 判断定时器是否正在运行。 */
int timer_is_running(const TimerControl *timer);

#endif
