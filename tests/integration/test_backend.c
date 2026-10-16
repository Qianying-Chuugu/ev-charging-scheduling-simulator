#include "../../src/module1_graph/graph.h"
#include "../../src/module3_entity/entity.h"
#include "../../src/module4_charger/charger.h"
#include "../../src/module5_recommend/recommend.h"
#include "../../src/module6_simulation/simulation.h"
#include "../../src/module7_fileio/fileio.h"

#include <assert.h>
#include <stdio.h>

/* 测试数据加载、推荐和完整充电流程。 */
int main(void)
{
    Graph graph;
    EntityStore store;
    Simulation simulation;
    RecommendationList list;
    PathResult path;
    const Recommendation *best;
    Vehicle *vehicle;
    LoadError error;
    DataFiles files = {
        "data/input/nodes.csv",
        "data/input/roads.csv",
        "data/input/stations.csv",
        "data/input/chargers.csv",
        "data/input/vehicles.csv"
    };
    int steps = 0;
    int i;

    graph_init(&graph);
    entity_init(&store);
    assert(fileio_load_all(&graph, &store, &files, &error) == 0);
    assert(graph.node_count == 305);
    assert(store.station_count == 5);
    assert(store.vehicle_count == 60);
    assert(store.charger_count == 24);
    assert(entity_find_charger(&store, 1001) != NULL);
    for (i = 0; i < store.station_count; ++i) {
        Station *station = entity_find_station(&store, store.station_ids[i]);
        assert(station != NULL);
        assert(graph_shortest_path(&graph, 184, station->node_id, &path) == 0);
        assert(path.reachable);
    }

    vehicle = entity_find_vehicle(&store, 201);
    assert(vehicle != NULL);
    assert(recommend_all(&graph, &store, vehicle, 0, &list) == 0);
    best = recommend_best(&list);
    assert(best != NULL);

    assert(simulation_init(&simulation, &graph, &store, 1.0) == 0);
    assert(simulation_submit(&simulation, vehicle->id, best) == 0);
    while (simulation_has_active(&simulation) && steps < 500) {
        assert(simulation_step(&simulation) == 0);
        ++steps;
    }
    assert(steps < 500);
    assert(vehicle->state == VEHICLE_FINISHED);
    assert(vehicle->battery_percent == vehicle->target_percent);

    /* 批量提交当前数据中的全部车辆，确认均可寻路并最终完成。 */
    simulation_reset(&simulation);
    for (i = 0; i < store.vehicle_count; ++i) {
        vehicle = entity_find_vehicle(&store, store.vehicle_ids[i]);
        assert(vehicle != NULL);
        assert(recommend_all(&graph, &store, vehicle,
                             simulation.current_time, &list) == 0);
        best = recommend_best(&list);
        assert(best != NULL);
        assert(simulation_submit(&simulation, vehicle->id, best) == 0);
    }
    steps = 0;
    while (simulation_has_active(&simulation) && steps < 1000) {
        assert(simulation_step(&simulation) == 0);
        ++steps;
    }
    assert(steps < 1000);
    for (i = 0; i < store.vehicle_count; ++i) {
        vehicle = entity_find_vehicle(&store, store.vehicle_ids[i]);
        assert(vehicle != NULL);
        assert(vehicle->state == VEHICLE_FINISHED);
    }

    graph_free(&graph);
    entity_free(&store);
    printf("test_backend passed\n");
    return 0;
}

