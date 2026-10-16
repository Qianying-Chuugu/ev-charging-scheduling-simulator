#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../module1_graph/graph.h"
#include "../module2_scheduler/scheduler.h"
#include "../module3_entity/entity.h"
#include "../module4_charger/charger.h"
#include "../module5_recommend/recommend.h"
#include "../module6_simulation/simulation.h"
#include "../module7_fileio/fileio.h"
#include "../module9_timer/timer.h"

/*
 * GTK 4 单窗口图形界面。
 *
 * 本文件只负责界面显示和用户交互。邻接表、最小堆、最大堆、
 * 哈希表、充电桩链表、Dijkstra、推荐与仿真调用模块1至模块7，
 * 定时控制调用模块9。
 */

#define UI_FRAME_INTERVAL_MS 16
#define UI_STEP_INTERVAL_MS 300.0

typedef struct {
    int vehicle_id;
    int node_ids[MAX_PATH_NODES];
    int path_length;
    double start_time;
    double duration;
    int valid;
} VehicleMotion;

typedef struct {
    GtkWidget *panel_card;
    int start_width;
    int minimum_width;
    int left_side;
} PanelResizeData;

typedef enum {
    SIMULATION_MODE_PROJECT,
    SIMULATION_MODE_CONTROL,
    SIMULATION_MODE_COUNT
} SimulationMode;

typedef enum {
    STAT_AVERAGE_TOTAL,
    STAT_MAXIMUM_TOTAL,
    STAT_AVERAGE_WAIT,
    STAT_MAXIMUM_WAIT,
    STAT_AVERAGE_TRAVEL,
    STAT_COMPLETED_COUNT,
    STAT_THROUGHPUT,
    STAT_METRIC_COUNT
} StatisticMetric;

typedef struct {
    int active;
    double submitted_at;
    double arrival_at;
    double first_charge_at;
    double travel_minutes;
} VehicleMetric;

typedef struct {
    int completed_count;
    double total_time_sum;
    double total_time_max;
    double wait_time_sum;
    double wait_time_max;
    double travel_time_sum;
    double elapsed_minutes;
} RunMetrics;

typedef struct {
    Graph graph;
    EntityStore store;
    Simulation simulation;
    Recommendation selected;
    int loaded;
    int has_selected;
    int has_path;
    int selected_vehicle_id;
    int selected_station_id;
    int selected_charger_id;
    double map_zoom;
    double map_offset_x;
    double map_offset_y;
    double drag_start_offset_x;
    double drag_start_offset_y;
    double simulation_speed;
    TimerControl timer;
    double visual_time;
    VehicleMotion motions[MAX_VEHICLES];
    int batch_vehicle_ids[MAX_VEHICLES];
    int batch_submitted[MAX_VEHICLES];
    int batch_starts[MAX_VEHICLES];
    int batch_sizes[MAX_VEHICLES];
    int batch_count;
    int next_batch;
    int submitted_vehicle_count;
    int total_submission_count;
    int continuous_pending_ids[MAX_VEHICLES];
    int continuous_pending_count;
    int continuous_batches_submitted;
    int continuous_traffic_enabled;
    int auto_submit_enabled;
    double next_auto_submit_time;
    double simulation_time_limit;
    SimulationMode simulation_mode;
    unsigned int scenario_seed;
    unsigned int traffic_random_state;
    VehicleMetric vehicle_metrics[MAX_VEHICLES];
    RunMetrics run_metrics[SIMULATION_MODE_COUNT];
    int completion_counted[MAX_VEHICLES];
    double vehicle_respawn_time[MAX_VEHICLES];
    double original_target_percent[MAX_VEHICLES];
    int original_urgency[MAX_VEHICLES];
    cairo_surface_t *vehicle_icon;
    cairo_surface_t *station_icon;

    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *zoom_label;
    GtkWidget *speed_label;
    GtkWidget *status_label;
    GtkWidget *statistics_compare_label;
    GtkWidget *statistics_cells[SIMULATION_MODE_COUNT][STAT_METRIC_COUNT];
    GtkWidget *statistics_revealer;
    GtkWidget *batch_status_label;
    GtkWidget *selected_vehicle_info_label;
    GtkWidget *vehicle_entry;
    GtkWidget *station_entry;
    GtkWidget *charger_entry;
    GtkWidget *power_entry;
    GtkWidget *recommendation_list;
    GtkWidget *charger_list;
    GtkWidget *charging_vehicle_list;
    GtkWidget *waiting_vehicle_list;
    GtkWidget *station_operation_title;
    GtkWidget *station_status_label;
} GtkDemoApp;

static GtkDemoApp app;

static void clamp_map_offset(void);
static void reset_simulation(void);
static void on_vehicle_row_selected(GtkListBox *box, GtkListBoxRow *row,
                                    gpointer data);

/* 加载地图图标；资源不可用时返回NULL，由绘图函数使用内置回退样式。 */
static cairo_surface_t *load_map_icon(const char *filename)
{
    cairo_surface_t *icon = cairo_image_surface_create_from_png(filename);

    if (cairo_surface_status(icon) != CAIRO_STATUS_SUCCESS) {
        g_warning("无法加载地图图标 %s: %s", filename,
                  cairo_status_to_string(cairo_surface_status(icon)));
        cairo_surface_destroy(icon);
        return NULL;
    }
    return icon;
}

static void draw_map_icon(cairo_t *cr, cairo_surface_t *icon,
                          double x, double y, double size)
{
    double scale_x;
    double scale_y;

    if (icon == NULL) return;
    scale_x = size / cairo_image_surface_get_width(icon);
    scale_y = size / cairo_image_surface_get_height(icon);
    cairo_save(cr);
    cairo_translate(cr, x - size / 2.0, y - size / 2.0);
    cairo_scale(cr, scale_x, scale_y);
    cairo_set_source_surface(cr, icon, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static const char *charger_state_name(ChargerState state)
{
    switch (state) {
        case CHARGER_FREE: return "空闲";
        case CHARGER_BUSY: return "工作中";
        case CHARGER_FAULT: return "故障";
        default: return "未知";
    }
}

/* 车辆尚未到站时才显示在城市地图上。 */
static int vehicle_is_on_map(const Vehicle *vehicle)
{
    return vehicle != NULL &&
        (vehicle->state == VEHICLE_IDLE ||
         vehicle->state == VEHICLE_TRAVELING);
}

/* 在顶部状态区显示最近一次操作结果或错误。 */
static void output_line(const char *format, ...)
{
    char text[256];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (app.status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app.status_label), text);
    }
}

static void reset_vehicle_motions(void)
{
    memset(app.motions, 0, sizeof(app.motions));
    app.visual_time = app.loaded ? app.simulation.current_time : 0.0;
}

static unsigned int next_batch_random(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/* 同一轮对照实验复用同一随机场景，刷新数据时才生成新场景。 */
static int scenario_random_int(int lower, int upper)
{
    unsigned int range;

    if (upper <= lower) return lower;
    range = (unsigned int)(upper - lower);
    return lower + (int)(next_batch_random(&app.traffic_random_state) % range);
}

static double scenario_random_double(double lower, double upper)
{
    double unit = next_batch_random(&app.traffic_random_state)
                / (double)UINT_MAX;
    return lower + (upper - lower) * unit;
}

static void update_batch_status(void)
{
    char text[160];
    int total = app.loaded ? app.store.vehicle_count : 0;
    int pending = total - app.submitted_vehicle_count
                + app.continuous_pending_count;

    if (pending < 0) pending = 0;
    if (app.next_batch >= app.batch_count &&
        (app.continuous_batches_submitted > 0 ||
         app.continuous_pending_count > 0)) {
        int current = app.continuous_batches_submitted
                    + (app.continuous_pending_count > 0 ? 1 : 0);
        snprintf(text, sizeof(text),
                 "循环批次 %d · 待提交 %d 辆 · 已提交 %d 辆",
                 current, pending, app.total_submission_count);
    } else {
        snprintf(text, sizeof(text),
                 "当前批次 %d/%d · 待提交 %d 辆 · 已提交 %d 辆",
                 app.next_batch, app.batch_count, pending,
                 app.total_submission_count);
    }
    if (app.batch_status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(app.batch_status_label), text);
    }
}

/* 每次重新准备批次时随机洗牌，并按每批最多12辆均匀划分。 */
static void prepare_vehicle_batches(void)
{
    unsigned int random_state = app.scenario_seed ^ 0x9e3779b9u;
    int count = app.loaded ? app.store.vehicle_count : 0;
    int start = 0;
    int base_size;
    int remainder;
    int i;

    memset(app.batch_submitted, 0, sizeof(app.batch_submitted));
    app.batch_count = 0;
    app.next_batch = 0;
    app.submitted_vehicle_count = 0;
    app.total_submission_count = 0;
    app.continuous_pending_count = 0;
    app.continuous_batches_submitted = 0;
    for (i = 0; i < count; ++i) {
        app.batch_vehicle_ids[i] = app.store.vehicle_ids[i];
    }
    for (i = count - 1; i > 0; --i) {
        int other = (int)(next_batch_random(&random_state)
                          % (unsigned int)(i + 1));
        int temp = app.batch_vehicle_ids[i];
        app.batch_vehicle_ids[i] = app.batch_vehicle_ids[other];
        app.batch_vehicle_ids[other] = temp;
    }
    if (count > 0) {
        app.batch_count = (count + 11) / 12;
        base_size = count / app.batch_count;
        remainder = count % app.batch_count;
        for (i = 0; i < app.batch_count; ++i) {
            app.batch_sizes[i] = base_size + (i < remainder ? 1 : 0);
        }
    }
    for (i = 0; i < app.batch_count; ++i) {
        app.batch_starts[i] = start;
        start += app.batch_sizes[i];
    }
    update_batch_status();
}

static int has_pending_batches(void)
{
    return app.next_batch < app.batch_count ||
           app.continuous_pending_count > 0;
}

static void mark_vehicle_submitted(int vehicle_id)
{
    int i;

    for (i = 0; i < app.store.vehicle_count; ++i) {
        if (app.batch_vehicle_ids[i] == vehicle_id) {
            if (!app.batch_submitted[i]) {
                app.batch_submitted[i] = 1;
                ++app.submitted_vehicle_count;
            }
            break;
        }
    }
}

static VehicleMotion *find_vehicle_motion(int vehicle_id)
{
    int i;
    for (i = 0; i < MAX_VEHICLES; ++i) {
        if (app.motions[i].valid && app.motions[i].vehicle_id == vehicle_id) {
            return &app.motions[i];
        }
    }
    return NULL;
}

static void track_vehicle_motion(int vehicle_id,
                                 const Recommendation *recommendation)
{
    VehicleMotion *motion = find_vehicle_motion(vehicle_id);
    int i;

    if (recommendation == NULL || !recommendation->valid) return;
    if (motion == NULL) {
        for (i = 0; i < MAX_VEHICLES; ++i) {
            if (!app.motions[i].valid) {
                motion = &app.motions[i];
                break;
            }
        }
    }
    if (motion == NULL) return;
    memset(motion, 0, sizeof(*motion));
    motion->vehicle_id = vehicle_id;
    motion->path_length = recommendation->path.path_length;
    if (motion->path_length > MAX_PATH_NODES) {
        motion->path_length = MAX_PATH_NODES;
    }
    for (i = 0; i < motion->path_length; ++i) {
        motion->node_ids[i] = recommendation->path.node_ids[i];
    }
    motion->start_time = app.simulation.current_time;
    motion->duration = recommendation->travel_minutes;
    motion->valid = motion->path_length > 0;
}

static int random_vehicle_node_id(void)
{
    int candidate_count = 0;
    int selected;
    int i;

    for (i = 0; i < app.graph.node_count; ++i) {
        if (app.graph.nodes[i].type == GRAPH_NODE_VEHICLE_POINT) {
            ++candidate_count;
        }
    }
    if (candidate_count == 0) return -1;
    selected = scenario_random_int(0, candidate_count);
    for (i = 0; i < app.graph.node_count; ++i) {
        if (app.graph.nodes[i].type != GRAPH_NODE_VEHICLE_POINT) continue;
        if (selected-- == 0) return app.graph.nodes[i].id;
    }
    return -1;
}

static void clear_vehicle_completion_marker(int vehicle_id)
{
    int i;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        if (app.store.vehicle_ids[i] == vehicle_id) {
            app.completion_counted[i] = 0;
            app.vehicle_respawn_time[i] = -1.0;
            return;
        }
    }
}

static void add_continuous_pending_vehicle(int vehicle_id)
{
    int i;

    for (i = 0; i < app.continuous_pending_count; ++i) {
        if (app.continuous_pending_ids[i] == vehicle_id) return;
    }
    if (app.continuous_pending_count < MAX_VEHICLES) {
        app.continuous_pending_ids[app.continuous_pending_count++] = vehicle_id;
    }
}

static void remove_continuous_pending_vehicle(int vehicle_id)
{
    int i;

    for (i = 0; i < app.continuous_pending_count; ++i) {
        if (app.continuous_pending_ids[i] == vehicle_id) {
            int j;
            for (j = i; j + 1 < app.continuous_pending_count; ++j) {
                app.continuous_pending_ids[j] =
                    app.continuous_pending_ids[j + 1];
            }
            --app.continuous_pending_count;
            return;
        }
    }
}

static void track_vehicle_completions(void)
{
    int i;

    if (!app.loaded) return;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        const Vehicle *vehicle = entity_find_vehicle(
            &app.store, app.store.vehicle_ids[i]);
        if (vehicle != NULL && vehicle->state == VEHICLE_FINISHED &&
            !app.completion_counted[i]) {
            app.completion_counted[i] = 1;
            app.vehicle_respawn_time[i] = app.continuous_traffic_enabled
                ? app.simulation.current_time
                    + scenario_random_double(5.0, 10.0)
                : -1.0;
        }
    }
}

static int recycle_vehicle(int index)
{
    Vehicle *vehicle;
    int node_id;

    vehicle = entity_find_vehicle(&app.store, app.store.vehicle_ids[index]);
    if (vehicle == NULL) return -1;
    if (vehicle->state == VEHICLE_FINISHED) {
        int urgency_roll;
        node_id = random_vehicle_node_id();
        if (node_id < 0) return -1;
        vehicle->node_id = node_id;
        vehicle->battery_percent = scenario_random_double(12.0, 46.0);
        vehicle->target_percent = scenario_random_double(80.0, 96.0);
        urgency_roll = scenario_random_int(0, 100);
        vehicle->urgency = urgency_roll < 15 ? 3
                         : urgency_roll < 45 ? 2 : 1;
        vehicle->state = VEHICLE_IDLE;
        vehicle->station_id = -1;
        vehicle->charger_id = -1;
        vehicle->arrival_time = 0.0;
    }
    if (vehicle->state != VEHICLE_IDLE || vehicle->request != NULL) return -1;
    add_continuous_pending_vehicle(vehicle->id);
    app.completion_counted[index] = 0;
    app.vehicle_respawn_time[index] = -1.0;
    update_batch_status();
    return 0;
}

static void process_continuous_traffic(void)
{
    int i;

    track_vehicle_completions();
    if (!app.continuous_traffic_enabled) return;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        if (!app.completion_counted[i]) continue;
        if (app.vehicle_respawn_time[i] < 0.0) {
            app.vehicle_respawn_time[i] = app.simulation.current_time
                + scenario_random_double(5.0, 10.0);
        }
        if (app.simulation.current_time < app.vehicle_respawn_time[i]) {
            continue;
        }
        if (recycle_vehicle(i) != 0) {
            app.vehicle_respawn_time[i] = app.simulation.current_time + 5.0;
        }
    }
}

static int vehicle_metric_index(int vehicle_id)
{
    int i;

    for (i = 0; i < app.store.vehicle_count; ++i) {
        if (app.store.vehicle_ids[i] == vehicle_id) return i;
    }
    return -1;
}

static void reset_current_statistics(void)
{
    memset(app.vehicle_metrics, 0, sizeof(app.vehicle_metrics));
    memset(&app.run_metrics[app.simulation_mode], 0,
           sizeof(app.run_metrics[app.simulation_mode]));
}

static void record_vehicle_submission(int vehicle_id,
                                      const Recommendation *recommendation)
{
    int index = vehicle_metric_index(vehicle_id);
    VehicleMetric *metric;

    if (index < 0 || recommendation == NULL) return;
    metric = &app.vehicle_metrics[index];
    metric->active = 1;
    metric->submitted_at = app.simulation.current_time;
    metric->arrival_at = app.simulation.current_time
                       + recommendation->travel_minutes;
    metric->first_charge_at = -1.0;
    metric->travel_minutes = recommendation->travel_minutes;
}

/* 请求完成后后端会释放请求对象，因此前端在状态变化时立即累计指标。 */
static void track_statistics_after_step(void)
{
    RunMetrics *run = &app.run_metrics[app.simulation_mode];
    int i;

    run->elapsed_minutes = app.simulation.current_time;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        VehicleMetric *metric = &app.vehicle_metrics[i];
        const Vehicle *vehicle;
        double total_time;
        double wait_time;

        if (!metric->active) continue;
        vehicle = entity_find_vehicle(&app.store, app.store.vehicle_ids[i]);
        if (vehicle == NULL) continue;
        if (metric->first_charge_at < 0.0 &&
            vehicle->state == VEHICLE_CHARGING) {
            metric->first_charge_at = app.simulation.current_time;
        }
        if (vehicle->state != VEHICLE_FINISHED) continue;

        total_time = app.simulation.current_time - metric->submitted_at;
        wait_time = metric->first_charge_at >= 0.0
                  ? metric->first_charge_at - metric->arrival_at : 0.0;
        if (total_time < 0.0) total_time = 0.0;
        if (wait_time < 0.0) wait_time = 0.0;
        ++run->completed_count;
        run->total_time_sum += total_time;
        run->wait_time_sum += wait_time;
        run->travel_time_sum += metric->travel_minutes;
        if (total_time > run->total_time_max) {
            run->total_time_max = total_time;
        }
        if (wait_time > run->wait_time_max) {
            run->wait_time_max = wait_time;
        }
        metric->active = 0;
    }
}

static void update_statistics_display(void)
{
    char comparison[640];
    RunMetrics *active = &app.run_metrics[app.simulation_mode];
    RunMetrics *project_run = &app.run_metrics[SIMULATION_MODE_PROJECT];
    RunMetrics *control_run = &app.run_metrics[SIMULATION_MODE_CONTROL];
    int mode;

    if (app.statistics_cells[0][0] == NULL) return;
    active->elapsed_minutes = app.loaded ? app.simulation.current_time : 0.0;
    for (mode = 0; mode < SIMULATION_MODE_COUNT; ++mode) {
        const RunMetrics *run = &app.run_metrics[mode];
        double count = run->completed_count;
        double throughput = run->elapsed_minutes > 0.0
                          ? count * 60.0 / run->elapsed_minutes : 0.0;
        char values[STAT_METRIC_COUNT][32];
        int metric;

        if (run->completed_count > 0) {
            snprintf(values[STAT_AVERAGE_TOTAL], 32, "%.1f",
                     run->total_time_sum / count);
            snprintf(values[STAT_MAXIMUM_TOTAL], 32, "%.1f",
                     run->total_time_max);
            snprintf(values[STAT_AVERAGE_WAIT], 32, "%.1f",
                     run->wait_time_sum / count);
            snprintf(values[STAT_MAXIMUM_WAIT], 32, "%.1f",
                     run->wait_time_max);
            snprintf(values[STAT_AVERAGE_TRAVEL], 32, "%.1f",
                     run->travel_time_sum / count);
        } else {
            for (metric = STAT_AVERAGE_TOTAL;
                 metric <= STAT_AVERAGE_TRAVEL; ++metric) {
                snprintf(values[metric], 32, "--");
            }
        }
        snprintf(values[STAT_COMPLETED_COUNT], 32, "%d",
                 run->completed_count);
        snprintf(values[STAT_THROUGHPUT], 32, "%.2f", throughput);
        for (metric = 0; metric < STAT_METRIC_COUNT; ++metric) {
            gtk_label_set_text(
                GTK_LABEL(app.statistics_cells[mode][metric]),
                values[metric]);
        }
    }

    snprintf(comparison, sizeof(comparison),
             "完成两种模式后显示本项目相对对照的优化率");
    if (project_run->completed_count > 0 &&
        control_run->completed_count > 0) {
        double project_count = project_run->completed_count;
        double control_count = control_run->completed_count;
        double project_throughput = project_run->elapsed_minutes > 0.0
            ? project_count * 60.0 / project_run->elapsed_minutes : 0.0;
        double control_throughput = control_run->elapsed_minutes > 0.0
            ? control_count * 60.0 / control_run->elapsed_minutes : 0.0;
        double control_total_average =
            control_run->total_time_sum / control_count;
        double control_wait_average =
            control_run->wait_time_sum / control_count;
        double control_travel_average =
            control_run->travel_time_sum / control_count;
        snprintf(comparison, sizeof(comparison),
                 "项目相对对照：平均总完成 %+.1f%%｜最大总完成 %+.1f%%｜平均排队 %+.1f%%｜最大排队 %+.1f%%｜平均行驶 %+.1f%%｜吞吐量 %+.1f%%",
                 control_total_average > 0.0
                    ? (control_total_average
                       - project_run->total_time_sum / project_count)
                      / control_total_average * 100.0 : 0.0,
                 control_run->total_time_max > 0.0
                    ? (control_run->total_time_max
                       - project_run->total_time_max)
                      / control_run->total_time_max * 100.0 : 0.0,
                 control_wait_average > 0.0
                    ? (control_wait_average
                       - project_run->wait_time_sum / project_count)
                      / control_wait_average * 100.0 : 0.0,
                 control_run->wait_time_max > 0.0
                    ? (control_run->wait_time_max
                       - project_run->wait_time_max)
                      / control_run->wait_time_max * 100.0 : 0.0,
                 control_travel_average > 0.0
                    ? (control_travel_average
                       - project_run->travel_time_sum / project_count)
                      / control_travel_average * 100.0 : 0.0,
                 control_throughput > 0.0
                    ? (project_throughput - control_throughput)
                      / control_throughput * 100.0 : 0.0);
    }
    gtk_label_set_text(GTK_LABEL(app.statistics_compare_label), comparison);
}

/* 对照组队列只比较到站时刻和请求序号，实现严格的先来先服务。 */
static int fcfs_request_before(const ChargeRequest *first,
                               const ChargeRequest *second)
{
    if (first->arrival_time != second->arrival_time) {
        return first->arrival_time < second->arrival_time;
    }
    return first->sequence < second->sequence;
}

static int fcfs_queue_push(Station *station, ChargeRequest *request)
{
    if (station == NULL || request == NULL ||
        station->queue_size >= MAX_VEHICLES) return -1;
    station->queue[station->queue_size++] = request;
    return 0;
}

static ChargeRequest *fcfs_queue_pop(Station *station)
{
    ChargeRequest *request;
    int best = 0;
    int i;

    if (station == NULL || station->queue_size <= 0) return NULL;
    for (i = 1; i < station->queue_size; ++i) {
        if (fcfs_request_before(station->queue[i], station->queue[best])) {
            best = i;
        }
    }
    request = station->queue[best];
    for (i = best; i + 1 < station->queue_size; ++i) {
        station->queue[i] = station->queue[i + 1];
    }
    --station->queue_size;
    return request;
}

static int control_simulation_step(void)
{
    int i;

    app.simulation.current_time += app.simulation.step_minutes;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        Vehicle *vehicle = entity_find_vehicle(
            &app.store, app.store.vehicle_ids[i]);
        if (vehicle != NULL && vehicle->state == VEHICLE_TRAVELING &&
            vehicle->arrival_time <= app.simulation.current_time &&
            vehicle->request != NULL && vehicle->request->station != NULL) {
            double old_battery = vehicle->battery_percent;
            vehicle->battery_percent =
                vehicle->request->arrival_battery_percent;
            if (fcfs_queue_push(vehicle->request->station,
                                vehicle->request) == 0) {
                vehicle->node_id = vehicle->request->station->node_id;
                vehicle->request->state = REQUEST_WAITING;
                vehicle->state = VEHICLE_WAITING;
            } else {
                vehicle->battery_percent = old_battery;
            }
        }
    }
    for (i = 0; i < app.store.station_count; ++i) {
        Station *station = entity_find_station(
            &app.store, app.store.station_ids[i]);
        Charger *charger = station == NULL ? NULL : station->chargers;
        while (charger != NULL) {
            if (charger_update(charger, app.simulation.step_minutes)) {
                charger_finish(&app.store, charger);
            }
            charger = charger->next;
        }
    }
    for (i = 0; i < app.store.station_count; ++i) {
        Station *station = entity_find_station(
            &app.store, app.store.station_ids[i]);
        Charger *charger = station == NULL ? NULL : charger_find_free(station);
        while (charger != NULL && station->queue_size > 0) {
            ChargeRequest *request = fcfs_queue_pop(station);
            if (charger_assign(charger, request) != 0) {
                fcfs_queue_push(station, request);
                break;
            }
            charger = charger_find_free(station);
        }
    }
    return 0;
}

static int step_simulation_for_mode(void)
{
    return app.simulation_mode == SIMULATION_MODE_CONTROL
        ? control_simulation_step()
        : simulation_step(&app.simulation);
}

static const Recommendation *recommendation_for_mode(
    const RecommendationList *list)
{
    const Recommendation *best = NULL;
    int i;

    if (app.simulation_mode == SIMULATION_MODE_PROJECT) {
        return recommend_best(list);
    }
    for (i = 0; i < list->count; ++i) {
        const Recommendation *item = &list->items[i];
        if (!item->valid) continue;
        if (best == NULL ||
            item->path.total_distance_km < best->path.total_distance_km ||
            (item->path.total_distance_km == best->path.total_distance_km &&
             item->station_id < best->station_id)) {
            best = item;
        }
    }
    return best;
}

static int entry_int(GtkWidget *entry, int *value)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    char extra;
    return sscanf(text, "%d %c", value, &extra) == 1;
}

static void refresh_selected_vehicle_info(void)
{
    const Vehicle *vehicle = NULL;
    char text[128];
    int vehicle_id;

    if (app.selected_vehicle_info_label == NULL) return;
    if (app.loaded && entry_int(app.vehicle_entry, &vehicle_id)) {
        vehicle = entity_find_vehicle(&app.store, vehicle_id);
    }
    if (vehicle == NULL) {
        gtk_label_set_text(GTK_LABEL(app.selected_vehicle_info_label),
                           "请输入有效车辆编号查看电量信息");
    } else {
        app.selected_vehicle_id = vehicle_id;
        snprintf(text, sizeof(text),
                 "当前车辆 %d · 当前电量 %.1f%% · 目标电量 %.1f%%",
                 vehicle->id, vehicle->battery_percent,
                 vehicle->target_percent);
        gtk_label_set_text(GTK_LABEL(app.selected_vehicle_info_label), text);
    }
    gtk_widget_set_visible(app.selected_vehicle_info_label, TRUE);
}

static void on_vehicle_entry_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    (void)data;
    app.has_selected = 0;
    app.has_path = 0;
    refresh_selected_vehicle_info();
    if (app.drawing_area != NULL) {
        gtk_widget_queue_draw(app.drawing_area);
    }
}

static int entry_double(GtkWidget *entry, double *value)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    char extra;
    return sscanf(text, "%lf %c", value, &extra) == 1;
}

static void list_clear(GtkWidget *list)
{
    GtkWidget *child = gtk_widget_get_first_child(list);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
        child = next;
    }
}

static void set_widget_margins(GtkWidget *widget,
                               int vertical, int horizontal)
{
    gtk_widget_set_margin_top(widget, vertical);
    gtk_widget_set_margin_bottom(widget, vertical);
    gtk_widget_set_margin_start(widget, horizontal);
    gtk_widget_set_margin_end(widget, horizontal);
}

static void list_append_text(GtkWidget *list, const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    set_widget_margins(label, 4, 6);
    gtk_list_box_append(GTK_LIST_BOX(list), label);
}

static GtkWidget *list_append_record(GtkWidget *list, const char *title,
                                     const char *subtitle, int entity_id)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *subtitle_label = gtk_label_new(subtitle);

    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(title_label, "record-title");
    gtk_widget_add_css_class(subtitle_label, "record-subtitle");
    set_widget_margins(box, 7, 10);
    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), subtitle_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "entity-id", GINT_TO_POINTER(entity_id));
    gtk_list_box_append(GTK_LIST_BOX(list), row);
    return row;
}

static void flow_box_clear(GtkWidget *flow_box)
{
    GtkWidget *child = gtk_widget_get_first_child(flow_box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(flow_box), child);
        child = next;
    }
}

static void flow_box_append_text(GtkWidget *flow_box, const char *text)
{
    GtkWidget *child = gtk_flow_box_child_new();
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    set_widget_margins(label, 10, 10);
    gtk_flow_box_child_set_child(GTK_FLOW_BOX_CHILD(child), label);
    gtk_widget_set_sensitive(child, FALSE);
    gtk_flow_box_append(GTK_FLOW_BOX(flow_box), child);
}

static GtkFlowBoxChild *flow_box_append_charger(GtkWidget *flow_box,
                                                const Charger *charger)
{
    GtkWidget *child = gtk_flow_box_child_new();
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title;
    GtkWidget *state;
    GtkWidget *power;
    char text[96];

    snprintf(text, sizeof(text), "充电桩 %d", charger->id);
    title = gtk_label_new(text);
    gtk_widget_add_css_class(title, "record-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    snprintf(text, sizeof(text), "状态：%s",
             charger_state_name(charger->state));
    state = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(state), 0.0f);
    snprintf(text, sizeof(text), "功率：%.1f kW", charger->power_kw);
    power = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(power), 0.0f);

    gtk_widget_add_css_class(card, "charger-card");
    gtk_widget_add_css_class(card,
        charger->state == CHARGER_FREE ? "charger-free" :
        charger->state == CHARGER_BUSY ? "charger-busy" : "charger-fault");
    gtk_widget_set_size_request(card, 135, 92);
    gtk_box_append(GTK_BOX(card), title);
    gtk_box_append(GTK_BOX(card), state);
    gtk_box_append(GTK_BOX(card), power);
    gtk_flow_box_child_set_child(GTK_FLOW_BOX_CHILD(child), card);
    g_object_set_data(G_OBJECT(child), "entity-id",
                      GINT_TO_POINTER(charger->id));
    gtk_flow_box_append(GTK_FLOW_BOX(flow_box), child);
    return GTK_FLOW_BOX_CHILD(child);
}

/* 在站内三列表格中加入带车辆图标的信息卡。 */
static GtkWidget *list_append_station_vehicle(GtkWidget *list,
                                              const Vehicle *vehicle,
                                              const char *details,
                                              const char *state_class)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon = gtk_label_new("车");
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title;
    char title_text[64];

    snprintf(title_text, sizeof(title_text), "车辆 %d", vehicle->id);
    title = gtk_label_new(title_text);
    gtk_widget_add_css_class(icon, "vehicle-map-icon");
    gtk_widget_add_css_class(icon, state_class);
    gtk_widget_add_css_class(title, "record-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_set_hexpand(text_box, TRUE);
    gtk_box_append(GTK_BOX(text_box), title);
    if (details != NULL && details[0] != '\0') {
        GtkWidget *subtitle = gtk_label_new(details);
        gtk_widget_add_css_class(subtitle, "record-subtitle");
        gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
        gtk_label_set_yalign(GTK_LABEL(subtitle), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
        gtk_box_append(GTK_BOX(text_box), subtitle);
    }
    set_widget_margins(box, 7, 8);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), text_box);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "entity-id",
                      GINT_TO_POINTER(vehicle->id));
    gtk_list_box_append(GTK_LIST_BOX(list), row);
    return row;
}

static const char *recommendation_reason_zh(const char *reason)
{
    if (strcmp(reason, "road unreachable") == 0) return "道路不可达";
    if (strcmp(reason, "no working charger") == 0) return "没有可用充电桩";
    if (strcmp(reason, "battery not enough") == 0) return "当前电量不足以到达";
    if (strcmp(reason, "station unavailable") == 0) return "站点当前不可用";
    return reason;
}

static void stop_timer(void)
{
    timer_stop(&app.timer);
    app.visual_time = app.loaded ? app.simulation.current_time : 0.0;
}

static void refresh_view(void);

static int all_vehicles_finished(void)
{
    int count;
    int i;

    if (!app.loaded) return 0;
    count = simulation_vehicle_count(&app.simulation);
    if (count <= 0) return 0;
    for (i = 0; i < count; ++i) {
        const Vehicle *vehicle = simulation_vehicle_at(&app.simulation, i);
        if (vehicle == NULL || vehicle->state != VEHICLE_FINISHED) return 0;
    }
    return 1;
}

static void update_status(void)
{
    char text[256];
    int traveling = 0;
    int waiting = 0;
    int charging = 0;
    int i;

    if (!app.loaded) {
        gtk_label_set_text(GTK_LABEL(app.status_label), "尚未加载数据");
        return;
    }
    track_vehicle_completions();
    for (i = 0; i < simulation_vehicle_count(&app.simulation); ++i) {
        const Vehicle *vehicle = simulation_vehicle_at(&app.simulation, i);
        if (vehicle == NULL) continue;
        if (vehicle->state == VEHICLE_TRAVELING) ++traveling;
        if (vehicle->state == VEHICLE_WAITING) ++waiting;
        if (vehicle->state == VEHICLE_CHARGING) ++charging;
    }
    snprintf(text, sizeof(text),
             "仿真 %.0f/%.0f 分钟｜行驶 %d｜等待 %d｜充电 %d",
             app.simulation.current_time, app.simulation_time_limit,
             traveling, waiting, charging);
    gtk_label_set_text(GTK_LABEL(app.status_label), text);
}

static void refresh_chargers(void)
{
    Station *station;
    Charger *charger;
    char heading[192];
    char summary[256];
    int free_count = 0;
    int busy_count = 0;
    int fault_count = 0;

    flow_box_clear(app.charger_list);
    if (!app.loaded || app.selected_station_id < 0) {
        gtk_label_set_text(GTK_LABEL(app.station_operation_title),
                           "请选择一个充电站");
        gtk_label_set_text(GTK_LABEL(app.station_status_label),
                           "尚未选择站点");
        flow_box_append_text(app.charger_list,
                             "输入充电站编号后显示所属充电桩");
        return;
    }
    station = entity_find_station(&app.store, app.selected_station_id);
    if (station == NULL) {
        gtk_label_set_text(GTK_LABEL(app.station_operation_title),
                           "充电站不存在");
        gtk_label_set_text(GTK_LABEL(app.station_status_label),
                           "无法读取站点状态");
        flow_box_append_text(app.charger_list, "无法读取该站点的充电桩");
        return;
    }

    snprintf(heading, sizeof(heading), "站点 %d · %s 的充电桩与操作",
             station->id, station->name);
    gtk_label_set_text(GTK_LABEL(app.station_operation_title), heading);

    charger = station->chargers;
    if (charger == NULL) {
        flow_box_append_text(app.charger_list, "该站点暂时没有充电桩");
    }
    while (charger != NULL) {
        GtkFlowBoxChild *child;

        if (charger->state == CHARGER_FREE) ++free_count;
        if (charger->state == CHARGER_BUSY) ++busy_count;
        if (charger->state == CHARGER_FAULT) ++fault_count;

        child = flow_box_append_charger(app.charger_list, charger);
        if (charger->id == app.selected_charger_id) {
            gtk_flow_box_select_child(GTK_FLOW_BOX(app.charger_list), child);
        }
        charger = charger->next;
    }
    snprintf(summary, sizeof(summary),
             "空闲 %d · 服务中 %d · 故障 %d · 排队 %d 辆",
             free_count, busy_count, fault_count, station->queue_size);
    gtk_label_set_text(GTK_LABEL(app.station_status_label), summary);
}

/* 刷新所选站点的充电中和优先级等待车辆。 */
static void refresh_queue(void)
{
    Station *station;
    Station copy;
    ChargeRequest request_copies[MAX_VEHICLES];
    ChargeRequest *request;
    Charger *charger;
    int charging_count = 0;
    int order = 1;
    int i;

    list_clear(app.charging_vehicle_list);
    list_clear(app.waiting_vehicle_list);
    if (!app.loaded || app.selected_station_id < 0) {
        list_append_text(app.charging_vehicle_list, "请选择充电站");
        list_append_text(app.waiting_vehicle_list, "请选择充电站");
        return;
    }
    station = entity_find_station(&app.store, app.selected_station_id);
    if (station == NULL) {
        list_append_text(app.charging_vehicle_list, "站点不存在");
        list_append_text(app.waiting_vehicle_list, "站点不存在");
        return;
    }

    charger = station->chargers;
    while (charger != NULL) {
        if (charger->state == CHARGER_BUSY && charger->request != NULL &&
            charger->request->vehicle != NULL) {
            const Vehicle *vehicle = charger->request->vehicle;
            char details[160];
            snprintf(details, sizeof(details),
                     "充电桩：%d\n电量：%.1f%%\n剩余：%.1f 分钟",
                     charger->id, vehicle->battery_percent,
                     charger->remaining_minutes);
            list_append_station_vehicle(app.charging_vehicle_list, vehicle,
                                        details, "vehicle-charging");
            ++charging_count;
        }
        charger = charger->next;
    }

    copy = *station;
    for (i = 0; i < station->queue_size; ++i) {
        request_copies[i] = *station->queue[i];
        copy.queue[i] = &request_copies[i];
    }
    if (app.simulation_mode == SIMULATION_MODE_PROJECT) {
        scheduler_rebuild(&copy, app.simulation.current_time);
    }
    while ((request = app.simulation_mode == SIMULATION_MODE_CONTROL
                      ? fcfs_queue_pop(&copy) : scheduler_pop(&copy)) != NULL) {
        char details[192];
        if (app.simulation_mode == SIMULATION_MODE_CONTROL) {
            snprintf(details, sizeof(details),
                     "排队位置：第%d位\n到站：%.1f 分钟\n先来先服务",
                     order++, request->arrival_time);
        } else {
            snprintf(details, sizeof(details),
                     "排队位置：第%d位\n优先级：%.1f\n等待：%.1f 分钟",
                     order++, request->priority, request->wait_time);
        }
        list_append_station_vehicle(app.waiting_vehicle_list,
                                    request->vehicle, details,
                                    "vehicle-waiting");
    }
    if (charging_count == 0)
        list_append_text(app.charging_vehicle_list, "暂无充电车辆");
    if (order == 1) {
        list_append_text(app.waiting_vehicle_list, "暂无排队车辆");
    }
}

static void refresh_view(void)
{
    update_status();
    update_statistics_display();
    update_batch_status();
    refresh_selected_vehicle_info();
    refresh_chargers();
    refresh_queue();
    gtk_widget_queue_draw(app.drawing_area);
}

static int load_data(void)
{
    DataFiles files = {
        "data/input/nodes.csv", "data/input/roads.csv",
        "data/input/stations.csv", "data/input/chargers.csv",
        "data/input/vehicles.csv"
    };
    Graph new_graph;
    EntityStore new_store;
    LoadError error;
    int i;

    graph_init(&new_graph);
    entity_init(&new_store);
    if (fileio_load_all(&new_graph, &new_store, &files, &error) != 0) {
        output_line("加载失败：%s，第 %d 行，%s。",
                    error.file, error.line, error.message);
        graph_free(&new_graph);
        entity_free(&new_store);
        return -1;
    }
    stop_timer();
    if (app.loaded) {
        simulation_reset(&app.simulation);
        entity_free(&app.store);
        graph_free(&app.graph);
    }
    app.graph = new_graph;
    app.store = new_store;
    if (simulation_init(&app.simulation, &app.graph, &app.store, 1.0) != 0) {
        entity_free(&app.store);
        graph_free(&app.graph);
        app.loaded = 0;
        return -1;
    }

    app.loaded = 1;
    app.scenario_seed = (unsigned int)g_get_real_time();
    app.traffic_random_state = app.scenario_seed ^ 0x85ebca6bu;
    memset(app.run_metrics, 0, sizeof(app.run_metrics));
    memset(app.vehicle_metrics, 0, sizeof(app.vehicle_metrics));
    memset(app.completion_counted, 0, sizeof(app.completion_counted));
    for (i = 0; i < MAX_VEHICLES; ++i) {
        app.vehicle_respawn_time[i] = -1.0;
    }
    app.next_auto_submit_time = app.auto_submit_enabled ? 10.0 : 0.0;
    for (i = 0; i < app.store.vehicle_count; ++i) {
        Vehicle *vehicle = entity_find_vehicle(
            &app.store, app.store.vehicle_ids[i]);
        if (vehicle != NULL) {
            app.original_target_percent[i] = vehicle->target_percent;
            app.original_urgency[i] = vehicle->urgency;
        }
    }
    reset_vehicle_motions();
    app.has_selected = 0;
    app.has_path = 0;
    app.selected_vehicle_id = -1;
    app.selected_station_id = app.store.station_count > 0
        ? app.store.station_ids[0] : -1;
    app.selected_charger_id = -1;
    prepare_vehicle_batches();
    if (app.store.station_count > 0) {
        char station_id_text[32];
        snprintf(station_id_text, sizeof(station_id_text), "%d",
                 app.store.station_ids[0]);
        gtk_editable_set_text(GTK_EDITABLE(app.station_entry),
                              station_id_text);
    }
    if (app.store.vehicle_count > 0) {
        char vehicle_id_text[32];
        snprintf(vehicle_id_text, sizeof(vehicle_id_text), "%d",
                 app.store.vehicle_ids[0]);
        gtk_editable_set_text(GTK_EDITABLE(app.vehicle_entry),
                              vehicle_id_text);
    }
    refresh_selected_vehicle_info();

    list_clear(app.recommendation_list);
    list_append_text(app.recommendation_list,
                     "请选择车辆并点击“计算推荐”查看站点比较。");
    refresh_view();
    return 0;
}

static Station *selected_station(void)
{
    Station *station;

    if (!app.loaded || app.selected_station_id < 0) {
        output_line("请先选择一个充电站。");
        return NULL;
    }
    station = entity_find_station(&app.store, app.selected_station_id);
    if (station == NULL) {
        output_line("所选充电站不存在。");
        return NULL;
    }
    return station;
}

static void on_recommendation_selected(GtkListBox *box,
                                       GtkListBoxRow *row, gpointer data)
{
    const Recommendation *recommendation;
    int vehicle_id;
    (void)box;
    (void)data;

    if (row == NULL || !entry_int(app.vehicle_entry, &vehicle_id)) return;
    recommendation = g_object_get_data(G_OBJECT(row), "recommendation");
    if (recommendation == NULL || !recommendation->valid) return;
    app.selected = *recommendation;
    app.has_selected = 1;
    app.has_path = 1;
    app.selected_vehicle_id = vehicle_id;
    app.selected_station_id = recommendation->station_id;
    refresh_view();
    output_line("已选择站点 %d，可提交当前选择。",
                recommendation->station_id);
}

static void recommend_vehicle(void)
{
    RecommendationList list;
    Vehicle *vehicle;
    int vehicle_id;
    int i;
    int valid_count = 0;
    GtkListBoxRow *first_valid_row = NULL;
    GtkListBoxRow *preferred_row = NULL;
    double preferred_distance = 1e30;

    if (!app.loaded || !entry_int(app.vehicle_entry, &vehicle_id)) {
        output_line("请输入有效车辆编号。");
        return;
    }
    vehicle = entity_find_vehicle(&app.store, vehicle_id);
    if (vehicle == NULL ||
        recommend_all(&app.graph, &app.store, vehicle,
                      app.simulation.current_time, &list) != 0) {
        output_line("车辆不存在或推荐计算失败。");
        return;
    }

    list_clear(app.recommendation_list);
    output_line("车辆 %d：节点 %d，电量 %.1f%%，紧急程度 %d。",
                vehicle->id, vehicle->node_id,
                vehicle->battery_percent, vehicle->urgency);
    app.has_selected = 0;
    app.has_path = 0;
    for (i = 0; i < list.count; ++i) {
        const Recommendation *item = &list.items[i];
        GtkWidget *row;
        char title[160];
        char subtitle[256];
        if (!item->valid) {
            snprintf(title, sizeof(title), "站点 %d　暂不可用", item->station_id);
            snprintf(subtitle, sizeof(subtitle), "%s",
                     recommendation_reason_zh(item->reason));
            row = list_append_record(app.recommendation_list, title, subtitle,
                                     item->station_id);
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
            continue;
        }
        ++valid_count;
        snprintf(title, sizeof(title), "%d. 站点 %d　总计 %.1f 分钟",
                 valid_count, item->station_id, item->total_minutes);
        snprintf(subtitle, sizeof(subtitle),
                 "距离 %.2f km · 行驶 %.1f · 等待 %.1f · 充电 %.1f · 到站电量 %.1f%%",
                 item->path.total_distance_km,
                 item->travel_minutes, item->wait_minutes,
                 item->charge_minutes, item->arrival_battery_percent);
        row = list_append_record(app.recommendation_list, title, subtitle,
                                 item->station_id);
        {
            Recommendation *copy = g_new(Recommendation, 1);
            *copy = *item;
            g_object_set_data_full(G_OBJECT(row), "recommendation",
                                   copy, g_free);
        }
        if (first_valid_row == NULL) {
            first_valid_row = GTK_LIST_BOX_ROW(row);
        }
        if (app.simulation_mode == SIMULATION_MODE_CONTROL &&
            item->path.total_distance_km < preferred_distance) {
            preferred_distance = item->path.total_distance_km;
            preferred_row = GTK_LIST_BOX_ROW(row);
        }
    }
    if (preferred_row == NULL) preferred_row = first_valid_row;
    if (preferred_row != NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(app.recommendation_list),
                                preferred_row);
    }
    output_line(app.simulation_mode == SIMULATION_MODE_CONTROL
                ? "对照模式已选择距离最近站点：%d 个站点可用。"
                : "推荐计算完成：%d 个站点可用，详细比较见左侧列表。",
                valid_count);
    refresh_view();
}

static void submit_selected(void)
{
    if (!app.loaded || !app.has_selected ||
        simulation_submit(&app.simulation, app.selected_vehicle_id,
                          &app.selected) != 0) {
        output_line("提交失败：请先为一辆空闲车辆计算推荐。");
        return;
    }
    track_vehicle_motion(app.selected_vehicle_id, &app.selected);
    record_vehicle_submission(app.selected_vehicle_id, &app.selected);
    clear_vehicle_completion_marker(app.selected_vehicle_id);
    remove_continuous_pending_vehicle(app.selected_vehicle_id);
    mark_vehicle_submitted(app.selected_vehicle_id);
    ++app.total_submission_count;
    update_batch_status();
    output_line("车辆 %d 已提交到站点 %d，预计 %.1f 分钟到达。",
                app.selected_vehicle_id, app.selected.station_id,
                app.selected.travel_minutes);
    app.has_selected = 0;
    refresh_view();
}

static int submit_continuous_batch(void)
{
    int vehicle_ids[12];
    int batch_size;
    int submitted = 0;
    int i;

    if (app.continuous_pending_count <= 0) return 0;
    batch_size = app.continuous_pending_count;
    if (batch_size > 12) batch_size = scenario_random_int(8, 13);
    for (i = 0; i < batch_size; ++i) {
        vehicle_ids[i] = app.continuous_pending_ids[i];
    }
    for (i = 0; i < batch_size; ++i) {
        Vehicle *vehicle = entity_find_vehicle(&app.store, vehicle_ids[i]);
        RecommendationList list;
        const Recommendation *best;

        if (vehicle == NULL || vehicle->state != VEHICLE_IDLE) continue;
        if (recommend_all(&app.graph, &app.store, vehicle,
                          app.simulation.current_time, &list) != 0) continue;
        best = recommendation_for_mode(&list);
        if (best != NULL &&
            simulation_submit(&app.simulation, vehicle->id, best) == 0) {
            track_vehicle_motion(vehicle->id, best);
            record_vehicle_submission(vehicle->id, best);
            remove_continuous_pending_vehicle(vehicle->id);
            ++app.total_submission_count;
            ++submitted;
        }
    }
    if (submitted > 0) ++app.continuous_batches_submitted;
    app.has_selected = 0;
    app.has_path = 0;
    update_batch_status();
    refresh_view();
    output_line("循环车流第 %d 批已提交 %d 辆，仍待提交 %d 辆。",
                app.continuous_batches_submitted, submitted,
                app.continuous_pending_count);
    return submitted;
}

static int submit_next_batch(void)
{
    int submitted = 0;
    int remaining = 0;
    int batch_number;
    int start;
    int size;
    int i;

    if (!app.loaded) return 0;
    if (app.next_batch >= app.batch_count) {
        if (app.continuous_pending_count > 0) {
            return submit_continuous_batch();
        }
        output_line("当前没有待提交车辆。");
        return 0;
    }
    batch_number = app.next_batch;
    start = app.batch_starts[batch_number];
    size = app.batch_sizes[batch_number];
    for (i = start; i < start + size; ++i) {
        Vehicle *vehicle;
        RecommendationList list;
        const Recommendation *best;

        if (app.batch_submitted[i]) continue;
        vehicle = entity_find_vehicle(&app.store, app.batch_vehicle_ids[i]);
        if (vehicle == NULL || vehicle->state != VEHICLE_IDLE) continue;
        if (recommend_all(&app.graph, &app.store, vehicle,
                          app.simulation.current_time, &list) != 0) continue;
        best = recommendation_for_mode(&list);
        if (best != NULL &&
            simulation_submit(&app.simulation, vehicle->id, best) == 0) {
            track_vehicle_motion(vehicle->id, best);
            record_vehicle_submission(vehicle->id, best);
            mark_vehicle_submitted(vehicle->id);
            ++app.total_submission_count;
            ++submitted;
        }
    }
    for (i = start; i < start + size; ++i) {
        if (!app.batch_submitted[i]) ++remaining;
    }
    if (remaining == 0) ++app.next_batch;
    update_batch_status();
    app.has_selected = 0;
    app.has_path = 0;
    refresh_view();
    if (remaining == 0) {
        output_line("第 %d 批已提交 %d 辆车。",
                    batch_number + 1, submitted);
    } else {
        output_line("第 %d 批已提交 %d 辆，另有 %d 辆等待重试。",
                    batch_number + 1, submitted, remaining);
    }
    return submitted;
}

static void add_charger(void)
{
    Station *station = selected_station();
    int charger_id;
    double power_kw;

    if (station == NULL) return;
    if (!entry_int(app.charger_entry, &charger_id) ||
        !entry_double(app.power_entry, &power_kw) || power_kw <= 0.0) {
        output_line("请输入有效充电桩编号和正数功率。");
        return;
    }
    if (charger_add(&app.store, station->id, charger_id,
                    power_kw, CHARGER_FREE) == 0) {
        app.selected_charger_id = charger_id;
        output_line("新增成功：桩 %d 已加入站点 %d 的单链表和哈希索引。",
                    charger_id, station->id);
    } else {
        output_line("新增失败：请检查站点、桩编号和容量限制。");
    }
    refresh_view();
}

static void remove_charger(void)
{
    Station *station = selected_station();
    int charger_id = app.selected_charger_id;

    if (station == NULL || charger_id < 0) {
        output_line("请先选择一个充电桩。");
        return;
    }
    if (charger_remove(&app.store, station->id, charger_id) == 0) {
        app.selected_charger_id = -1;
        output_line("桩 %d 已从单链表和哈希索引中删除。", charger_id);
    } else {
        output_line("删除失败：充电桩不存在、不属于该站点或正在工作。");
    }
    refresh_view();
}

static void set_charger_state(int repair)
{
    Station *station;
    Charger *charger;
    int charger_id = app.selected_charger_id;
    int result;

    station = selected_station();
    if (station == NULL || charger_id < 0) {
        output_line("请先选择一个充电桩。");
        return;
    }
    charger = station->chargers;
    while (charger != NULL && charger->id != charger_id) {
        charger = charger->next;
    }
    if (charger == NULL) {
        output_line("操作失败：充电桩 %d 不属于当前站点 %d。",
                    charger_id, station->id);
        return;
    }
    result = repair
        ? simulation_repair_charger(&app.simulation, charger_id)
        : simulation_break_charger(&app.simulation, charger_id);
    output_line("充电桩 %d %s。", charger_id,
                result == 0 ? (repair ? "已修复" : "已设为故障")
                            : "状态修改失败");
    refresh_view();
}

static int simulation_time_reached(void)
{
    return app.loaded && app.simulation_time_limit > 0.0 &&
           app.simulation.current_time + 0.000001 >=
               app.simulation_time_limit;
}

static gboolean finish_timer_if_needed(void)
{
    if (!app.loaded) {
        timer_finish(&app.timer);
        return TRUE;
    }
    if (simulation_time_reached()) {
        timer_finish(&app.timer);
        app.visual_time = app.simulation.current_time;
        refresh_view();
        output_line("已达到设定仿真时间 %.0f 分钟，仿真自动停止。",
                    app.simulation_time_limit);
        return TRUE;
    }
    if (simulation_has_active(&app.simulation) || has_pending_batches() ||
        app.continuous_traffic_enabled) return FALSE;

    timer_finish(&app.timer);
    app.visual_time = app.simulation.current_time;
    refresh_view();
    output_line(all_vehicles_finished()
                ? "所有车辆全部充电完成，仿真已自动停止。"
                : "当前充电任务已完成，仿真已自动停止。");
    return TRUE;
}

static gboolean timer_tick(gpointer data)
{
    int steps;
    int stepped = 0;
    (void)data;
    if (finish_timer_if_needed()) return G_SOURCE_REMOVE;

    steps = timer_collect_steps(&app.timer);
    while (steps > 0 &&
           !simulation_time_reached() &&
           (simulation_has_active(&app.simulation) || has_pending_batches() ||
            app.continuous_traffic_enabled)) {
        if (step_simulation_for_mode() != 0) break;
        track_statistics_after_step();
        process_continuous_traffic();
        if (app.auto_submit_enabled &&
            app.simulation.current_time + 0.000001 >=
                app.next_auto_submit_time) {
            if (has_pending_batches()) submit_next_batch();
            do {
                app.next_auto_submit_time += 10.0;
            } while (app.next_auto_submit_time <=
                     app.simulation.current_time + 0.000001);
        }
        --steps;
        stepped = 1;
    }
    app.visual_time = app.simulation.current_time
                    + timer_progress(&app.timer)
                    * app.simulation.step_minutes;
    if (stepped) {
        refresh_view();
    } else {
        gtk_widget_queue_draw(app.drawing_area);
    }
    return finish_timer_if_needed() ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static void start_simulation(void)
{
    if (!app.loaded) {
        output_line("请先加载车辆数据。");
        return;
    }
    if (simulation_time_reached()) {
        output_line("当前已达到 %.0f 分钟，请增大仿真时长或重置仿真。",
                    app.simulation_time_limit);
        return;
    }
    if (!simulation_has_active(&app.simulation) && !has_pending_batches() &&
        !app.continuous_traffic_enabled) {
        output_line("当前没有进行中的充电任务。");
        return;
    }
    if (!timer_is_running(&app.timer)) {
        app.visual_time = app.simulation.current_time;
        if (timer_start(&app.timer, UI_FRAME_INTERVAL_MS,
                        timer_tick, NULL) == 0) {
            output_line("连续仿真已开始。");
        } else {
            output_line("定时器启动失败。");
        }
    }
}

static void reset_simulation(void)
{
    int i;

    if (!app.loaded) return;
    stop_timer();
    simulation_reset(&app.simulation);
    app.traffic_random_state = app.scenario_seed ^ 0x85ebca6bu;
    reset_current_statistics();
    for (i = 0; i < app.store.vehicle_count; ++i) {
        Vehicle *vehicle = entity_find_vehicle(
            &app.store, app.store.vehicle_ids[i]);
        if (vehicle != NULL) {
            vehicle->target_percent = app.original_target_percent[i];
            vehicle->urgency = app.original_urgency[i];
        }
        app.completion_counted[i] = 0;
        app.vehicle_respawn_time[i] = -1.0;
    }
    app.next_auto_submit_time = app.auto_submit_enabled ? 10.0 : 0.0;
    reset_vehicle_motions();
    prepare_vehicle_batches();
    app.has_selected = 0;
    app.has_path = 0;
    refresh_selected_vehicle_info();
    list_clear(app.recommendation_list);
    list_append_text(app.recommendation_list,
                     "请选择车辆并点击“计算推荐”查看站点比较。");
    output_line("仿真已恢复到初始状态。");
    refresh_view();
}

static double edge_travel_minutes(int from_id, int to_id);

/* 已提交车辆行驶时，只保留当前路段及其前方尚未经过的推荐路线。 */
static int selected_edge(int first_id, int second_id)
{
    int first_visible_segment = 0;
    int i;

    if (!app.has_path) return 0;
    if (!app.has_selected) {
        const Vehicle *vehicle = entity_find_vehicle(
            &app.store, app.selected_vehicle_id);
        VehicleMotion *motion = find_vehicle_motion(app.selected_vehicle_id);
        double elapsed;

        if (vehicle == NULL || vehicle->state != VEHICLE_TRAVELING ||
            motion == NULL || motion->path_length < 2) {
            return 0;
        }
        elapsed = app.visual_time - motion->start_time;
        if (elapsed < 0.0) elapsed = 0.0;
        for (i = 0; i + 1 < motion->path_length; ++i) {
            double segment = edge_travel_minutes(motion->node_ids[i],
                                                 motion->node_ids[i + 1]);
            if (segment <= 0.0) {
                segment = motion->duration / (motion->path_length - 1);
            }
            if (elapsed < segment || i + 2 >= motion->path_length) {
                first_visible_segment = i;
                break;
            }
            elapsed -= segment;
        }
    }
    for (i = first_visible_segment;
         i + 1 < app.selected.path.path_length; ++i) {
        int a = app.selected.path.node_ids[i];
        int b = app.selected.path.node_ids[i + 1];
        if ((a == first_id && b == second_id) ||
            (a == second_id && b == first_id)) return 1;
    }
    return 0;
}

static int reverse_edge_exists(int from_index, int to_index)
{
    const Edge *edge = app.graph.nodes[to_index].edges;
    while (edge != NULL) {
        if (edge->to_index == from_index) return 1;
        edge = edge->next;
    }
    return 0;
}

/* 把示意地图坐标转换为当前缩放和平移后的屏幕坐标。 */
static void map_point(double map_x, double map_y, int width, int height,
                      double *x, double *y)
{
    const double margin = 28.0;
    double min_x = 0.0;
    double max_x = 500.0;
    double min_y = 0.0;
    double max_y = 410.0;
    double range_x;
    double range_y;
    double usable_width = width - margin * 2.0;
    double usable_height = height - margin * 2.0;
    double scale;
    double left;
    double top;
    double center_x = width / 2.0;
    double center_y = height / 2.0;
    int i;

    if (app.graph.node_count > 0) {
        min_x = max_x = app.graph.nodes[0].x;
        min_y = max_y = app.graph.nodes[0].y;
        for (i = 1; i < app.graph.node_count; ++i) {
            if (app.graph.nodes[i].x < min_x) min_x = app.graph.nodes[i].x;
            if (app.graph.nodes[i].x > max_x) max_x = app.graph.nodes[i].x;
            if (app.graph.nodes[i].y < min_y) min_y = app.graph.nodes[i].y;
            if (app.graph.nodes[i].y > max_y) max_y = app.graph.nodes[i].y;
        }
    }
    if (usable_width < 1.0) usable_width = 1.0;
    if (usable_height < 1.0) usable_height = 1.0;
    range_x = max_x - min_x;
    range_y = max_y - min_y;
    if (range_x < 1.0) range_x = 1.0;
    if (range_y < 1.0) range_y = 1.0;
    scale = MIN(usable_width / range_x, usable_height / range_y);
    left = (width - range_x * scale) / 2.0;
    top = (height - range_y * scale) / 2.0;
    *x = left + (map_x - min_x) * scale;
    *y = top + (map_y - min_y) * scale;
    *x = center_x + (*x - center_x) * app.map_zoom + app.map_offset_x;
    *y = center_y + (*y - center_y) * app.map_zoom + app.map_offset_y;
}

static void node_point(const GraphNode *node, int width, int height,
                       double *x, double *y)
{
    map_point(node->x, node->y, width, height, x, y);
}

static double edge_travel_minutes(int from_id, int to_id)
{
    int from_index = graph_find_node(&app.graph, from_id);
    int to_index = graph_find_node(&app.graph, to_id);
    const Edge *edge;

    if (from_index < 0 || to_index < 0) return 0.0;
    edge = app.graph.nodes[from_index].edges;
    while (edge != NULL) {
        if (edge->to_index == to_index) return edge->travel_minutes;
        edge = edge->next;
    }
    return 0.0;
}

static int vehicle_point(const Vehicle *vehicle, int width, int height,
                         double *x, double *y)
{
    VehicleMotion *motion;
    int node_index;

    if (vehicle == NULL) return 0;
    motion = find_vehicle_motion(vehicle->id);
    if (vehicle->state == VEHICLE_TRAVELING && motion != NULL &&
        motion->path_length > 1) {
        double elapsed = app.visual_time - motion->start_time;
        double fallback = motion->duration / (motion->path_length - 1);
        int i;

        if (elapsed < 0.0) elapsed = 0.0;
        if (elapsed > motion->duration) elapsed = motion->duration;
        for (i = 0; i + 1 < motion->path_length; ++i) {
            double segment = edge_travel_minutes(motion->node_ids[i],
                                                 motion->node_ids[i + 1]);
            int first_index;
            int second_index;
            double x1, y1, x2, y2;
            double ratio;
            if (segment <= 0.0) segment = fallback > 0.0 ? fallback : 1.0;
            if (elapsed > segment && i + 2 < motion->path_length) {
                elapsed -= segment;
                continue;
            }
            first_index = graph_find_node(&app.graph, motion->node_ids[i]);
            second_index = graph_find_node(&app.graph, motion->node_ids[i + 1]);
            if (first_index < 0 || second_index < 0) break;
            node_point(&app.graph.nodes[first_index], width, height, &x1, &y1);
            node_point(&app.graph.nodes[second_index], width, height, &x2, &y2);
            ratio = segment > 0.0 ? elapsed / segment : 1.0;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            ratio = ratio * ratio * (3.0 - 2.0 * ratio);
            *x = x1 + (x2 - x1) * ratio;
            *y = y1 + (y2 - y1) * ratio;
            return 1;
        }
        node_index = graph_find_node(
            &app.graph, motion->node_ids[motion->path_length - 1]);
    } else {
        node_index = graph_find_node(&app.graph, vehicle->node_id);
    }
    if (node_index < 0) return 0;
    node_point(&app.graph.nodes[node_index], width, height, x, y);
    return 1;
}

static void draw_vehicles(cairo_t *cr, int width, int height)
{
    int i;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10.0);
    for (i = 0; i < simulation_vehicle_count(&app.simulation); ++i) {
        const Vehicle *vehicle = simulation_vehicle_at(&app.simulation, i);
        double x, y;
        int show_label;
        double box_width;
        double box_height;
        double battery_width;
        double icon_x;
        double icon_size;
        cairo_text_extents_t extents;
        char label[24];
        if (!vehicle_is_on_map(vehicle)) continue;
        if (!vehicle_point(vehicle, width, height, &x, &y)) continue;
        show_label = app.map_zoom >= 1.55 ||
                     vehicle->id == app.selected_vehicle_id;
        box_width = show_label ? 58.0 : 25.0;
        box_height = show_label ? 28.0 : 22.0;

        if (vehicle->state == VEHICLE_TRAVELING) {
            cairo_set_source_rgb(cr, 0.95, 0.48, 0.08);
            if (show_label) box_width = 62.0;
        } else {
            cairo_set_source_rgb(cr, 0.18, 0.48, 0.78);
        }
        cairo_rectangle(cr, x - box_width / 2.0, y - box_height / 2.0,
                        box_width, box_height);
        cairo_fill_preserve(cr);
        if (vehicle->id == app.selected_vehicle_id) {
            cairo_set_source_rgb(cr, 1.0, 0.82, 0.18);
            cairo_set_line_width(cr, 2.5);
        } else {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.2);
        }
        cairo_stroke(cr);

        icon_x = show_label ? x - box_width / 2.0 + 14.0 : x;
        icon_size = show_label ? 19.0 : 17.0;
        if (app.vehicle_icon != NULL) {
            draw_map_icon(cr, app.vehicle_icon, icon_x, y - 1.5, icon_size);
        }

        if (show_label) {
            snprintf(label, sizeof(label), "V%d", vehicle->id);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_font_size(cr, 12.0);
            cairo_text_extents(cr, label, &extents);
            cairo_move_to(cr, x + 6.0 - extents.width / 2.0, y + 3.0);
            cairo_show_text(cr, label);
        } else if (app.vehicle_icon == NULL) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, x - 6.0, y + 2.0);
            cairo_line_to(cr, x - 3.0, y - 4.0);
            cairo_line_to(cr, x + 4.0, y - 4.0);
            cairo_line_to(cr, x + 7.0, y + 2.0);
            cairo_stroke(cr);
        }

        battery_width = (box_width - 4.0) * vehicle->battery_percent / 100.0;
        if (battery_width < 0.0) battery_width = 0.0;
        if (battery_width > box_width - 4.0) battery_width = box_width - 4.0;
        if (vehicle->battery_percent < 20.0) {
            cairo_set_source_rgb(cr, 1.0, 0.35, 0.30);
        } else if (vehicle->battery_percent < 50.0) {
            cairo_set_source_rgb(cr, 1.0, 0.78, 0.22);
        } else {
            cairo_set_source_rgb(cr, 0.34, 0.91, 0.57);
        }
        cairo_rectangle(cr, x - box_width / 2.0 + 2.0,
                        y + box_height / 2.0 - 4.0,
                        battery_width, 2.5);
        cairo_fill(cr);
    }
}

/* 返回不同等级道路在导航地图中的线宽。 */
static double road_line_width(RoadLevel level)
{
    switch (level) {
        case ROAD_EXPRESSWAY: return 5.2;
        case ROAD_ARTERIAL: return 3.5;
        case ROAD_SECONDARY: return 2.2;
        case ROAD_ACCESS: return 1.0;
        default: return 1.2;
    }
}

/* 判断一条接入边是否只是车辆起始位置到主路网的逻辑连接。 */
static int is_vehicle_access(int first_index, int second_index,
                             const Edge *edge)
{
    return edge->level == ROAD_ACCESS &&
        (app.graph.nodes[first_index].type == GRAPH_NODE_VEHICLE_POINT ||
         app.graph.nodes[second_index].type == GRAPH_NODE_VEHICLE_POINT);
}

/* 先绘制白色道路外框，使道路看起来更接近导航地图。 */
static void draw_road_casings(cairo_t *cr, int width, int height)
{
    int i;

    for (i = 0; i < app.graph.node_count; ++i) {
        const Edge *edge = app.graph.nodes[i].edges;
        double x1, y1;
        node_point(&app.graph.nodes[i], width, height, &x1, &y1);
        while (edge != NULL) {
            if ((i < edge->to_index ||
                 !reverse_edge_exists(i, edge->to_index)) &&
                !is_vehicle_access(i, edge->to_index, edge)) {
                double x2, y2;
                double line_width = road_line_width(edge->level) + 3.0;
                node_point(&app.graph.nodes[edge->to_index], width, height,
                           &x2, &y2);
                if (selected_edge(app.graph.nodes[i].id,
                                  app.graph.nodes[edge->to_index].id)) {
                    line_width = 7.0;
                }
                cairo_set_dash(cr, NULL, 0, 0.0);
                cairo_set_source_rgb(cr, 0.98, 0.99, 1.0);
                cairo_set_line_width(cr, line_width);
                cairo_move_to(cr, x1, y1);
                cairo_line_to(cr, x2, y2);
                cairo_stroke(cr);
            }
            edge = edge->next;
        }
    }
}

/* 绘制道路，并用橙色突出当前推荐路径。 */
static void draw_roads(cairo_t *cr, int width, int height)
{
    int i;

    for (i = 0; i < app.graph.node_count; ++i) {
        const Edge *edge = app.graph.nodes[i].edges;
        double x1, y1;
        node_point(&app.graph.nodes[i], width, height, &x1, &y1);
        while (edge != NULL) {
            if (i < edge->to_index || !reverse_edge_exists(i, edge->to_index)) {
                double x2, y2;
                node_point(&app.graph.nodes[edge->to_index], width, height,
                           &x2, &y2);
                if (selected_edge(app.graph.nodes[i].id,
                                  app.graph.nodes[edge->to_index].id)) {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgb(cr, 0.93, 0.45, 0.08);
                    cairo_set_line_width(cr, 4.0);
                } else if (is_vehicle_access(i, edge->to_index, edge)) {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
                    cairo_set_line_width(cr, 0.1);
                } else if (edge->level == ROAD_ACCESS) {
                    const double dashes[] = {4.0, 3.0};
                    cairo_set_dash(cr, dashes, 2, 0.0);
                    cairo_set_source_rgb(cr, 0.45, 0.55, 0.64);
                    cairo_set_line_width(cr, 1.0);
                } else if (edge->level == ROAD_EXPRESSWAY) {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgb(cr, 0.43, 0.53, 0.63);
                    cairo_set_line_width(cr, 5.2);
                } else if (edge->level == ROAD_ARTERIAL) {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgb(cr, 0.54, 0.62, 0.70);
                    cairo_set_line_width(cr, 3.5);
                } else if (edge->level == ROAD_SECONDARY) {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgb(cr, 0.62, 0.68, 0.74);
                    cairo_set_line_width(cr, 3.0);
                } else {
                    cairo_set_dash(cr, NULL, 0, 0.0);
                    cairo_set_source_rgb(cr, 0.64, 0.70, 0.76);
                    cairo_set_line_width(cr, 2.2);
                }
                cairo_move_to(cr, x1, y1);
                cairo_line_to(cr, x2, y2);
                cairo_stroke(cr);
                cairo_set_dash(cr, NULL, 0, 0.0);
            }
            edge = edge->next;
        }
    }
}

/* 只绘制真正的道路路口，车辆点和站点由各自图标绘制。 */
static void draw_nodes(cairo_t *cr, int width, int height)
{
    int i;

    if (app.map_zoom < 2.5) return;
    cairo_set_source_rgb(cr, 0.25, 0.35, 0.47);
    for (i = 0; i < app.graph.node_count; ++i) {
        double x, y;
        if (app.graph.nodes[i].type != GRAPH_NODE_JUNCTION) {
            continue;
        }
        node_point(&app.graph.nodes[i], width, height, &x, &y);
        cairo_arc(cr, x, y, 2.0, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
    }
}

/* 使用Pango进行中文字体回退，避免Cairo简易文字接口显示缺字方框。 */
static PangoLayout *create_map_text_layout(cairo_t *cr, const char *text,
                                           double size, int bold)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();

    pango_font_description_set_family(font, "Microsoft YaHei UI");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(
        font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_font_description_free(font);
    return layout;
}

/* 绘制导航地图式充电站标记。 */
static void draw_stations(cairo_t *cr, int width, int height)
{
    int i;

    for (i = 0; i < app.store.station_count; ++i) {
        Station *station = entity_find_station(&app.store,
                                               app.store.station_ids[i]);
        int node_index;
        double x, y;
        PangoLayout *layout;
        int text_width;
        int text_height;
        if (station == NULL) continue;
        node_index = graph_find_node(&app.graph, station->node_id);
        if (node_index < 0) continue;
        node_point(&app.graph.nodes[node_index], width, height, &x, &y);
        cairo_set_source_rgb(cr, 0.05, 0.58, 0.31);
        cairo_arc(cr, x, y, 22.0, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
        if (station->id == app.selected_station_id) {
            cairo_set_source_rgb(cr, 1.0, 0.78, 0.12);
            cairo_set_line_width(cr, 4.0);
            cairo_arc(cr, x, y, 27.0, 0.0, 2.0 * G_PI);
            cairo_stroke(cr);
        }
        if (app.station_icon != NULL) {
            draw_map_icon(cr, app.station_icon, x, y, 31.0);
        } else {
            layout = create_map_text_layout(cr, "充", 17.0, 1);
            pango_layout_get_pixel_size(layout, &text_width, &text_height);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_move_to(cr, x - text_width / 2.0, y - text_height / 2.0);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }

        layout = create_map_text_layout(cr, station->name, 13.0, 1);
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.86);
        cairo_rectangle(cr, x + 26.0, y - 13.0,
                        text_width + 10.0, text_height + 5.0);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.05, 0.35, 0.17);
        cairo_move_to(cr, x + 31.0, y - 11.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
}

/* 使用纯色地图底层，避免背景图案干扰道路和车辆。 */
static void draw_map_background(cairo_t *cr)
{
    cairo_set_source_rgb(cr, 0.94, 0.96, 0.97);
    cairo_paint(cr);
}

/* 按道路、节点、站点和车辆的顺序绘制完整地图。 */
static void draw_map(GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, gpointer data)
{
    (void)area;
    (void)data;

    clamp_map_offset();
    draw_map_background(cr);
    if (!app.loaded) return;

    draw_road_casings(cr, width, height);
    draw_roads(cr, width, height);
    draw_nodes(cr, width, height);
    draw_stations(cr, width, height);
    draw_vehicles(cr, width, height);
}

static GtkWidget *make_scrolled_list(int minimum_height)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, minimum_height);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    g_object_set_data(G_OBJECT(scroll), "content-list", list);
    return scroll;
}

static GtkWidget *make_button(const char *text, GCallback callback,
                              gpointer data)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    const char *action = data;

    if (action != NULL) {
        if (strcmp(action, "start") == 0) {
            gtk_widget_add_css_class(button, "action-start");
        } else if (strcmp(action, "pause") == 0) {
            gtk_widget_add_css_class(button, "action-warning");
        } else if (strcmp(action, "reset") == 0 ||
                   strcmp(action, "remove") == 0 ||
                   strcmp(action, "break") == 0) {
            gtk_widget_add_css_class(button, "action-danger");
        } else if (strcmp(action, "repair") == 0 ||
                   strcmp(action, "add") == 0) {
            gtk_widget_add_css_class(button, "action-success");
        } else if (strcmp(action, "submit") == 0 ||
                   strcmp(action, "submit-next-batch") == 0 ||
                   strcmp(action, "recommend") == 0) {
            gtk_widget_add_css_class(button, "action-primary");
        }
    }
    g_signal_connect(button, "clicked", callback, data);
    return button;
}

static GtkWidget *make_primary_button(const char *text, GCallback callback,
                                      gpointer data)
{
    GtkWidget *button = make_button(text, callback, data);
    gtk_widget_add_css_class(button, "suggested-action");
    return button;
}

static GtkWidget *make_section_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "section-title");
    return label;
}

static GtkWidget *make_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    set_widget_margins(page, 14, 14);
    return page;
}

static GtkWidget *make_card(GtkWidget *child)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "card");
    gtk_frame_set_child(GTK_FRAME(frame), child);
    return frame;
}

static GtkWidget *build_station_vehicle_column(const char *title,
                                                GtkWidget **list)
{
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *scroll = make_scrolled_list(230);

    gtk_box_append(GTK_BOX(column), make_section_label(title));
    gtk_widget_set_vexpand(scroll, TRUE);
    *list = g_object_get_data(G_OBJECT(scroll), "content-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(*list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(*list, "row-selected",
                     G_CALLBACK(on_vehicle_row_selected), NULL);
    gtk_box_append(GTK_BOX(column), scroll);
    return make_card(column);
}

static void install_css(void)
{
    static const char css[] =
        "window { background: #f3f6fa; color: #172033; font-size: 18px; }"
        ".app-title { font-size: 32px; font-weight: 800; color: #102a4c; }"
        ".app-subtitle { color: #5d6b80; font-size: 17px; font-weight: 600; }"
        ".section-title { font-size: 20px; font-weight: 800; color: #17365f; }"
        ".hint { color: #667085; font-size: 17px; }"
        ".status-pill { background: #dcecff; color: #1358a0; border: 1px solid #b8d6fa; border-radius: 16px; padding: 9px 15px; font-size: 17px; font-weight: 700; }"
        ".card { background: #ffffff; border: 1px solid #cfdae8; border-radius: 12px; box-shadow: 0 3px 12px rgba(31,65,114,0.10); }"
        ".record-title { font-size: 18px; font-weight: 800; color: #203754; }"
        ".record-subtitle { color: #5c6b80; font-size: 17px; }"
        ".state-badge { background: #edf2f8; color: #344a67; border: 1px solid #d4deea; border-radius: 11px; padding: 5px 11px; font-size: 16px; font-weight: 700; }"
        ".station-summary { background: #e5f0ff; color: #15579f; border: 1px solid #c5ddfb; border-radius: 9px; padding: 10px 12px; font-size: 18px; font-weight: 800; }"
        ".map-controls { background: rgba(255,255,255,0.94); color: #344054; border: 1px solid #dbe2ea; border-radius: 9px; padding: 6px 8px; }"
        ".side-resize-handle { background: rgba(255,255,255,0.72); border-left: 1px solid #c5d0dd; border-right: 1px solid #c5d0dd; min-width: 8px; }"
        ".side-resize-handle:hover { background: #b9d5fb; }"
        ".map-legend { background: rgba(255,255,255,0.92); color: #475467; border-radius: 8px; padding: 7px 10px; font-size: 14px; }"
        ".map-topbar { background: rgba(255,255,255,0.97); border: 1px solid #cfdae8; border-radius: 12px; padding: 10px 13px; box-shadow: 0 3px 12px rgba(31,65,114,0.10); }"
        ".side-panel-header { background: #edf4fd; border-bottom: 1px solid #ccdaea; padding: 10px 12px; }"
        ".side-expand { background: rgba(255,255,255,0.96); color: #1959a6; border: 1px solid #dbe2ea; border-radius: 8px; padding: 10px 8px; font-weight: 700; }"
        ".statistics-panel { background: rgba(255,255,255,0.97); padding: 16px 18px; }"
        ".statistics-compare { color: #475467; font-size: 16px; font-weight: 600; }"
        ".statistics-table-header { background: #e8f1ff; color: #1959a6; padding: 8px 10px; font-size: 16px; font-weight: 700; }"
        ".statistics-table-row-header { background: #f1f5f9; color: #24344d; padding: 8px 10px; font-size: 16px; font-weight: 700; }"
        ".statistics-table-cell { background: #ffffff; color: #172033; padding: 8px 10px; font-size: 17px; font-weight: 600; }"
        ".vehicle-map-icon { min-width: 28px; min-height: 28px; border-radius: 7px; color: white; font-weight: 700; }"
        ".vehicle-charging { background: #7a4fc4; }"
        ".vehicle-waiting { background: #d64545; }"
        ".charger-card { border: 2px solid transparent; border-radius: 9px; padding: 9px 11px; }"
        ".charger-free { background: #dff6e9; color: #146c43; }"
        ".charger-busy { background: #e8e3fa; color: #5b3ba4; }"
        ".charger-fault { background: #fde5e2; color: #b42318; }"
        "flowboxchild:selected .charger-card { border-color: #1959a6; box-shadow: 0 0 0 2px rgba(25,89,166,0.20); }"
        "list { background: #ffffff; }"
        "list row { border-bottom: 1px solid #e7edf4; }"
        "list row:hover { background: #f2f7fd; }"
        "list row:selected { background: #eaf2ff; color: #172033; }";
    static const char control_css[] =
        "button { min-height: 42px; padding: 8px 13px; font-size: 17px; font-weight: 700; border-radius: 9px; border: 1px solid #cbd7e5; background: #ffffff; color: #29415f; }"
        "button:hover { background: #edf5ff; border-color: #8db9ee; color: #15579f; }"
        "button:active { background: #dcecff; }"
        "button.suggested-action, button.action-primary { background: #1976d2; border-color: #1976d2; color: #ffffff; }"
        "button.suggested-action:hover, button.action-primary:hover { background: #125fac; border-color: #125fac; color: #ffffff; }"
        "button.action-start { background: #16845b; border-color: #16845b; color: #ffffff; }"
        "button.action-start:hover { background: #106b49; border-color: #106b49; color: #ffffff; }"
        "button.action-warning { background: #fff2cf; border-color: #efc768; color: #7a4b00; }"
        "button.action-danger { background: #fde9e7; border-color: #efb5ae; color: #a92d24; }"
        "button.action-success { background: #e0f5e9; border-color: #9bd5b3; color: #12633f; }"
        "button.mode-switch { background: #e4efff; border-color: #a9c9f2; color: #15579f; }"
        "button.mode-switch:checked { background: #fff0d6; border-color: #e9bd65; color: #875400; }"
        "button.toggle-control:checked { background: #1976d2; border-color: #1976d2; color: #ffffff; }"
        "entry, spinbutton { min-height: 42px; font-size: 18px; font-weight: 600; border-radius: 8px; border-color: #bdcada; }"
        "entry:focus, spinbutton:focus-within { border-color: #1976d2; box-shadow: 0 0 0 2px rgba(25,118,210,0.18); }";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, control_css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* 统一选择车辆，供列表点击和地图点击共同调用。 */
static void select_vehicle(int vehicle_id)
{
    char text[32];

    if (entity_find_vehicle(&app.store, vehicle_id) == NULL) return;
    app.selected_vehicle_id = vehicle_id;
    snprintf(text, sizeof(text), "%d", vehicle_id);
    gtk_editable_set_text(GTK_EDITABLE(app.vehicle_entry), text);
    gtk_widget_queue_draw(app.drawing_area);
}

/* 统一选择充电站，并刷新该站的充电桩和等待队列。 */
static void select_station(int station_id)
{
    char text[32];
    const char *entry_text;

    if (entity_find_station(&app.store, station_id) == NULL) return;
    snprintf(text, sizeof(text), "%d", station_id);
    entry_text = gtk_editable_get_text(GTK_EDITABLE(app.station_entry));
    if (strcmp(entry_text, text) != 0) {
        gtk_editable_set_text(GTK_EDITABLE(app.station_entry), text);
        return;
    }
    if (app.selected_station_id != station_id) {
        app.selected_charger_id = -1;
    }
    app.selected_station_id = station_id;
    refresh_chargers();
    refresh_queue();
    gtk_widget_queue_draw(app.drawing_area);
}

static void on_station_entry_changed(GtkEditable *editable, gpointer data)
{
    int station_id;
    (void)editable;
    (void)data;

    if (app.loaded && entry_int(app.station_entry, &station_id) &&
        entity_find_station(&app.store, station_id) != NULL) {
        select_station(station_id);
        return;
    }
    app.selected_station_id = -1;
    app.selected_charger_id = -1;
    if (app.loaded) {
        refresh_chargers();
        refresh_queue();
        gtk_widget_queue_draw(app.drawing_area);
    }
}

static void on_vehicle_row_selected(GtkListBox *box, GtkListBoxRow *row,
                                    gpointer data)
{
    int id;
    (void)box;
    (void)data;
    if (row == NULL) return;
    id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "entity-id"));
    if (id <= 0) return;
    select_vehicle(id);
}

/* 点击地图时优先选择车辆，没有点中车辆时再选择充电站。 */
static void on_map_pressed(GtkGestureClick *gesture, int press_count,
                           double mouse_x, double mouse_y, gpointer data)
{
    int width = gtk_widget_get_width(app.drawing_area);
    int height = gtk_widget_get_height(app.drawing_area);
    int best_id = -1;
    double best_distance = 1e30;
    int i;
    (void)gesture;
    (void)press_count;
    (void)data;

    if (!app.loaded) return;

    for (i = 0; i < simulation_vehicle_count(&app.simulation); ++i) {
        const Vehicle *vehicle = simulation_vehicle_at(&app.simulation, i);
        double x, y;
        double dx, dy;
        double distance;

        if (!vehicle_is_on_map(vehicle) ||
            !vehicle_point(vehicle, width, height, &x, &y)) continue;
        dx = mouse_x - x;
        dy = mouse_y - y;
        if (dx < -29.0 || dx > 29.0 || dy < -20.0 || dy > 20.0) {
            continue;
        }
        distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best_id = vehicle->id;
        }
    }
    if (best_id >= 0) {
        select_vehicle(best_id);
        return;
    }

    best_distance = 1e30;
    for (i = 0; i < simulation_station_count(&app.simulation); ++i) {
        const Station *station = simulation_station_at(&app.simulation, i);
        int node_index;
        double x, y;
        double dx, dy;
        double distance;

        if (station == NULL) continue;
        node_index = graph_find_node(&app.graph, station->node_id);
        if (node_index < 0) continue;
        node_point(&app.graph.nodes[node_index], width, height, &x, &y);
        dx = mouse_x - x;
        dy = mouse_y - y;
        if (dx < -26.0 || dx > 26.0 || dy < -26.0 || dy > 26.0) {
            continue;
        }
        distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best_id = station->id;
        }
    }
    if (best_id >= 0) {
        select_station(best_id);
    }
}

/* 更新地图缩放比例提示。 */
static void update_zoom_label(void)
{
    char text[64];

    if (app.zoom_label == NULL) return;
    snprintf(text, sizeof(text), "%.0f%%", app.map_zoom * 100.0);
    gtk_label_set_text(GTK_LABEL(app.zoom_label), text);
}

/* 保留宽松边界，允许在各缩放级别大范围查看路网边缘。 */
static void clamp_map_offset(void)
{
    int width;
    int height;
    double half_map_width;
    double half_map_height;
    double max_x;
    double max_y;

    if (app.drawing_area == NULL) return;
    width = gtk_widget_get_width(app.drawing_area);
    height = gtk_widget_get_height(app.drawing_area);
    half_map_width = (width - 56.0) * app.map_zoom / 2.0;
    half_map_height = (height - 56.0) * app.map_zoom / 2.0;
    max_x = width * 0.75 + half_map_width;
    max_y = height * 0.75 + half_map_height;
    if (app.map_offset_x > max_x) app.map_offset_x = max_x;
    if (app.map_offset_x < -max_x) app.map_offset_x = -max_x;
    if (app.map_offset_y > max_y) app.map_offset_y = max_y;
    if (app.map_offset_y < -max_y) app.map_offset_y = -max_y;
}

/* 鼠标滚轮以当前视图中心缩放地图，避免坐标累计漂移。 */
static gboolean on_map_scroll(GtkEventControllerScroll *controller,
                              double dx, double dy, gpointer data)
{
    double old_zoom = app.map_zoom;
    double new_zoom;

    (void)controller;
    (void)dx;
    (void)data;
    if (dy == 0.0) return FALSE;

    new_zoom = dy < 0.0 ? old_zoom * 1.18 : old_zoom / 1.18;
    if (new_zoom < 0.35) new_zoom = 0.35;
    if (new_zoom > 12.00) new_zoom = 12.00;
    if (new_zoom == old_zoom) return TRUE;

    app.map_offset_x *= new_zoom / old_zoom;
    app.map_offset_y *= new_zoom / old_zoom;
    app.map_zoom = new_zoom;
    clamp_map_offset();
    update_zoom_label();
    gtk_widget_queue_draw(app.drawing_area);
    return TRUE;
}

/* 记录开始拖拽时的地图偏移量。 */
static void on_map_drag_begin(GtkGestureDrag *gesture, double x, double y,
                              gpointer data)
{
    (void)gesture;
    (void)x;
    (void)y;
    (void)data;
    app.drag_start_offset_x = app.map_offset_x;
    app.drag_start_offset_y = app.map_offset_y;
}

/* 根据鼠标拖拽距离平移地图。 */
static void on_map_drag_update(GtkGestureDrag *gesture,
                               double offset_x, double offset_y,
                               gpointer data)
{
    (void)gesture;
    (void)data;
    app.map_offset_x = app.drag_start_offset_x + offset_x;
    app.map_offset_y = app.drag_start_offset_y + offset_y;
    clamp_map_offset();
    gtk_widget_queue_draw(app.drawing_area);
}

/* 恢复完整路网的默认显示范围。 */
static void reset_map_view(void)
{
    app.map_zoom = 1.0;
    app.map_offset_x = 0.0;
    app.map_offset_y = 0.0;
    update_zoom_label();
    if (app.drawing_area != NULL) gtk_widget_queue_draw(app.drawing_area);
}

/* 更新顶部的仿真倍速提示。 */
static void update_speed_label(void)
{
    char text[32];

    if (app.speed_label == NULL) return;
    snprintf(text, sizeof(text), "%.2gx", app.simulation_speed);
    gtk_label_set_text(GTK_LABEL(app.speed_label), text);
}

/* 在固定倍速档位之间切换，并保持当前动画进度比例。 */
static void change_simulation_speed(int direction)
{
    static const double speeds[] = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    double progress = timer_progress(&app.timer);
    int count = (int)(sizeof(speeds) / sizeof(speeds[0]));
    int current = 0;
    int i;

    for (i = 1; i < count; ++i) {
        if (speeds[i] <= app.simulation_speed + 0.000001) current = i;
    }
    current += direction < 0 ? -1 : 1;
    if (current < 0) current = 0;
    if (current >= count) current = count - 1;
    app.simulation_speed = speeds[current];
    app.timer.step_interval_ms = UI_STEP_INTERVAL_MS / app.simulation_speed;
    app.timer.accumulator_ms = progress * app.timer.step_interval_ms;
    update_speed_label();
    output_line("仿真速度已调整为 %.2g 倍。", app.simulation_speed);
}

static void on_charger_selection_changed(GtkFlowBox *box, gpointer data)
{
    GList *selected = gtk_flow_box_get_selected_children(box);
    int id;
    (void)data;

    if (selected == NULL) return;
    id = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(selected->data), "entity-id"));
    g_list_free(selected);
    if (id <= 0) return;
    app.selected_charger_id = id;
}

static void on_continuous_traffic_toggled(GtkToggleButton *button,
                                          gpointer data)
{
    int i;
    (void)data;

    app.continuous_traffic_enabled =
        gtk_toggle_button_get_active(button);
    gtk_button_set_label(GTK_BUTTON(button),
        app.continuous_traffic_enabled ? "持续车流：开" : "持续车流：关");
    if (app.continuous_traffic_enabled && app.loaded) {
        track_vehicle_completions();
        for (i = 0; i < app.store.vehicle_count; ++i) {
            if (app.completion_counted[i] &&
                app.vehicle_respawn_time[i] < 0.0) {
                app.vehicle_respawn_time[i] = app.simulation.current_time
                    + scenario_random_double(5.0, 10.0);
            }
        }
    }
    output_line(app.continuous_traffic_enabled
                ? "持续车流已开启；刷新车辆需点击提交下一批进行调度。"
                : "持续车流已关闭。完成当前任务后仿真将正常停止。");
}

static void on_auto_submit_toggled(GtkToggleButton *button, gpointer data)
{
    (void)data;
    app.auto_submit_enabled = gtk_toggle_button_get_active(button);
    gtk_button_set_label(GTK_BUTTON(button),
        app.auto_submit_enabled ? "自动提交：开" : "自动提交：关");
    app.next_auto_submit_time = app.auto_submit_enabled && app.loaded
        ? app.simulation.current_time + 10.0 : 0.0;
    output_line(app.auto_submit_enabled
                ? "自动提交已开启；每隔 10 个仿真分钟提交下一批。"
                : "自动提交已关闭。后续批次改为手动提交。");
}

static void on_mode_toggled(GtkToggleButton *button, gpointer data)
{
    SimulationMode mode = gtk_toggle_button_get_active(button)
                        ? SIMULATION_MODE_CONTROL
                        : SIMULATION_MODE_PROJECT;
    (void)data;

    if (mode == app.simulation_mode) return;
    app.simulation_mode = mode;
    gtk_button_set_label(GTK_BUTTON(button),
        mode == SIMULATION_MODE_CONTROL ? "对照实验模式" : "本项目模式");
    if (app.loaded) reset_simulation();
    output_line("已切换到%s；仿真已从相同初始场景重新开始。",
        mode == SIMULATION_MODE_CONTROL
            ? "对照实验（最近站点 + 先来先服务）"
            : "本项目（综合推荐 + 动态优先级）");
}

static void on_time_limit_changed(GtkSpinButton *spin, gpointer data)
{
    (void)data;
    app.simulation_time_limit = gtk_spin_button_get_value(spin);
    if (app.loaded) {
        update_status();
        output_line("仿真时长已设为 %.0f 分钟。",
                    app.simulation_time_limit);
    }
}

static void on_statistics_toggle(GtkButton *button, gpointer data)
{
    gboolean expanded;
    (void)data;

    expanded = gtk_revealer_get_reveal_child(
        GTK_REVEALER(app.statistics_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(app.statistics_revealer),
                                  !expanded);
    gtk_button_set_label(button, expanded ? "展开统计" : "收起统计");
}

static void on_simple_button(GtkButton *button, gpointer data)
{
    const char *action = data;
    (void)button;
    if (strcmp(action, "load") == 0) load_data();
    else if (strcmp(action, "recommend") == 0) recommend_vehicle();
    else if (strcmp(action, "submit") == 0) submit_selected();
    else if (strcmp(action, "submit-next-batch") == 0)
        submit_next_batch();
    else if (strcmp(action, "start") == 0) start_simulation();
    else if (strcmp(action, "pause") == 0) {
        stop_timer();
        output_line("仿真已暂停。");
    }
    else if (strcmp(action, "reset") == 0) reset_simulation();
    else if (strcmp(action, "map-reset") == 0) reset_map_view();
    else if (strcmp(action, "speed-down") == 0)
        change_simulation_speed(-1);
    else if (strcmp(action, "speed-up") == 0)
        change_simulation_speed(1);
    else if (strcmp(action, "add") == 0) add_charger();
    else if (strcmp(action, "remove") == 0) remove_charger();
    else if (strcmp(action, "break") == 0) set_charger_state(0);
    else if (strcmp(action, "repair") == 0) set_charger_state(1);
}

static GtkWidget *labeled_entry(GtkWidget *grid, const char *label_text,
                                const char *initial, int row, int column)
{
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_editable_set_text(GTK_EDITABLE(entry), initial);
    gtk_widget_set_size_request(entry, 110, -1);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), label_text);
    gtk_grid_attach(GTK_GRID(grid), label, column, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, column + 1, row, 1, 1);
    return entry;
}

static void on_shutdown(GApplication *application, gpointer data)
{
    (void)application;
    (void)data;
    stop_timer();
    if (app.loaded) {
        simulation_reset(&app.simulation);
        entity_free(&app.store);
        graph_free(&app.graph);
        app.loaded = 0;
    }
    if (app.vehicle_icon != NULL) cairo_surface_destroy(app.vehicle_icon);
    if (app.station_icon != NULL) cairo_surface_destroy(app.station_icon);
}

/* 创建窗口顶部的标题和状态区域。 */
static GtkWidget *build_header(void)
{
    GtkWidget *header_text;
    GtkWidget *title;
    GtkWidget *subtitle;

    header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    title = gtk_label_new("充电地图");
    subtitle = gtk_label_new("查找合适的充电站并规划充电行程");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_widget_add_css_class(title, "app-title");
    gtk_widget_add_css_class(subtitle, "app-subtitle");
    gtk_box_append(GTK_BOX(header_text), title);
    gtk_box_append(GTK_BOX(header_text), subtitle);
    return header_text;
}

/* 创建批次、模式和仿真控制栏。 */
static GtkWidget *build_toolbar(void)
{
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget *control;
    GtkAdjustment *time_adjustment;

    gtk_box_append(GTK_BOX(toolbar),
                   make_button("刷新数据", G_CALLBACK(on_simple_button), "load"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_button("提交下一批", G_CALLBACK(on_simple_button),
                               "submit-next-batch"));
    control = gtk_toggle_button_new_with_label("自动提交：关");
    gtk_widget_add_css_class(control, "toggle-control");
    g_signal_connect(control, "toggled",
                     G_CALLBACK(on_auto_submit_toggled), NULL);
    gtk_box_append(GTK_BOX(toolbar), control);
    control = gtk_toggle_button_new_with_label("持续车流：关");
    gtk_widget_add_css_class(control, "toggle-control");
    g_signal_connect(control, "toggled",
                     G_CALLBACK(on_continuous_traffic_toggled), NULL);
    gtk_box_append(GTK_BOX(toolbar), control);
    control = gtk_toggle_button_new_with_label("本项目模式");
    gtk_widget_add_css_class(control, "mode-switch");
    g_signal_connect(control, "toggled",
                     G_CALLBACK(on_mode_toggled), NULL);
    gtk_box_append(GTK_BOX(toolbar), control);
    gtk_box_append(GTK_BOX(toolbar), gtk_label_new("时长(min)"));
    time_adjustment = gtk_adjustment_new(app.simulation_time_limit,
                                         10.0, 10000.0,
                                         10.0, 60.0, 0.0);
    control = gtk_spin_button_new(time_adjustment, 10.0, 0);
    gtk_widget_set_size_request(control, 82, -1);
    gtk_widget_set_tooltip_text(control,
                                "达到该仿真分钟数后自动停止");
    g_signal_connect(control, "value-changed",
                     G_CALLBACK(on_time_limit_changed), NULL);
    gtk_box_append(GTK_BOX(toolbar), control);
    gtk_box_append(GTK_BOX(toolbar),
                   make_primary_button("开始", G_CALLBACK(on_simple_button), "start"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_button("暂停", G_CALLBACK(on_simple_button), "pause"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_button("减速", G_CALLBACK(on_simple_button),
                               "speed-down"));
    app.speed_label = gtk_label_new("1x");
    gtk_widget_add_css_class(app.speed_label, "state-badge");
    gtk_box_append(GTK_BOX(toolbar), app.speed_label);
    gtk_box_append(GTK_BOX(toolbar),
                   make_button("加速", G_CALLBACK(on_simple_button),
                               "speed-up"));
    gtk_box_append(GTK_BOX(toolbar),
                   make_button("重置", G_CALLBACK(on_simple_button), "reset"));
    return toolbar;
}

/* 创建车辆推荐页面。 */
static GtkWidget *build_recommend_page(void)
{
    GtkWidget *page = gtk_scrolled_window_new();
    GtkWidget *content = make_page();
    GtkWidget *entry_grid;
    GtkWidget *button_row;
    GtkWidget *scroll;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), content);
    gtk_box_append(GTK_BOX(content), make_section_label("路线规划"));
    app.batch_status_label = gtk_label_new(
        "当前批次 0/0 · 待提交 0 辆 · 已提交 0 辆");
    gtk_label_set_xalign(GTK_LABEL(app.batch_status_label), 0.0f);
    gtk_widget_add_css_class(app.batch_status_label, "station-summary");
    gtk_box_append(GTK_BOX(content), app.batch_status_label);
    entry_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(entry_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(entry_grid), 8);
    gtk_widget_set_hexpand(entry_grid, TRUE);
    app.vehicle_entry = labeled_entry(entry_grid, "车辆编号", "201", 0, 0);
    g_signal_connect(app.vehicle_entry, "changed",
                     G_CALLBACK(on_vehicle_entry_changed), NULL);
    gtk_box_append(GTK_BOX(content), entry_grid);
    button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_box_append(GTK_BOX(button_row),
        make_primary_button("计算推荐", G_CALLBACK(on_simple_button), "recommend"));
    gtk_box_append(GTK_BOX(button_row),
        make_button("提交当前选择的充电站",
                    G_CALLBACK(on_simple_button), "submit"));
    gtk_box_append(GTK_BOX(content), button_row);
    app.selected_vehicle_info_label = gtk_label_new(
        "当前车辆 201 · 当前电量 -- · 目标电量 --");
    gtk_label_set_xalign(GTK_LABEL(app.selected_vehicle_info_label), 0.0f);
    gtk_widget_add_css_class(app.selected_vehicle_info_label,
                             "station-summary");
    gtk_box_append(GTK_BOX(content), app.selected_vehicle_info_label);
    gtk_box_append(GTK_BOX(content), make_section_label("推荐结果"));
    scroll = make_scrolled_list(260);
    gtk_widget_set_vexpand(scroll, TRUE);
    app.recommendation_list = g_object_get_data(G_OBJECT(scroll), "content-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.recommendation_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(app.recommendation_list, "row-selected",
                     G_CALLBACK(on_recommendation_selected), NULL);
    gtk_box_append(GTK_BOX(content), scroll);
    return page;
}

/* 创建充电站、充电桩和等待队列操作页面。 */
static GtkWidget *build_charger_page(void)
{
    GtkWidget *page = gtk_scrolled_window_new();
    GtkWidget *content = make_page();
    GtkWidget *operation_box;
    GtkWidget *entry_grid;
    GtkWidget *button_grid;
    GtkWidget *vehicle_columns;
    GtkWidget *scroll;
    GtkWidget *maintenance;
    GtkWidget *maintenance_box;
    GtkWidget *add_box;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), content);
    gtk_box_append(GTK_BOX(content), make_section_label("站点服务"));
    entry_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(entry_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(entry_grid), 8);
    gtk_widget_set_hexpand(entry_grid, TRUE);
    app.station_entry = labeled_entry(entry_grid, "充电站编号", "101", 0, 0);
    g_signal_connect(app.station_entry, "changed",
                     G_CALLBACK(on_station_entry_changed), NULL);
    gtk_box_append(GTK_BOX(content), entry_grid);

    operation_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    set_widget_margins(operation_box, 10, 10);
    app.station_operation_title = make_section_label("请选择一个充电站");
    gtk_box_append(GTK_BOX(operation_box), app.station_operation_title);
    app.station_status_label = gtk_label_new("尚未选择站点");
    gtk_label_set_xalign(GTK_LABEL(app.station_status_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(app.station_status_label), TRUE);
    gtk_widget_add_css_class(app.station_status_label, "hint");
    gtk_widget_add_css_class(app.station_status_label, "station-summary");
    gtk_box_append(GTK_BOX(operation_box), app.station_status_label);
    gtk_box_append(GTK_BOX(operation_box),
                   make_section_label("站内车辆状态"));

    vehicle_columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(vehicle_columns), TRUE);
    gtk_widget_set_hexpand(vehicle_columns, TRUE);
    gtk_widget_set_vexpand(vehicle_columns, TRUE);

    gtk_box_append(GTK_BOX(vehicle_columns),
                   build_station_vehicle_column(
                       "正在充电", &app.charging_vehicle_list));
    gtk_box_append(GTK_BOX(vehicle_columns),
                   build_station_vehicle_column(
                       "排队顺序", &app.waiting_vehicle_list));
    gtk_box_append(GTK_BOX(operation_box), vehicle_columns);
    maintenance = gtk_expander_new("设备维护");
    maintenance_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(maintenance_box, 8);
    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 125);
    gtk_widget_set_hexpand(scroll, TRUE);
    app.charger_list = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app.charger_list),
                                    GTK_SELECTION_SINGLE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(app.charger_list), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(app.charger_list), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app.charger_list), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(app.charger_list), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(app.charger_list), 8);
    g_signal_connect(app.charger_list, "selected-children-changed",
                     G_CALLBACK(on_charger_selection_changed), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                  app.charger_list);
    gtk_box_append(GTK_BOX(maintenance_box), scroll);

    button_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(button_grid), 7);
    gtk_grid_set_column_spacing(GTK_GRID(button_grid), 7);
    gtk_grid_set_column_homogeneous(GTK_GRID(button_grid), TRUE);
    gtk_widget_set_hexpand(button_grid, TRUE);
    gtk_grid_attach(GTK_GRID(button_grid),
                    make_button("删除充电桩", G_CALLBACK(on_simple_button),
                                "remove"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(button_grid),
                    make_button("设为故障", G_CALLBACK(on_simple_button),
                                "break"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(button_grid),
                    make_button("修复充电桩", G_CALLBACK(on_simple_button),
                                "repair"), 2, 0, 1, 1);
    gtk_box_append(GTK_BOX(maintenance_box), button_grid);

    add_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    set_widget_margins(add_box, 10, 10);
    gtk_box_append(GTK_BOX(add_box), make_section_label("新增充电桩"));
    entry_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(entry_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(entry_grid), 8);
    gtk_widget_set_hexpand(entry_grid, TRUE);
    app.charger_entry = labeled_entry(entry_grid, "充电桩编号", "1001", 0, 0);
    app.power_entry = labeled_entry(entry_grid, "新增功率 kW", "60", 1, 0);
    gtk_box_append(GTK_BOX(add_box), entry_grid);
    gtk_box_append(GTK_BOX(add_box),
        make_primary_button("新增充电桩",
                            G_CALLBACK(on_simple_button), "add"));
    gtk_box_append(GTK_BOX(maintenance_box), make_card(add_box));
    gtk_expander_set_child(GTK_EXPANDER(maintenance), maintenance_box);
    gtk_box_append(GTK_BOX(operation_box), maintenance);
    gtk_box_append(GTK_BOX(content), make_card(operation_box));
    return page;
}

/* 创建窗口上方的主体地图区域。 */
static GtkWidget *build_map_panel(void)
{
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *map_controls;
    GtkWidget *legend;
    GtkGesture *map_click;
    GtkGesture *map_drag;
    GtkEventController *map_scroll;

    app.drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app.drawing_area), 1000);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app.drawing_area), 560);
    gtk_widget_set_hexpand(app.drawing_area, TRUE);
    gtk_widget_set_vexpand(app.drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.drawing_area),
                                   draw_map, NULL, NULL);
    map_click = gtk_gesture_click_new();
    g_signal_connect(map_click, "pressed", G_CALLBACK(on_map_pressed), NULL);
    gtk_widget_add_controller(app.drawing_area,
                              GTK_EVENT_CONTROLLER(map_click));
    map_drag = gtk_gesture_drag_new();
    g_signal_connect(map_drag, "drag-begin",
                     G_CALLBACK(on_map_drag_begin), NULL);
    g_signal_connect(map_drag, "drag-update",
                     G_CALLBACK(on_map_drag_update), NULL);
    gtk_widget_add_controller(app.drawing_area,
                              GTK_EVENT_CONTROLLER(map_drag));
    map_scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(map_scroll, "scroll", G_CALLBACK(on_map_scroll), NULL);
    gtk_widget_add_controller(app.drawing_area, map_scroll);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), app.drawing_area);

    map_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(map_controls, "map-controls");
    gtk_widget_set_halign(map_controls, GTK_ALIGN_END);
    gtk_widget_set_valign(map_controls, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(map_controls, 14);
    gtk_widget_set_margin_end(map_controls, 14);
    app.zoom_label = gtk_label_new("缩放 100%");
    gtk_box_append(GTK_BOX(map_controls), app.zoom_label);
    gtk_box_append(GTK_BOX(map_controls),
                   make_button("复位", G_CALLBACK(on_simple_button),
                               "map-reset"));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), map_controls);

    legend = gtk_label_new("蓝色为车辆　绿色为充电站");
    gtk_widget_add_css_class(legend, "map-legend");
    gtk_widget_set_halign(legend, GTK_ALIGN_START);
    gtk_widget_set_valign(legend, GTK_ALIGN_END);
    gtk_widget_set_margin_start(legend, 14);
    gtk_widget_set_margin_bottom(legend, 14);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), legend);
    return make_card(overlay);
}

/* 使用固定在界面侧边的同一个按钮展开或收起操作窗口。 */
static void on_panel_toggle(GtkButton *button, gpointer data)
{
    GtkRevealer *revealer = GTK_REVEALER(data);
    gboolean expanded = gtk_revealer_get_reveal_child(revealer);
    gboolean left_side = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "left-side"));

    gtk_revealer_set_reveal_child(revealer, !expanded);
    gtk_button_set_label(button,
        left_side ? (expanded ? ">" : "<")
                  : (expanded ? "<" : ">"));
}

static void on_panel_resize_begin(GtkGestureDrag *gesture,
                                  double start_x, double start_y,
                                  gpointer data)
{
    PanelResizeData *resize = data;
    (void)gesture;
    (void)start_x;
    (void)start_y;
    resize->start_width = gtk_widget_get_width(resize->panel_card);
}

static void on_panel_resize_update(GtkGestureDrag *gesture,
                                   double offset_x, double offset_y,
                                   gpointer data)
{
    PanelResizeData *resize = data;
    int window_width = gtk_widget_get_width(app.window);
    int maximum_width = window_width * 7 / 10;
    int width;
    (void)gesture;
    (void)offset_y;

    if (maximum_width < resize->minimum_width) {
        maximum_width = resize->minimum_width;
    }
    width = resize->start_width
          + (resize->left_side ? (int)offset_x : -(int)offset_x);
    width = CLAMP(width, resize->minimum_width, maximum_width);
    gtk_widget_set_size_request(resize->panel_card, width, -1);
}

/* 创建叠加在地图上的可折叠侧边窗口。 */
static GtkWidget *build_side_panel(const char *title, GtkWidget *content,
                                   GtkRevealerTransitionType transition,
                                   GtkAlign align, int width)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *resizable = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *window = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *side_button = gtk_button_new_with_label(
        align == GTK_ALIGN_START ? "<" : ">");
    GtkWidget *revealer = gtk_revealer_new();
    GtkWidget *resize_handle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *card;
    GtkGesture *resize_drag;
    PanelResizeData *resize_data = g_new0(PanelResizeData, 1);

    gtk_widget_add_css_class(header, "side-panel-header");
    gtk_widget_add_css_class(title_label, "section-title");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(header), title_label);

    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(window), header);
    gtk_box_append(GTK_BOX(window), content);
    card = make_card(window);
    gtk_widget_set_size_request(card, width, -1);
    gtk_widget_set_hexpand(card, TRUE);
    gtk_widget_set_vexpand(card, TRUE);

    resize_data->panel_card = card;
    resize_data->start_width = width;
    resize_data->minimum_width = align == GTK_ALIGN_START ? 300 : 360;
    resize_data->left_side = align == GTK_ALIGN_START;
    gtk_widget_add_css_class(resize_handle, "side-resize-handle");
    gtk_widget_set_cursor_from_name(resize_handle, "ew-resize");
    gtk_widget_set_vexpand(resize_handle, TRUE);
    resize_drag = gtk_gesture_drag_new();
    g_signal_connect(resize_drag, "drag-begin",
                     G_CALLBACK(on_panel_resize_begin), resize_data);
    g_signal_connect(resize_drag, "drag-update",
                     G_CALLBACK(on_panel_resize_update), resize_data);
    g_object_set_data_full(G_OBJECT(resize_drag), "panel-resize-data",
                           resize_data, g_free);
    gtk_widget_add_controller(resize_handle,
                              GTK_EVENT_CONTROLLER(resize_drag));

    if (align == GTK_ALIGN_START) {
        gtk_box_append(GTK_BOX(resizable), card);
        gtk_box_append(GTK_BOX(resizable), resize_handle);
    } else {
        gtk_box_append(GTK_BOX(resizable), resize_handle);
        gtk_box_append(GTK_BOX(resizable), card);
    }
    gtk_widget_set_vexpand(resizable, TRUE);

    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), transition);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 220);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
    gtk_revealer_set_child(GTK_REVEALER(revealer), resizable);
    gtk_widget_set_vexpand(revealer, TRUE);

    gtk_widget_add_css_class(side_button, "side-expand");
    gtk_widget_set_valign(side_button, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(side_button), "left-side",
                      GINT_TO_POINTER(align == GTK_ALIGN_START));
    g_signal_connect(side_button, "clicked",
                     G_CALLBACK(on_panel_toggle), revealer);

    if (align == GTK_ALIGN_START) {
        gtk_box_append(GTK_BOX(panel), revealer);
        gtk_box_append(GTK_BOX(panel), side_button);
    } else {
        gtk_box_append(GTK_BOX(panel), side_button);
        gtk_box_append(GTK_BOX(panel), revealer);
    }
    gtk_widget_set_halign(panel, align);
    gtk_widget_set_valign(panel, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(panel, 88);
    gtk_widget_set_margin_bottom(panel, 350);
    if (align == GTK_ALIGN_START) {
        gtk_widget_set_margin_start(panel, 16);
    } else {
        gtk_widget_set_margin_end(panel, 16);
    }
    return panel;
}

static GtkWidget *make_statistics_table_label(const char *text,
                                              const char *css_class)
{
    GtkWidget *label = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
    gtk_widget_set_size_request(label, 114, 58);
    gtk_widget_add_css_class(label, css_class);
    return label;
}

/* 创建地图底部中央的可折叠实时统计与对照表格。 */
static GtkWidget *build_completed_count_panel(void)
{
    static const char *headers[STAT_METRIC_COUNT] = {
        "平均总完成\n(min)", "最大总完成\n(min)",
        "平均排队\n(min)", "最大排队\n(min)",
        "平均行驶\n(min)", "累计完成\n(辆)",
        "单位时间完成\n(辆/h)"
    };
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *table = gtk_grid_new();
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *card;
    GtkWidget *toggle_button;
    int column;
    int row;

    gtk_box_append(GTK_BOX(content), make_section_label("仿真统计对比"));
    gtk_grid_set_row_spacing(GTK_GRID(table), 1);
    gtk_grid_set_column_spacing(GTK_GRID(table), 1);
    gtk_grid_attach(GTK_GRID(table),
                    make_statistics_table_label("实验项目",
                                                "statistics-table-header"),
                    0, 0, 1, 1);
    for (column = 0; column < STAT_METRIC_COUNT; ++column) {
        gtk_grid_attach(GTK_GRID(table),
                        make_statistics_table_label(
                            headers[column], "statistics-table-header"),
                        column + 1, 0, 1, 1);
    }
    for (row = 0; row < SIMULATION_MODE_COUNT; ++row) {
        SimulationMode mode = row == 0
                            ? SIMULATION_MODE_CONTROL
                            : SIMULATION_MODE_PROJECT;
        const char *row_name = mode == SIMULATION_MODE_CONTROL
                             ? "对照实验" : "本项目";
        gtk_grid_attach(GTK_GRID(table),
                        make_statistics_table_label(
                            row_name, "statistics-table-row-header"),
                        0, row + 1, 1, 1);
        for (column = 0; column < STAT_METRIC_COUNT; ++column) {
            GtkWidget *cell = make_statistics_table_label(
                column == STAT_COMPLETED_COUNT ? "0" : "--",
                "statistics-table-cell");
            app.statistics_cells[mode][column] = cell;
            gtk_grid_attach(GTK_GRID(table), cell,
                            column + 1, row + 1, 1, 1);
        }
    }
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(scroll, 940, 184);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), table);
    gtk_box_append(GTK_BOX(content), scroll);

    app.statistics_compare_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.statistics_compare_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(app.statistics_compare_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app.statistics_compare_label), 74);
    gtk_widget_add_css_class(app.statistics_compare_label,
                             "statistics-compare");
    gtk_box_append(GTK_BOX(content), app.statistics_compare_label);
    gtk_widget_add_css_class(content, "statistics-panel");

    card = make_card(content);
    gtk_widget_set_size_request(card, 980, -1);
    app.statistics_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(app.statistics_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(app.statistics_revealer),
                                         180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(app.statistics_revealer), TRUE);
    gtk_revealer_set_child(GTK_REVEALER(app.statistics_revealer), card);
    gtk_box_append(GTK_BOX(panel), app.statistics_revealer);

    toggle_button = gtk_button_new_with_label("收起统计");
    gtk_widget_set_halign(toggle_button, GTK_ALIGN_CENTER);
    g_signal_connect(toggle_button, "clicked",
                     G_CALLBACK(on_statistics_toggle), NULL);
    gtk_box_append(GTK_BOX(panel), toggle_button);
    gtk_widget_set_halign(panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(panel, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(panel, 14);
    return panel;
}

/* 创建主窗口并组合各个界面区域。 */
static void activate(GtkApplication *application, gpointer data)
{
    GtkWidget *root;
    GtkWidget *topbar;
    GtkWidget *vehicle_panel;
    GtkWidget *station_panel;
    (void)data;

    memset(&app, 0, sizeof(app));
    app.simulation_speed = 1.0;
    app.simulation_time_limit = 240.0;
    timer_init(&app.timer, UI_STEP_INTERVAL_MS);
    app.selected_vehicle_id = -1;
    app.selected_station_id = -1;
    app.selected_charger_id = -1;
    app.map_zoom = 1.0;
    app.vehicle_icon = load_map_icon("assets/icons/car-front.png");
    app.station_icon = load_map_icon("assets/icons/charging-station.png");
    install_css();

    app.window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app.window),
                         "新能源汽车充电资源调度系统");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1440, 900);

    root = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(app.window), root);

    gtk_overlay_set_child(GTK_OVERLAY(root), build_map_panel());

    vehicle_panel = build_side_panel(
        "车辆操作", build_recommend_page(),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT, GTK_ALIGN_START, 390);
    station_panel = build_side_panel(
        "充电站操作", build_charger_page(),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT, GTK_ALIGN_END, 520);
    gtk_overlay_add_overlay(GTK_OVERLAY(root), vehicle_panel);
    gtk_overlay_add_overlay(GTK_OVERLAY(root), station_panel);
    gtk_overlay_add_overlay(GTK_OVERLAY(root),
                            build_completed_count_panel());

    topbar = gtk_center_box_new();
    gtk_widget_add_css_class(topbar, "map-topbar");
    gtk_widget_set_halign(topbar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(topbar, GTK_ALIGN_START);
    gtk_widget_set_margin_top(topbar, 14);
    gtk_widget_set_margin_start(topbar, 16);
    gtk_widget_set_margin_end(topbar, 16);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(topbar), build_header());
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(topbar), build_toolbar());
    app.status_label = gtk_label_new("正在准备数据…");
    gtk_widget_add_css_class(app.status_label, "status-pill");
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(topbar), app.status_label);
    gtk_overlay_add_overlay(GTK_OVERLAY(root), topbar);

    gtk_window_present(GTK_WINDOW(app.window));
    load_data();
}

int main(int argc, char **argv)
{
    GtkApplication *application;
    int status;

    application = gtk_application_new(
        "cn.edu.charging.scheduler.gtk4", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(application, "shutdown", G_CALLBACK(on_shutdown), NULL);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
