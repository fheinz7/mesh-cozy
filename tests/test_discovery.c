#include "mesh_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void add_node(discovery_topology_t *t, uint16_t address, uint8_t role,
                     bool capable, uint8_t preliminary)
{
    discovery_topology_node_t n;
    memset(&n, 0, sizeof(n));
    n.address = address; n.role = role; n.relay_capable = capable;
    n.preliminary_ring = preliminary; n.calculated_ring = DISCOVERY_UNKNOWN_RING;
    assert(discovery_topology_add_node(t, &n, NULL) == DISCOVERY_OK);
}

static void link_nodes(discovery_topology_t *t, size_t a, size_t b,
                       uint16_t pdr, int8_t rssi)
{
    t->links[a][b].measured = t->links[b][a].measured = true;
    t->links[a][b].pdr_per_mille = t->links[b][a].pdr_per_mille = pdr;
    t->links[a][b].average_rssi = t->links[b][a].average_rssi = rssi;
}

static void test_protocol(void)
{
    uint8_t wire[8];
    discovery_header_t in = {1U, MSG_RING_PROBE, 0x1234U, 0xABCDU}, out;
    discovery_report_entry_t ri = {0x3344U, 875U, -77, -90, -65, 9U}, ro;
    assert(discovery_encode_header(wire, sizeof(wire), &in) == 6U);
    assert(wire[2] == 0x34U && wire[3] == 0x12U);
    assert(discovery_decode_header(&out, wire, 6U) == DISCOVERY_OK);
    assert(out.epoch == in.epoch && out.sequence == in.sequence);
    assert(discovery_encode_report_entry(wire, sizeof(wire), &ri) == 8U);
    assert(discovery_decode_report_entry(&ro, wire, sizeof(wire)) == DISCOVERY_OK);
    assert(ro.neighbor_address == ri.neighbor_address && ro.average_rssi == -77);
}

static void test_measurement(void)
{
    discovery_neighbor_table_t table;
    discovery_report_entry_t report[2];
    size_t count = 0U;
    discovery_neighbor_table_init(&table, 1U, 5U);
    assert(discovery_register_probe(&table, 2U, 0U, -80) == DISCOVERY_OK);
    assert(discovery_register_probe(&table, 2U, 0U, -20) == DISCOVERY_OK);
    assert(discovery_register_probe(&table, 2U, 1U, -70) == DISCOVERY_OK);
    assert(table.entries[0].received_count == 2U);
    assert(discovery_average_rssi(&table.entries[0]) == -75);
    assert(discovery_pdr_per_mille(&table.entries[0], 4U) == 500U);
    assert(discovery_make_report(&table, 4U, report, 2U, &count) == DISCOVERY_OK);
    assert(count == 1U && report[0].minimum_rssi == -80);
}

static void test_reassembly(void)
{
    discovery_report_reassembly_t r;
    discovery_reassembly_init(&r, 2U, 7U, 3U);
    assert(discovery_reassembly_accept(&r, 7U, 2U) == DISCOVERY_OK);
    assert(discovery_reassembly_missing(&r) == 3U);
    assert(discovery_reassembly_accept(&r, 7U, 0U) == DISCOVERY_OK);
    assert(discovery_reassembly_accept(&r, 7U, 1U) == DISCOVERY_OK);
    assert(r.complete);
    assert(discovery_reassembly_accept(&r, 6U, 0U) == DISCOVERY_STALE_EPOCH);
}

static void test_topology(void)
{
    discovery_topology_t t;
    discovery_configuration_t c = discovery_default_configuration;
    uint8_t coverage[DISCOVERY_MAX_NODES];
    bool degraded[DISCOVERY_MAX_NODES];
    discovery_topology_init(&t);
    c.maximum_relays_per_ring = 0U;
    add_node(&t, 1U, NODE_ROLE_GATEWAY, true, 0U);
    add_node(&t, 2U, NODE_ROLE_RELAY_CANDIDATE, true, 1U);
    add_node(&t, 3U, NODE_ROLE_RELAY_CANDIDATE, true, 1U);
    add_node(&t, 4U, NODE_ROLE_COMMON, false, 2U);
    link_nodes(&t, 0U, 1U, 950U, -60);
    link_nodes(&t, 0U, 2U, 920U, -65);
    link_nodes(&t, 1U, 3U, 900U, -70);
    link_nodes(&t, 2U, 3U, 910U, -72);
    assert(discovery_calculate_rings(&t, 0U, &c) == DISCOVERY_OK);
    assert(t.nodes[3].calculated_ring == 2U);
    assert(discovery_select_relays(&t, 0U, &c, coverage, degraded) == DISCOVERY_OK);
    assert(t.nodes[1].selected_relay && t.nodes[2].selected_relay);
    assert(coverage[3] == 2U && !degraded[3]);
    assert(discovery_relay_set_is_connected(&t, 0U, &c));
}

typedef struct { int enables; int disables; int takeovers; int restores; } fake_io_t;
static int fake_set(void *ctx, bool enabled)
{
    fake_io_t *f = ctx; if (enabled) ++f->enables; else ++f->disables; return 0;
}
static int fake_takeover(void *ctx, uint16_t p, uint16_t r, uint16_t g,
                         uint8_t priority, uint32_t age)
{
    fake_io_t *f = ctx; (void)p; (void)r; (void)g; (void)priority; (void)age;
    ++f->takeovers; return 0;
}
static int fake_beacon(void *ctx, uint16_t p, uint16_t r, uint16_t g,
                       uint16_t sequence, uint8_t priority)
{
    (void)ctx; (void)p; (void)r; (void)g; (void)sequence; (void)priority; return 0;
}
static int fake_restored(void *ctx, uint16_t p, uint16_t r, uint16_t g)
{
    fake_io_t *f = ctx; (void)p; (void)r; (void)g; ++f->restores; return 0;
}

static void test_backup_selection(void)
{
    discovery_topology_t t;
    discovery_configuration_t c = discovery_default_configuration;
    discovery_relay_watchdog_plan_t plans[DISCOVERY_MAX_NODES];
    discovery_relay_failure_simulation_t sims[DISCOVERY_MAX_NODES];
    size_t count = 0U;
    discovery_topology_init(&t);
    add_node(&t, 1U, NODE_ROLE_GATEWAY, true, 0U);
    add_node(&t, 2U, NODE_ROLE_RELAY_CANDIDATE, true, 1U);
    add_node(&t, 3U, NODE_ROLE_RELAY_CANDIDATE, true, 1U);
    add_node(&t, 4U, NODE_ROLE_COMMON, false, 2U);
    link_nodes(&t, 0U, 1U, 950U, -60); link_nodes(&t, 0U, 2U, 950U, -62);
    link_nodes(&t, 1U, 2U, 930U, -65); link_nodes(&t, 1U, 3U, 900U, -70);
    link_nodes(&t, 2U, 3U, 910U, -69);
    assert(discovery_calculate_rings(&t, 0U, &c) == DISCOVERY_OK);
    t.nodes[0].selected_relay = true; t.nodes[1].selected_relay = true;
    assert(discovery_select_relay_backups(&t, 0U, &c, 42U, plans, &count, sims)
           == DISCOVERY_OK);
    assert(count == 1U && plans[0].primary_relay == 2U);
    assert(plans[0].backup_count == 1U && plans[0].backup_nodes[0] == 3U);
    assert(sims[0].connectivity_preserved && sims[0].affected_nodes == 0U);
}

static void test_backup_monitor(void)
{
    discovery_relay_watchdog_plan_t plan;
    discovery_relay_backup_monitor_t m;
    fake_io_t fake = {0};
    discovery_relay_backup_io_t io = {
        .context = &fake, .set_local_relay = fake_set,
        .send_takeover_beacon = fake_beacon,
        .send_takeover_event = fake_takeover,
        .send_restored_event = fake_restored
    };
    discovery_relay_beacon_sender_t sender;
    discovery_relay_alive_beacon_t beacon;
    memset(&plan, 0, sizeof(plan));
    plan.primary_relay = 2U; plan.configuration_generation = 9U;
    plan.beacon_period_ms = 2000U; plan.missed_beacon_threshold = 3U;
    plan.recovery_stable_ms = 10000U; plan.minimum_relay_active_ms = 15000U;
    discovery_relay_backup_init(&m, 3U, 7U, &plan, 0U, 0U);
    discovery_relay_beacon_sender_init(&sender, 2U, 7U, 9U, 0U, 2000U, 0U);
    assert(discovery_relay_beacon_prepare(&sender, true, 0U, &beacon));
    assert(beacon.header.message_type == MSG_RELAY_ALIVE_BEACON && beacon.relay_state == 1U);
    assert(!discovery_relay_beacon_prepare(&sender, true, 1999U, &beacon));
    assert(discovery_relay_backup_process(&m, 6000U, &io) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_SUSPECTING);
    assert(discovery_relay_backup_process(&m, 6000U, &io) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_WAIT_TAKEOVER);
    assert(discovery_relay_backup_process(&m, 6111U, &io) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_ACTIVE && fake.enables == 1 && fake.takeovers == 1);
    assert(discovery_relay_backup_on_beacon(&m, 7U, 2U, 9U, 1U, 0U, true,
                                            -70, -90, 7000U) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_RECOVERY_MONITORING);
    assert(discovery_relay_backup_on_beacon(&m, 7U, 2U, 9U, 2U, 0U, true,
                                            -70, -90, 12000U) == DISCOVERY_OK);
    assert(discovery_relay_backup_on_beacon(&m, 7U, 2U, 9U, 3U, 0U, true,
                                            -70, -90, 17000U) == DISCOVERY_OK);
    assert(discovery_relay_backup_on_beacon(&m, 7U, 2U, 9U, 4U, 0U, true,
                                            -70, -90, 21000U) == DISCOVERY_OK);
    assert(discovery_relay_backup_process(&m, 21111U, &io) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_RETURNING_NORMAL);
    assert(discovery_relay_backup_on_beacon(&m, 7U, 2U, 9U, 5U, 0U, true,
                                            -70, -90, 24000U) == DISCOVERY_OK);
    assert(discovery_relay_backup_process(&m, 24111U, &io) == DISCOVERY_OK);
    assert(m.state == RELAY_BACKUP_NORMAL && !m.local_relay_enabled);
    assert(fake.disables == 1 && fake.restores == 1);
}

int main(void)
{
    assert(discovery_epoch_is_newer(1U, UINT16_MAX));
    assert(discovery_next_epoch(UINT16_MAX) == 1U);
    test_protocol(); test_measurement(); test_reassembly(); test_topology();
    test_backup_selection(); test_backup_monitor();
    puts("all discovery tests passed");
    return 0;
}
