#include "../../src/module1_graph/graph.h"
#include "../../src/module3_entity/entity.h"
#include "../../src/module4_charger/charger.h"
#include "../../src/module5_recommend/recommend.h"
#include "../../src/module6_simulation/simulation.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* 测试完整仿真流程和重置功能。 */
int main(void)
{
    Graph graph;
    EntityStore store;
    Simulation simulation;
    RecommendationList list;
    const Recommendation *best;
    Vehicle *vehicle;
    double arrival_battery;
    double battery_before;
    int steps = 0;

    graph_init(&graph);
    entity_init(&store);
    assert(graph_add_typed_node(&graph, 1, 0, 0,
                                GRAPH_NODE_VEHICLE_POINT) == 0);
    assert(graph_add_typed_node(&graph, 2, 1, 0,
                                GRAPH_NODE_STATION_POINT) == 0);
    assert(graph_add_edge(&graph, 1, 2, 2, 2) == 0);
    assert(entity_add_station(&store, &graph, 10, 2, "Station") == 0);
    assert(charger_add(&store, 10, 100, 60, CHARGER_FREE) == 0);
    assert(charger_add(&store, 10, 101, 60, CHARGER_FAULT) == 0);
    assert(entity_add_vehicle(&store, &graph, 1, 1,
                              20, 80, 60, 2) == 0);

    vehicle = entity_find_vehicle(&store, 1);
    assert(recommend_all(&graph, &store, vehicle, 0, &list) == 0);
    assert(simulation_init(&simulation, &graph, &store, 1) == 0);
    assert(simulation_charger_count(&simulation, 10) == 2);
    assert(simulation_charger_at(&simulation, 10, 0) != NULL);
    assert(simulation_charger_at(&simulation, 10, 2) == NULL);
    best = recommend_best(&list);
    assert(best != NULL);
    arrival_battery = best->arrival_battery_percent;
    assert(simulation_submit(&simulation, 1, best) == 0);
    assert(simulation_request_count(&simulation) == 1);
    assert(simulation_request_at(&simulation, 0) == vehicle->request);
    assert(entity_find_request(&store, vehicle->request->id) != NULL);

    /* 提交和行驶途中保持出发电量，到站时再一次性扣除耗电。 */
    assert(fabs(vehicle->battery_percent - 20.0) < 0.000001);
    assert(fabs(vehicle->request->arrival_battery_percent
                - arrival_battery) < 0.000001);
    simulation_step(&simulation);
    ++steps;
    assert(vehicle->state == VEHICLE_TRAVELING);
    assert(fabs(vehicle->battery_percent - 20.0) < 0.000001);
    simulation_step(&simulation);
    ++steps;
    assert(vehicle->state == VEHICLE_CHARGING);
    assert(fabs(vehicle->battery_percent - arrival_battery) < 0.000001);
    battery_before = vehicle->battery_percent;
    simulation_step(&simulation);
    assert(vehicle->battery_percent > battery_before);
    assert(simulation_break_charger(&simulation, 100) == 0);
    assert(vehicle->state == VEHICLE_WAITING);
    assert(simulation_waiting_count(&simulation, 10) == 1);
    assert(simulation_waiting_vehicle_at(&simulation, 10, 0) == vehicle);
    assert(simulation_repair_charger(&simulation, 100) == 0);
    simulation_step(&simulation);
    assert(vehicle->state == VEHICLE_CHARGING);

    while (simulation_has_active(&simulation) && steps++ < 100) {
        simulation_step(&simulation);
    }
    assert(vehicle->state == VEHICLE_FINISHED);
    assert(simulation_repair_charger(&simulation, 101) == 0);
    assert(simulation_break_charger(&simulation, 100) == 0);
    simulation_reset(&simulation);
    assert(vehicle->state == VEHICLE_IDLE);
    assert(vehicle->node_id == 1);
    assert(vehicle->battery_percent == 20);
    assert(entity_find_charger(&store, 100)->state == CHARGER_FREE);
    assert(entity_find_charger(&store, 101)->state == CHARGER_FAULT);

    graph_free(&graph);
    entity_free(&store);
    printf("test_simulation passed\n");
    return 0;
}

