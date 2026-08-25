# Bluetooth Mesh topology discovery

Portable C99 implementation of the algorithm in `SPECS.md`. It contains the
parts that can be tested independently from Zephyr/NCS:

- duplicate-safe TTL-0 probe accounting, RSSI and PDR;
- explicit little-endian wire codecs and report reassembly bitmap;
- directed report ingestion and bidirectional link validation;
- BFS ring calculation and TTL/BFS mismatch marking;
- connected, ring-aware greedy relay selection with degraded coverage output;
- enable-near-to-far / disable-far-to-near transaction, verification and rollback;
- callback boundary for the Bluetooth Mesh Configuration Client and persistence.
- local relay watchdog plans, failure simulation and automatic backup takeover;
- TTL-0 primary/takeover beacons, priority jitter, hysteresis and handover overlap.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

The repository includes two portable examples:

- `examples/gateway_example.c`: builds reports into a topology, calculates the
  active relays and watchdog plans, simulates failures, applies Relay Set,
  verifies every node and commits.
- `examples/node_example.c`: shows both roles available to a node—periodic
  TTL-0 beacon transmission while it is a primary relay, and autonomous
  monitoring/takeover while it is a backup.

With a multi-configuration generator such as Visual Studio, run them with:

```powershell
.\build\Debug\discovery_gateway_example.exe
.\build\Debug\discovery_node_example.exe
```

The example callbacks print operations to the console. In firmware, replace
them with the corresponding Zephyr Bluetooth Mesh model sends, Configuration
Client calls and Configuration Server state updates.

## nRF Connect SDK integration

Add the three `src/*.c` files and `include/` to the Zephyr application. The
vendor model is responsible for scheduling the protocol phases and feeding
decoded reports/probes into this library. Keep neighbor probes at TTL 0 and
Network Transmit count 0.

Implement `discovery_gateway_io_t` as follows:

- `set_relay`: call the Configuration Client Relay Set operation and return
  only after the matching Relay Status is received;
- `verify`: send three unicast verify requests and return the response count;
- `persist`: write the committed epoch/topology using Zephyr settings/NVS;
- `publish_commit` and `publish_abort`: publish the vendor status message.

Call `discovery_gateway_compute_plan()` after all complete neighbor reports are
loaded, then `discovery_gateway_apply_plan()`. On any configuration or
verification failure it restores the previous relay bitmap before publishing
abort. The gateway node is always retained in the selected relay set.

After relay selection, call `discovery_select_relay_backups()` and distribute
each generated watchdog plan. A backup node initializes its local monitor with
`discovery_relay_backup_init()`, passes received TTL-0 beacons to the matching
`discovery_relay_backup_on_*()` function, and calls
`discovery_relay_backup_process()` periodically. Its `set_local_relay` callback
must update the same Relay state exposed by the Bluetooth Mesh Configuration
Server; it must not maintain a separate application-only relay flag.
