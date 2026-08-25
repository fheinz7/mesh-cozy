#include "mesh_discovery.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    bool relay_state[DISCOVERY_MAX_NODES];
    const discovery_topology_t *topology;
} example_platform_t;

static size_t node_index(const example_platform_t *platform, uint16_t address)
{
    return discovery_topology_find_node(platform->topology, address);
}

/* Replace this with bt_mesh_cfg_cli_relay_set() and wait for Relay Status. */
static int set_remote_relay(void *context, uint16_t address, bool enabled,
                            uint8_t retransmit_count, uint16_t interval_ms)
{
    example_platform_t *platform = context;
    size_t index = node_index(platform, address);
    if (index == DISCOVERY_INVALID_INDEX) return -1;
    platform->relay_state[index] = enabled;
    printf("Config Relay Set: node=0x%04X state=%s count=%u interval=%u ms\n",
           address, enabled ? "enabled" : "disabled", retransmit_count, interval_ms);
    return 0;
}

/* Replace this with VERIFY_REQUEST/VERIFY_RESPONSE through the vendor model. */
static int verify_node(void *context, uint16_t address, uint8_t attempts,
                       uint8_t *successes)
{
    (void)context;
    printf("Verify node 0x%04X: %u/%u responses\n", address, attempts, attempts);
    *successes = attempts;
    return 0;
}

/* Replace this with settings_save_one() or an NVS transaction. */
static int persist_plan(void *context, const discovery_gateway_t *gateway)
{
    (void)context;
    printf("Persisting topology for epoch %u\n", gateway->epoch);
    return 0;
}

static int publish_commit(void *context, uint16_t epoch, uint16_t relay_count,
                          uint8_t maximum_ring)
{
    (void)context;
    printf("DISCOVERY_COMMIT: epoch=%u relays=%u maximum_ring=%u\n",
           epoch, relay_count, maximum_ring);
    return 0;
}

static int publish_abort(void *context, uint16_t epoch, int reason)
{
    (void)context;
    printf("DISCOVERY_ABORT: epoch=%u reason=%d\n", epoch, reason);
    return 0;
}

static void add_node(discovery_topology_t *topology, uint16_t address,
                     discovery_node_role_t role, bool relay_capable)
{
    discovery_topology_node_t node;
    memset(&node, 0, sizeof(node));
    node.address = address;
    node.role = (uint8_t)role;
    node.relay_capable = relay_capable;
    node.preliminary_ring = DISCOVERY_UNKNOWN_RING;
    node.calculated_ring = DISCOVERY_UNKNOWN_RING;
    if (discovery_topology_add_node(topology, &node, NULL) != DISCOVERY_OK)
        puts("Could not add topology node");
}

/* A report from receiver B about transmitter A creates the directed link A -> B. */
static void add_direction(discovery_topology_t *topology, uint16_t transmitter,
                          uint16_t receiver, uint16_t pdr, int8_t rssi)
{
    discovery_report_entry_t report;
    memset(&report, 0, sizeof(report));
    report.neighbor_address = transmitter;
    report.pdr_per_mille = pdr;
    report.average_rssi = rssi;
    report.minimum_rssi = (int8_t)(rssi - 3);
    report.maximum_rssi = (int8_t)(rssi + 3);
    report.received_probes = 9U;
    (void)discovery_topology_add_report(topology, receiver, &report, 1U);
}

static void add_bidirectional_link(discovery_topology_t *topology, uint16_t a,
                                   uint16_t b, uint16_t pdr, int8_t rssi)
{
    add_direction(topology, a, b, pdr, rssi);
    add_direction(topology, b, a, pdr, rssi);
}

int main(void)
{
    discovery_gateway_t gateway;
    discovery_relay_watchdog_plan_t plans[DISCOVERY_MAX_NODES];
    discovery_relay_failure_simulation_t simulations[DISCOVERY_MAX_NODES];
    discovery_gateway_io_t io;
    example_platform_t platform;
    size_t gateway_index, plan_count = 0U, i;
    discovery_result_t result;

    discovery_gateway_init(&gateway, NULL);
    gateway.epoch = 40U; /* Value restored from persistent storage. */

    add_node(&gateway.topology, 0x0001U, NODE_ROLE_GATEWAY, true);
    add_node(&gateway.topology, 0x0101U, NODE_ROLE_RELAY_CANDIDATE, true);
    add_node(&gateway.topology, 0x0102U, NODE_ROLE_RELAY_CANDIDATE, true);
    add_node(&gateway.topology, 0x0111U, NODE_ROLE_RELAY_CANDIDATE, true);
    add_node(&gateway.topology, 0x0112U, NODE_ROLE_RELAY_CANDIDATE, true);
    add_node(&gateway.topology, 0x0201U, NODE_ROLE_COMMON, false);
    add_node(&gateway.topology, 0x0202U, NODE_ROLE_COMMON, false);

    /* Both candidates are direct gateway neighbors and jointly cover ring 2. */
    add_bidirectional_link(&gateway.topology, 0x0001U, 0x0101U, 960U, -61);
    add_bidirectional_link(&gateway.topology, 0x0001U, 0x0102U, 940U, -65);
    add_bidirectional_link(&gateway.topology, 0x0101U, 0x0102U, 930U, -67);
    add_bidirectional_link(&gateway.topology, 0x0001U, 0x0111U, 910U, -70);
    add_bidirectional_link(&gateway.topology, 0x0001U, 0x0112U, 905U, -71);
    add_bidirectional_link(&gateway.topology, 0x0101U, 0x0111U, 920U, -68);
    add_bidirectional_link(&gateway.topology, 0x0101U, 0x0112U, 900U, -73);
    add_bidirectional_link(&gateway.topology, 0x0102U, 0x0111U, 900U, -72);
    add_bidirectional_link(&gateway.topology, 0x0102U, 0x0112U, 920U, -68);
    add_bidirectional_link(&gateway.topology, 0x0101U, 0x0201U, 920U, -72);
    add_bidirectional_link(&gateway.topology, 0x0102U, 0x0201U, 910U, -74);
    add_bidirectional_link(&gateway.topology, 0x0101U, 0x0202U, 900U, -76);
    add_bidirectional_link(&gateway.topology, 0x0102U, 0x0202U, 905U, -75);
    add_bidirectional_link(&gateway.topology, 0x0111U, 0x0201U, 880U, -78);
    add_bidirectional_link(&gateway.topology, 0x0111U, 0x0202U, 870U, -80);
    add_bidirectional_link(&gateway.topology, 0x0112U, 0x0201U, 870U, -80);
    add_bidirectional_link(&gateway.topology, 0x0112U, 0x0202U, 880U, -78);

    gateway_index = discovery_topology_find_node(&gateway.topology, 0x0001U);
    result = discovery_gateway_start(&gateway, gateway_index);
    if (result != DISCOVERY_OK) return 1;

    /* In firmware, ENTER/READY, probes and reports occur before this call. */
    result = discovery_gateway_compute_plan(&gateway);
    if (result != DISCOVERY_OK) {
        printf("Could not calculate relay plan: %d\n", result);
        return 1;
    }

    puts("Selected active relays:");
    for (i = 0U; i < gateway.topology.node_count; ++i) {
        const discovery_topology_node_t *node = &gateway.topology.nodes[i];
        if (node->selected_relay)
            printf("  0x%04X (ring %u)\n", node->address, node->calculated_ring);
    }

    result = discovery_select_relay_backups(
        &gateway.topology, gateway_index, &gateway.configuration, 7U,
        plans, &plan_count, simulations);
    printf("Watchdog plans: %zu (selection result %d)\n", plan_count, result);
    for (i = 0U; i < plan_count; ++i) {
        size_t j;
        printf("  primary=0x%04X", plans[i].primary_relay);
        for (j = 0U; j < plans[i].backup_count; ++j)
            printf(" backup[%zu]=0x%04X", j, plans[i].backup_nodes[j]);
        printf(" preserved=%s\n", simulations[i].connectivity_preserved ? "yes" : "no");
    }

    memset(&platform, 0, sizeof(platform));
    platform.topology = &gateway.topology;
    memset(&io, 0, sizeof(io));
    io.context = &platform;
    io.set_relay = set_remote_relay;
    io.verify = verify_node;
    io.persist = persist_plan;
    io.publish_commit = publish_commit;
    io.publish_abort = publish_abort;

    result = discovery_gateway_apply_plan(&gateway, &io);
    printf("Final gateway state=%d result=%d\n", gateway.state, result);
    return result == DISCOVERY_OK ? 0 : 1;
}
