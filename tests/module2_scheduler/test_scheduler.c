#include "../../src/module2_scheduler/scheduler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 测试最大堆能够优先返回紧急车辆。 */
int main(void)
{
    Station station;
    Vehicle normal;
    Vehicle urgent;
    ChargeRequest first;
    ChargeRequest second;
    Charger busy_charger;
    Charger free_charger;
    Station estimate_station;
    Vehicle estimate_vehicles[3];
    ChargeRequest estimate_requests[3];
    Charger fast_charger;
    Charger slow_charger;

    memset(&station, 0, sizeof(station));
    memset(&normal, 0, sizeof(normal));
    memset(&urgent, 0, sizeof(urgent));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&busy_charger, 0, sizeof(busy_charger));
    memset(&free_charger, 0, sizeof(free_charger));
    memset(&estimate_station, 0, sizeof(estimate_station));
    memset(estimate_vehicles, 0, sizeof(estimate_vehicles));
    memset(estimate_requests, 0, sizeof(estimate_requests));
    memset(&fast_charger, 0, sizeof(fast_charger));
    memset(&slow_charger, 0, sizeof(slow_charger));

    normal.id = 1;
    normal.battery_percent = 40;
    normal.urgency = 1;
    urgent.id = 2;
    urgent.battery_percent = 20;
    urgent.urgency = 3;

    first.vehicle = &normal;
    first.arrival_time = 0;
    first.sequence = 1;
    second.vehicle = &urgent;
    second.arrival_time = 1;
    second.sequence = 2;

    assert(scheduler_push(&station, &first, 5) == 0);
    assert(scheduler_push(&station, &second, 5) == 0);
    assert(scheduler_pop(&station) == &second);
    assert(scheduler_pop(&station) == &first);

    busy_charger.power_kw = 60;
    busy_charger.state = CHARGER_BUSY;
    busy_charger.remaining_minutes = 30;
    busy_charger.next = &free_charger;
    free_charger.power_kw = 40;
    free_charger.state = CHARGER_FREE;
    station.chargers = &busy_charger;
    assert(scheduler_estimate_wait(&station, 20, 0, 0) == 0.0);

    /* 验证估算按最大堆出堆顺序进行，而不是按堆数组下标进行。 */
    fast_charger.power_kw = 60;
    fast_charger.state = CHARGER_FREE;
    fast_charger.next = &slow_charger;
    slow_charger.power_kw = 30;
    slow_charger.state = CHARGER_FREE;
    estimate_station.chargers = &fast_charger;

    estimate_vehicles[0].battery_percent = 50;
    estimate_vehicles[0].urgency = 3;
    estimate_requests[0].vehicle = &estimate_vehicles[0];
    estimate_requests[0].needed_kwh = 20;
    estimate_requests[0].sequence = 1;

    estimate_vehicles[1].battery_percent = 50;
    estimate_vehicles[1].urgency = 1;
    estimate_requests[1].vehicle = &estimate_vehicles[1];
    estimate_requests[1].needed_kwh = 10;
    estimate_requests[1].sequence = 2;

    estimate_vehicles[2].battery_percent = 50;
    estimate_vehicles[2].urgency = 2;
    estimate_requests[2].vehicle = &estimate_vehicles[2];
    estimate_requests[2].needed_kwh = 60;
    estimate_requests[2].sequence = 3;

    assert(scheduler_push(&estimate_station, &estimate_requests[0], 0) == 0);
    assert(scheduler_push(&estimate_station, &estimate_requests[1], 0) == 0);
    assert(scheduler_push(&estimate_station, &estimate_requests[2], 0) == 0);
    assert(scheduler_estimate_wait(&estimate_station, 20, 0, 10) == 20.0);
    assert(estimate_station.queue_size == 3);
    assert(estimate_requests[0].wait_time == 0.0);
    assert(scheduler_pop(&estimate_station) == &estimate_requests[0]);
    assert(scheduler_pop(&estimate_station) == &estimate_requests[2]);
    assert(scheduler_pop(&estimate_station) == &estimate_requests[1]);
    printf("test_scheduler passed\n");
    return 0;
}

