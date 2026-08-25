#include "mesh_discovery.h"

#include <limits.h>
#include <string.h>

const discovery_configuration_t discovery_default_configuration = {
    16U, 10U, 300U, 20U, 800U, -90, 2U, 3U, 3U, 2U, 4U, 3000U, 1U, 20U
};

bool discovery_epoch_is_newer(uint16_t candidate, uint16_t current)
{
    return (int16_t)(candidate - current) > 0;
}

uint16_t discovery_next_epoch(uint16_t current)
{
    uint16_t next = (uint16_t)(current + 1U);
    return next == 0U ? 1U : next;
}

discovery_health_t discovery_classify_pdr(uint16_t pdr)
{
    if (pdr >= 900U) return DISCOVERY_HEALTHY;
    if (pdr >= 750U) return DISCOVERY_DEGRADED;
    return DISCOVERY_FAILED;
}

uint32_t discovery_slot_start_ms(uint32_t local_phase_start_ms,
                                 uint16_t slot_index, uint16_t slot_duration_ms)
{
    return local_phase_start_ms + (uint32_t)slot_index * slot_duration_ms;
}

uint8_t discovery_report_ttl(uint8_t estimated_ring, bool ring_reliable,
                             uint8_t maximum_ttl, uint8_t margin)
{
    uint16_t ttl;
    if (!ring_reliable || estimated_ring == DISCOVERY_UNKNOWN_RING) return maximum_ttl;
    ttl = (uint16_t)estimated_ring + margin;
    return (uint8_t)(ttl > maximum_ttl ? maximum_ttl : ttl);
}

void discovery_neighbor_table_init(discovery_neighbor_table_t *table,
                                   uint16_t owner, uint16_t epoch)
{
    if (table == NULL) return;
    memset(table, 0, sizeof(*table));
    table->owner_address = owner;
    table->epoch = epoch;
}

discovery_result_t discovery_register_probe(discovery_neighbor_table_t *table,
                                            uint16_t source, uint8_t probe_index,
                                            int8_t rssi)
{
    discovery_neighbor_entry_t *entry = NULL;
    size_t i;
    uint16_t mask;
    if (table == NULL || source == 0U || probe_index >= DISCOVERY_MAX_PROBES)
        return DISCOVERY_INVALID_ARGUMENT;
    for (i = 0U; i < table->entry_count; ++i) {
        if (table->entries[i].address == source) { entry = &table->entries[i]; break; }
    }
    if (entry == NULL) {
        if (table->entry_count >= DISCOVERY_MAX_NEIGHBORS) return DISCOVERY_CAPACITY;
        entry = &table->entries[table->entry_count++];
        memset(entry, 0, sizeof(*entry));
        entry->address = source;
        entry->rssi_min = INT8_MAX;
        entry->rssi_max = INT8_MIN;
        entry->valid = true;
    }
    mask = (uint16_t)(UINT16_C(1) << probe_index);
    if ((entry->received_bitmap & mask) != 0U) return DISCOVERY_OK;
    entry->received_bitmap |= mask;
    ++entry->received_count;
    entry->rssi_sum = (int16_t)(entry->rssi_sum + rssi);
    if (rssi < entry->rssi_min) entry->rssi_min = rssi;
    if (rssi > entry->rssi_max) entry->rssi_max = rssi;
    return DISCOVERY_OK;
}

int8_t discovery_average_rssi(const discovery_neighbor_entry_t *entry)
{
    if (entry == NULL || entry->received_count == 0U) return INT8_MIN;
    return (int8_t)(entry->rssi_sum / (int16_t)entry->received_count);
}

uint16_t discovery_pdr_per_mille(const discovery_neighbor_entry_t *entry,
                                 uint8_t expected_probes)
{
    uint32_t result;
    if (entry == NULL || expected_probes == 0U) return 0U;
    result = (uint32_t)entry->received_count * 1000U / expected_probes;
    return (uint16_t)(result > 1000U ? 1000U : result);
}

discovery_result_t discovery_make_report(const discovery_neighbor_table_t *table,
                                         uint8_t expected_probes,
                                         discovery_report_entry_t *entries,
                                         size_t capacity, size_t *count)
{
    size_t i;
    if (table == NULL || entries == NULL || count == NULL || expected_probes == 0U)
        return DISCOVERY_INVALID_ARGUMENT;
    if (capacity < table->entry_count) return DISCOVERY_CAPACITY;
    for (i = 0U; i < table->entry_count; ++i) {
        const discovery_neighbor_entry_t *in = &table->entries[i];
        entries[i].neighbor_address = in->address;
        entries[i].pdr_per_mille = discovery_pdr_per_mille(in, expected_probes);
        entries[i].average_rssi = discovery_average_rssi(in);
        entries[i].minimum_rssi = in->rssi_min;
        entries[i].maximum_rssi = in->rssi_max;
        entries[i].received_probes = in->received_count;
    }
    *count = table->entry_count;
    return DISCOVERY_OK;
}

void discovery_gateway_init(discovery_gateway_t *gateway,
                            const discovery_configuration_t *configuration)
{
    if (gateway == NULL) return;
    memset(gateway, 0, sizeof(*gateway));
    gateway->state = DISCOVERY_STATE_IDLE;
    gateway->gateway_index = DISCOVERY_INVALID_INDEX;
    gateway->configuration = configuration != NULL ? *configuration
                                                    : discovery_default_configuration;
    discovery_topology_init(&gateway->topology);
}

discovery_result_t discovery_gateway_start(discovery_gateway_t *gateway,
                                           size_t gateway_index)
{
    if (gateway == NULL || gateway->state != DISCOVERY_STATE_IDLE)
        return gateway == NULL ? DISCOVERY_INVALID_ARGUMENT : DISCOVERY_WRONG_STATE;
    if (gateway_index >= gateway->topology.node_count) return DISCOVERY_INVALID_ARGUMENT;
    gateway->epoch = discovery_next_epoch(gateway->epoch);
    gateway->sequence = 0U;
    gateway->gateway_index = gateway_index;
    gateway->state = DISCOVERY_STATE_ENTER;
    return DISCOVERY_OK;
}

discovery_result_t discovery_gateway_compute_plan(discovery_gateway_t *gateway)
{
    discovery_result_t result;
    size_t i;
    if (gateway == NULL) return DISCOVERY_INVALID_ARGUMENT;
    gateway->state = DISCOVERY_STATE_CALCULATE_RINGS;
    result = discovery_calculate_rings(&gateway->topology, gateway->gateway_index,
                                       &gateway->configuration);
    if (result != DISCOVERY_OK) return result;
    gateway->state = DISCOVERY_STATE_SELECT_RELAYS;
    result = discovery_select_relays(&gateway->topology, gateway->gateway_index,
                                     &gateway->configuration, gateway->coverage,
                                     gateway->degraded);
    if (result != DISCOVERY_OK) return result;
    for (i = 0U; i < gateway->topology.node_count; ++i) {
        gateway->transaction.previous_relay[i] = gateway->topology.nodes[i].currently_relay;
        gateway->transaction.planned_relay[i] = gateway->topology.nodes[i].selected_relay;
        gateway->transaction.applied_relay[i] = gateway->topology.nodes[i].currently_relay;
    }
    gateway->state = DISCOVERY_STATE_ENABLE_NEW_RELAYS;
    return DISCOVERY_OK;
}

static discovery_result_t set_ring(discovery_gateway_t *g,
                                   const discovery_gateway_io_t *io,
                                   uint8_t ring, bool enable)
{
    size_t i;
    for (i = 0U; i < g->topology.node_count; ++i) {
        discovery_topology_node_t *n = &g->topology.nodes[i];
        bool desired = g->transaction.planned_relay[i];
        if (i == g->gateway_index || n->calculated_ring != ring) continue;
        if ((enable && desired && !g->transaction.applied_relay[i]) ||
            (!enable && !desired && g->transaction.applied_relay[i])) {
            if (io->set_relay(io->context, n->address, enable,
                              g->configuration.relay_retransmit_count,
                              g->configuration.relay_retransmit_interval_ms) != 0)
                return DISCOVERY_IO_ERROR;
            g->transaction.applied_relay[i] = enable;
        }
    }
    return DISCOVERY_OK;
}

static bool verify_all(discovery_gateway_t *g, const discovery_gateway_io_t *io)
{
    size_t i;
    for (i = 0U; i < g->topology.node_count; ++i) {
        uint8_t successes = 0U;
        if (g->absent[i]) continue;
        if (io->verify(io->context, g->topology.nodes[i].address,
                       g->configuration.verify_attempts, &successes) != 0 ||
            successes < g->configuration.minimum_verify_successes) return false;
    }
    return true;
}

void discovery_gateway_abort(discovery_gateway_t *g,
                             const discovery_gateway_io_t *io, int reason)
{
    size_t i;
    if (g == NULL || io == NULL || io->set_relay == NULL) return;
    g->state = DISCOVERY_STATE_ROLLBACK;
    for (i = 0U; i < g->topology.node_count; ++i) {
        if (i == g->gateway_index || g->transaction.applied_relay[i] ==
                                     g->transaction.previous_relay[i]) continue;
        (void)io->set_relay(io->context, g->topology.nodes[i].address,
                            g->transaction.previous_relay[i],
                            g->configuration.relay_retransmit_count,
                            g->configuration.relay_retransmit_interval_ms);
        g->transaction.applied_relay[i] = g->transaction.previous_relay[i];
    }
    if (io->publish_abort != NULL) (void)io->publish_abort(io->context, g->epoch, reason);
    g->state = DISCOVERY_STATE_IDLE;
}

discovery_result_t discovery_gateway_apply_plan(discovery_gateway_t *g,
                                                const discovery_gateway_io_t *io)
{
    uint8_t max_ring = 0U, ring;
    size_t i;
    uint16_t relay_count = 0U;
    discovery_result_t result;
    if (g == NULL || io == NULL || io->set_relay == NULL || io->verify == NULL)
        return DISCOVERY_INVALID_ARGUMENT;
    if (g->state != DISCOVERY_STATE_ENABLE_NEW_RELAYS) return DISCOVERY_WRONG_STATE;
    for (i = 0U; i < g->topology.node_count; ++i)
        if (g->topology.nodes[i].calculated_ring != DISCOVERY_UNKNOWN_RING &&
            g->topology.nodes[i].calculated_ring > max_ring)
            max_ring = g->topology.nodes[i].calculated_ring;
    for (ring = 1U; ring <= max_ring; ++ring) {
        result = set_ring(g, io, ring, true);
        if (result != DISCOVERY_OK) { discovery_gateway_abort(g, io, result); return result; }
    }
    g->state = DISCOVERY_STATE_VERIFY_NEW_RELAYS;
    if (!verify_all(g, io)) { discovery_gateway_abort(g, io, DISCOVERY_IO_ERROR); return DISCOVERY_IO_ERROR; }
    g->state = DISCOVERY_STATE_DISABLE_OLD_RELAYS;
    for (ring = max_ring; ring > 0U; --ring) {
        result = set_ring(g, io, ring, false);
        if (result != DISCOVERY_OK || !verify_all(g, io)) {
            discovery_gateway_abort(g, io, result != DISCOVERY_OK ? result : DISCOVERY_IO_ERROR);
            return result != DISCOVERY_OK ? result : DISCOVERY_IO_ERROR;
        }
    }
    g->state = DISCOVERY_STATE_VERIFY_NETWORK;
    if (!verify_all(g, io)) { discovery_gateway_abort(g, io, DISCOVERY_IO_ERROR); return DISCOVERY_IO_ERROR; }
    g->state = DISCOVERY_STATE_COMMIT;
    for (i = 0U; i < g->topology.node_count; ++i) {
        g->topology.nodes[i].currently_relay = g->transaction.applied_relay[i];
        if (g->transaction.applied_relay[i]) ++relay_count;
    }
    if (io->persist != NULL && io->persist(io->context, g) != 0) {
        discovery_gateway_abort(g, io, DISCOVERY_IO_ERROR); return DISCOVERY_IO_ERROR;
    }
    if (io->publish_commit != NULL && io->publish_commit(io->context, g->epoch,
                                                         relay_count, max_ring) != 0) {
        discovery_gateway_abort(g, io, DISCOVERY_IO_ERROR); return DISCOVERY_IO_ERROR;
    }
    g->state = DISCOVERY_STATE_COMPLETE;
    return DISCOVERY_OK;
}
