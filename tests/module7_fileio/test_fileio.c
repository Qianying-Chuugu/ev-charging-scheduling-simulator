#include "../../src/module1_graph/graph.h"
#include "../../src/module3_entity/entity.h"
#include "../../src/module7_fileio/fileio.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 测试全部基础数据的加载、保存和再次加载。 */
int main(void)
{
    Graph first_graph;
    Graph second_graph;
    EntityStore first_store;
    EntityStore second_store;
    LoadError error;
    Vehicle *first_vehicle;
    Vehicle *second_vehicle;
    Station *first_station;
    Station *second_station;
    Charger *first_charger;
    Charger *second_charger;
    int node_count;
    int vehicle_count;
    int station_count;
    int charger_count;
    DataFiles input = {
        "data/input/nodes.csv",
        "data/input/roads.csv",
        "data/input/stations.csv",
        "data/input/chargers.csv",
        "data/input/vehicles.csv"
    };
    DataFiles output = {
        "data/output/test_nodes.csv",
        "data/output/test_roads.csv",
        "data/output/test_stations.csv",
        "data/output/test_chargers.csv",
        "data/output/test_vehicles.csv"
    };
    DataFiles invalid = {
        "data/input/not_found.csv",
        "data/input/roads.csv",
        "data/input/stations.csv",
        "data/input/chargers.csv",
        "data/input/vehicles.csv"
    };

    graph_init(&first_graph);
    graph_init(&second_graph);
    entity_init(&first_store);
    entity_init(&second_store);
    assert(fileio_load_all(&first_graph, &first_store, &input, &error) == 0);

    /* 有效文件应当能够加载出完整的基础数据。 */
    assert(first_graph.node_count > 0);
    assert(first_store.vehicle_count > 0);
    assert(first_store.station_count > 0);
    assert(first_store.charger_count > 0);

    node_count = first_graph.node_count;
    vehicle_count = first_store.vehicle_count;
    station_count = first_store.station_count;
    charger_count = first_store.charger_count;

    /* 加载错误文件失败后，原来已经加载的数据必须保留。 */
    assert(fileio_load_all(&first_graph, &first_store, &invalid, &error) != 0);
    assert(first_graph.node_count == node_count);
    assert(first_store.vehicle_count == vehicle_count);
    assert(first_store.station_count == station_count);
    assert(first_store.charger_count == charger_count);

    /* 保存并重新读取，检查数据数量是否保持一致。 */
    assert(fileio_save_all(&first_graph, &first_store, &output) == 0);
    assert(fileio_load_all(&second_graph, &second_store, &output, &error) == 0);
    assert(second_graph.node_count == first_graph.node_count);
    assert(second_store.vehicle_count == first_store.vehicle_count);
    assert(second_store.station_count == first_store.station_count);
    assert(second_store.charger_count == first_store.charger_count);

    /* 比较一个节点和一组业务实体，防止只保存了数量而内容错误。 */
    assert(second_graph.nodes[0].id == first_graph.nodes[0].id);
    assert(second_graph.nodes[0].type == first_graph.nodes[0].type);
    assert(fabs(second_graph.nodes[0].x - first_graph.nodes[0].x) < 0.01);
    assert(fabs(second_graph.nodes[0].y - first_graph.nodes[0].y) < 0.01);

    first_vehicle = entity_find_vehicle(&first_store,
                                         first_store.vehicle_ids[0]);
    second_vehicle = entity_find_vehicle(&second_store,
                                          first_store.vehicle_ids[0]);
    assert(first_vehicle != NULL && second_vehicle != NULL);
    assert(second_vehicle->node_id == first_vehicle->node_id);
    assert(fabs(second_vehicle->battery_percent
                - first_vehicle->battery_percent) < 0.01);
    assert(second_vehicle->urgency == first_vehicle->urgency);

    first_station = entity_find_station(&first_store,
                                         first_store.station_ids[0]);
    second_station = entity_find_station(&second_store,
                                          first_store.station_ids[0]);
    assert(first_station != NULL && second_station != NULL);
    assert(second_station->node_id == first_station->node_id);
    assert(strcmp(second_station->name, first_station->name) == 0);

    first_charger = entity_find_charger(&first_store,
                                         first_store.charger_ids[0]);
    second_charger = entity_find_charger(&second_store,
                                          first_store.charger_ids[0]);
    assert(first_charger != NULL && second_charger != NULL);
    assert(fabs(second_charger->power_kw - first_charger->power_kw) < 0.01);
    assert(second_charger->initial_state == first_charger->initial_state);

    graph_free(&first_graph);
    graph_free(&second_graph);
    entity_free(&first_store);
    entity_free(&second_store);
    printf("test_fileio passed\n");
    return 0;
}

