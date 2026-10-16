# EV Charging Scheduling Simulator

[简体中文](README.md) | **English**

[![C11 Build and Tests](https://github.com/Qianying-Chuugu/ev-charging-scheduling-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/Qianying-Chuugu/ev-charging-scheduling-simulator/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> A visual scheduling laboratory built with **C11 and GTK 4**, where graphs, heaps, hash tables, and linked lists directly drive route planning, queue scheduling, and discrete-time simulation.

The application simulates EV request submission, driving, station queues, charging, charger failures, and recovery on an interactive road network. It also compares two strategies in real time:

- **Project strategy:** multi-factor station recommendation plus dynamic-priority queues.
- **Control strategy:** nearest reachable station plus first-come, first-served queues.

**Keywords:** `C11` · `GTK 4` · `Data Structures` · `Dijkstra` · `Priority Queue` · `Discrete-Time Simulation`

<p align="center">
  <img src="assets/screenshots/overview.png" alt="Main application window" width="100%">
</p>

## Why This Project Is Useful

| Audience | What the project demonstrates |
| --- | --- |
| C and data-structure students | Modular C design, dynamic memory, file I/O, graphs, heaps, hash tables, linked lists, and tests in one application |
| GTK 4 learners | Window layouts, custom map drawing, zooming, dragging, resizable panels, events, timers, and live state updates |
| Simulation and scheduling learners | An end-to-end flow from request submission and route planning to queuing, charging, and metric collection |
| Experiment-framework users | CSV-driven scenarios and separated recommendation, scheduling, simulation, and UI modules that can be replaced independently |

## Highlights

### Complete Charging Workflow

- Submit a single vehicle, submit randomized batches, or automatically submit every 10 simulated minutes.
- Compute and animate routes on a directed weighted road network.
- Simulate arrival, queuing, charger allocation, charging completion, and resource release.
- Add, remove, fail, and repair chargers from the station panel.
- Recycle completed vehicles after 5–10 simulated minutes for continuous traffic experiments.

### Explainable Algorithms

- Dijkstra shortest-path search with a binary min-heap and predecessor recovery.
- Station scoring based on travel, estimated waiting, charging time, and low-battery risk.
- Dynamic-priority waiting queues implemented with a binary max-heap.
- A nearest-station and FCFS control group for quantitative comparison.

### Interactive GTK 4 Interface

- Zoomable and draggable map with selectable vehicles, stations, and routes.
- Collapsible, resizable vehicle and charging-station side panels.
- Simulation duration, speed, experiment mode, automatic submission, and continuous-traffic controls.
- Live comparison table for completion time, queue time, travel time, and throughput.

## Interface Preview

<table>
  <tr>
    <td width="50%" align="center">
      <img src="assets/screenshots/vehicle-recommendation.png" alt="Vehicle recommendation panel"><br>
      <sub>Ranked station recommendations</sub>
    </td>
    <td width="50%" align="center">
      <img src="assets/screenshots/station-management.png" alt="Station management panel"><br>
      <sub>Queue, charging, and charger maintenance</sub>
    </td>
  </tr>
</table>

<p align="center">
  <img src="assets/screenshots/route-visualization.png" alt="Route animation" width="88%"><br>
  <sub>Vehicle location and the currently planned route</sub>
</p>

<p align="center">
  <img src="assets/screenshots/statistics-comparison.png" alt="Experiment statistics comparison" width="88%"><br>
  <sub>Live comparison of the project and control strategies</sub>
</p>

## Quick Start

### Windows with MSYS2 UCRT64

Install the toolchain and GTK 4:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-pkgconf make
```

The Windows scripts expect the runtime under `C:\msys64\ucrt64\bin`.

```bat
构建GTK4.bat --no-pause
启动GTK4.bat
```

The generated application is `build/charging_system_gtk4.exe`.

### GNU Make

```bash
make          # Build the GTK application and command-line demo
make gui      # Build only the GTK application
make test     # Build and run all tests
make clean    # Remove generated executables
```

> Run the application with the repository root as its working directory so relative data and icon paths can be resolved.

## Algorithms and Data Structures

| Problem | Implementation | Role |
| --- | --- | --- |
| Road network | Adjacency list, directed weighted graph, road classes | Efficient network traversal |
| Route planning | Dijkstra, binary min-heap, predecessor recovery | Shortest travel-time path to candidate stations |
| Queue scheduling | Binary max-heap, dynamic priority | Prioritize low-battery or long-waiting vehicles |
| Entity lookup | Chained hash table and ID arrays | Fast vehicle and station lookup |
| Charger management | Singly linked list | Dynamic device creation, state updates, and removal |
| Simulation | Fixed time steps and GTK periodic timer | Advance driving, waiting, and charging states |
| Data exchange | Validated CSV loading and state output | Replace scenarios without recompiling algorithms |

Dynamic queue priority:

```text
priority = urgency × 30
         + (100 - current battery percentage) × 0.5
         + waiting time × 0.2
```

Station recommendation score:

```text
total time = travel time + estimated waiting time + charging time
score      = total time + low-battery risk penalty
```

## Simulation Flow

```mermaid
flowchart LR
    A[Load CSV data] --> B[Build graph and entity indexes]
    B --> C[Submit charging request]
    C --> D[Calculate candidate routes]
    D --> E[Estimate waiting and charging time]
    E --> F[Select station]
    F --> G[Drive]
    G --> H[Queue at station]
    H --> I[Allocate charger]
    I --> J[Advance charging]
    J --> K[Complete and release resources]
```

## Experiment Metrics

The UI tracks each strategy independently:

- average and maximum total completion time;
- average and maximum queue time;
- average travel time;
- cumulative completed vehicles;
- completed vehicles per simulated hour.

See [`docs/EXPERIMENTS.md`](docs/EXPERIMENTS.md) for metric definitions and a reproducible comparison procedure.

## Project Structure

```text
assets/                 Icons and interface screenshots
data/input/             Road, vehicle, station, and charger CSV files
data/output/            Generated outputs, ignored by Git
src/app/                Command-line entry point
src/common/             Shared models and limits
src/module1_graph/      Graph and shortest paths
src/module2_scheduler/  Priority queue scheduling
src/module3_entity/     Entity indexes
src/module4_charger/    Charger management
src/module5_recommend/  Station recommendation
src/module6_simulation/ Discrete-time simulation
src/module7_fileio/     CSV loading and saving
src/module8_ui/         GTK 4 interface and map
src/module9_timer/      GTK timer control
tests/                  Unit and integration tests
```

The current sample includes 305 graph nodes, 442 road records, 60 vehicles, 5 stations, and 24 chargers. It models a relative experimental network rather than real-time navigation data.

## Contributing

Bug reports, feature proposals, documentation improvements, tests, and algorithm experiments are welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a Pull Request.

Possible extensions include traffic congestion, time-of-use pricing, charging reservations, alternative scheduling algorithms, battery degradation, and larger road networks.

## License

Source code is released under the [MIT License](LICENSE). Vehicle and station icons are derived from Lucide Icons and remain subject to the ISC license in [`assets/icons/LUCIDE-LICENSE.txt`](assets/icons/LUCIDE-LICENSE.txt).
