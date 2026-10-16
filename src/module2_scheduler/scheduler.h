#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../common/model.h"

/* 计算一条充电请求在当前时刻的动态优先级。 */
double scheduler_calc_priority(ChargeRequest *request, double current_time);

/* 把一条请求加入站点的最大堆等待队列。 */
int scheduler_push(Station *station, ChargeRequest *request, double current_time);

/* 取出站点中当前优先级最高的请求。 */
ChargeRequest *scheduler_pop(Station *station);

/* 重新计算队列中所有请求的优先级并重建最大堆。 */
void scheduler_rebuild(Station *station, double current_time);

/* 按实际优先级顺序模拟等待队列，估算新车辆到站后的等待时间。 */
double scheduler_estimate_wait(const Station *station, double needed_kwh,
                               double current_time, double arrival_time);

#endif

