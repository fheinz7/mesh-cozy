#include "mesh_discovery.h"

#include <limits.h>
#include <string.h>

void discovery_topology_init(discovery_topology_t *topology)
{
    if (topology != NULL) memset(topology, 0, sizeof(*topology));
}

size_t discovery_topology_find_node(const discovery_topology_t *topology,
                                    uint16_t address)
{
    size_t i;
    if (topology == NULL) return DISCOVERY_INVALID_INDEX;
    for (i = 0U; i < topology->node_count; ++i)
        if (topology->nodes[i].address == address) return i;
    return DISCOVERY_INVALID_INDEX;
}

discovery_result_t discovery_topology_add_node(discovery_topology_t *topology,
                                               const discovery_topology_node_t *node,
                                               size_t *index)
{
    size_t found;
    if (topology == NULL || node == NULL || node->address == 0U)
        return DISCOVERY_INVALID_ARGUMENT;
    found = discovery_topology_find_node(topology, node->address);
    if (found != DISCOVERY_INVALID_INDEX) {
        if (index != NULL) *index = found;
        return DISCOVERY_OK;
    }
    if (topology->node_count >= DISCOVERY_MAX_NODES) return DISCOVERY_CAPACITY;
    topology->nodes[topology->node_count] = *node;
    topology->nodes[topology->node_count].calculated_ring = DISCOVERY_UNKNOWN_RING;
    if (index != NULL) *index = topology->node_count;
    ++topology->node_count;
    return DISCOVERY_OK;
}

discovery_result_t discovery_topology_add_report(discovery_topology_t *topology,
                                                 uint16_t reporter,
                                                 const discovery_report_entry_t *entries,
                                                 size_t count)
{
    size_t receiver, i;
    if (topology == NULL || (entries == NULL && count != 0U))
        return DISCOVERY_INVALID_ARGUMENT;
    receiver = discovery_topology_find_node(topology, reporter);
    if (receiver == DISCOVERY_INVALID_INDEX) return DISCOVERY_NOT_FOUND;
    for (i = 0U; i < count; ++i) {
        size_t transmitter = discovery_topology_find_node(topology, entries[i].neighbor_address);
        discovery_directed_link_t *link;
        if (transmitter == DISCOVERY_INVALID_INDEX) continue;
        link = &topology->links[transmitter][receiver]; /* transmitter -> reporter */
        link->measured = true;
        link->pdr_per_mille = entries[i].pdr_per_mille > 1000U
                                ? 1000U : entries[i].pdr_per_mille;
        link->average_rssi = entries[i].average_rssi;
        link->minimum_rssi = entries[i].minimum_rssi;
        link->maximum_rssi = entries[i].maximum_rssi;
    }
    return DISCOVERY_OK;
}

bool discovery_link_is_valid(const discovery_topology_t *topology, size_t a,
                             size_t b, const discovery_configuration_t *config)
{
    const discovery_directed_link_t *ab, *ba;
    if (topology == NULL || config == NULL || a >= topology->node_count ||
        b >= topology->node_count || a == b) return false;
    ab = &topology->links[a][b];
    ba = &topology->links[b][a];
    return ab->measured && ba->measured &&
           ab->pdr_per_mille >= config->minimum_pdr_per_mille &&
           ba->pdr_per_mille >= config->minimum_pdr_per_mille &&
           ab->average_rssi >= config->minimum_rssi_dbm &&
           ba->average_rssi >= config->minimum_rssi_dbm;
}

bool discovery_link_is_marginal(const discovery_topology_t *topology, size_t a,
                                size_t b)
{
    const discovery_directed_link_t *ab, *ba;
    if (topology == NULL || a >= topology->node_count || b >= topology->node_count || a == b)
        return false;
    ab = &topology->links[a][b]; ba = &topology->links[b][a];
    return ab->measured && ba->measured && ab->pdr_per_mille >= 600U &&
           ba->pdr_per_mille >= 600U && ab->average_rssi >= -95 &&
           ba->average_rssi >= -95;
}

discovery_result_t discovery_calculate_rings(discovery_topology_t *topology,
                                             size_t gateway_index,
                                             const discovery_configuration_t *config)
{
    size_t queue[DISCOVERY_MAX_NODES], read = 0U, write = 0U, i;
    if (topology == NULL || config == NULL || gateway_index >= topology->node_count)
        return DISCOVERY_INVALID_ARGUMENT;
    for (i = 0U; i < topology->node_count; ++i) {
        topology->nodes[i].calculated_ring = DISCOVERY_UNKNOWN_RING;
        topology->nodes[i].ring_mismatch = false;
    }
    topology->nodes[gateway_index].calculated_ring = 0U;
    queue[write++] = gateway_index;
    while (read < write) {
        size_t current = queue[read++], candidate;
        for (candidate = 0U; candidate < topology->node_count; ++candidate) {
            if (topology->nodes[candidate].calculated_ring != DISCOVERY_UNKNOWN_RING ||
                !discovery_link_is_valid(topology, current, candidate, config)) continue;
            topology->nodes[candidate].calculated_ring =
                (uint8_t)(topology->nodes[current].calculated_ring + 1U);
            queue[write++] = candidate;
        }
    }
    for (i = 0U; i < topology->node_count; ++i) {
        discovery_topology_node_t *node = &topology->nodes[i];
        if (node->calculated_ring == DISCOVERY_UNKNOWN_RING) return DISCOVERY_DISCONNECTED;
        if (i != gateway_index && node->preliminary_ring != DISCOVERY_UNKNOWN_RING &&
            node->preliminary_ring != node->calculated_ring) node->ring_mismatch = true;
    }
    return DISCOVERY_OK;
}

static bool candidate_connected(const discovery_topology_t *t, size_t candidate,
                                const discovery_configuration_t *c)
{
    size_t i;
    uint8_t ring = t->nodes[candidate].calculated_ring;
    for (i = 0U; i < t->node_count; ++i) {
        if (!t->nodes[i].selected_relay) continue;
        if (t->nodes[i].calculated_ring >= ring) continue;
        if (discovery_link_is_valid(t, i, candidate, c)) return true;
    }
    return false;
}

static int candidate_score(const discovery_topology_t *t, size_t candidate,
                           const discovery_configuration_t *c,
                           const uint8_t coverage[DISCOVERY_MAX_NODES])
{
    size_t i;
    int score = 0, links = 0, rssi_total = 0, shared = 0;
    uint8_t ring = t->nodes[candidate].calculated_ring;
    if (t->nodes[candidate].role == NODE_ROLE_DEDICATED_REPEATER) score += 300;
    else if (t->nodes[candidate].role == NODE_ROLE_RELAY_CANDIDATE) score += 150;
    if (t->nodes[candidate].energy_restricted) score -= 500;
    if (t->nodes[candidate].unstable) score -= 1000;
    for (i = 0U; i < t->node_count; ++i) {
        const discovery_directed_link_t *ab, *ba;
        if (!discovery_link_is_valid(t, candidate, i, c)) continue;
        ab = &t->links[candidate][i]; ba = &t->links[i][candidate];
        ++links; rssi_total += ab->average_rssi < ba->average_rssi
                                ? ab->average_rssi : ba->average_rssi;
        if (t->nodes[i].calculated_ring < ring) score += 50;
        if (coverage[i] < c->required_relay_coverage &&
            t->nodes[i].calculated_ring > 1U) {
            score += t->nodes[i].calculated_ring > ring ? 100 : 30;
            if (coverage[i] != 0U) ++shared;
        }
        score += (int)((ab->pdr_per_mille < ba->pdr_per_mille
                         ? ab->pdr_per_mille : ba->pdr_per_mille) / 10U);
    }
    if (links != 0) score += ((rssi_total / links) + 128) * 2;
    return score - shared * 20;
}

static bool unmet_exists(const discovery_topology_t *t,
                         const discovery_configuration_t *c,
                         const uint8_t coverage[DISCOVERY_MAX_NODES])
{
    size_t i;
    for (i = 0U; i < t->node_count; ++i)
        if (t->nodes[i].calculated_ring > 1U &&
            coverage[i] < c->required_relay_coverage) return true;
    return false;
}

discovery_result_t discovery_select_relays(discovery_topology_t *topology,
                                           size_t gateway_index,
                                           const discovery_configuration_t *config,
                                           uint8_t coverage[DISCOVERY_MAX_NODES],
                                           bool degraded[DISCOVERY_MAX_NODES])
{
    uint8_t ring_counts[UINT8_MAX + 1U] = {0};
    size_t i;
    if (topology == NULL || config == NULL || coverage == NULL || degraded == NULL ||
        gateway_index >= topology->node_count || config->required_relay_coverage == 0U)
        return DISCOVERY_INVALID_ARGUMENT;
    memset(coverage, 0, DISCOVERY_MAX_NODES);
    memset(degraded, 0, DISCOVERY_MAX_NODES * sizeof(bool));
    for (i = 0U; i < topology->node_count; ++i) topology->nodes[i].selected_relay = false;
    topology->nodes[gateway_index].selected_relay = true;
    while (unmet_exists(topology, config, coverage)) {
        size_t best = DISCOVERY_INVALID_INDEX;
        int best_score = INT_MIN;
        for (i = 0U; i < topology->node_count; ++i) {
            const discovery_topology_node_t *n = &topology->nodes[i];
            int score;
            if (n->selected_relay || !n->relay_capable || n->unstable ||
                n->calculated_ring == 0U || n->calculated_ring == DISCOVERY_UNKNOWN_RING ||
                !candidate_connected(topology, i, config)) continue;
            if (config->maximum_relays_per_ring != 0U &&
                ring_counts[n->calculated_ring] >= config->maximum_relays_per_ring) continue;
            score = candidate_score(topology, i, config, coverage);
            if (score > best_score || (score == best_score && best != DISCOVERY_INVALID_INDEX &&
                                       n->address < topology->nodes[best].address)) {
                best = i; best_score = score;
            }
        }
        if (best == DISCOVERY_INVALID_INDEX || best_score <= 0) break;
        topology->nodes[best].selected_relay = true;
        ++ring_counts[topology->nodes[best].calculated_ring];
        for (i = 0U; i < topology->node_count; ++i) {
            if (topology->nodes[i].calculated_ring > 1U &&
                discovery_link_is_valid(topology, best, i, config) && coverage[i] < UINT8_MAX)
                ++coverage[i];
        }
    }
    for (i = 0U; i < topology->node_count; ++i) {
        if (topology->nodes[i].calculated_ring <= 1U) continue;
        if (coverage[i] < config->required_relay_coverage) degraded[i] = true;
        if (coverage[i] == 0U) return DISCOVERY_NO_CANDIDATE;
    }
    return discovery_relay_set_is_connected(topology, gateway_index, config)
             ? DISCOVERY_OK : DISCOVERY_DISCONNECTED;
}

bool discovery_relay_set_is_connected(const discovery_topology_t *topology,
                                      size_t gateway_index,
                                      const discovery_configuration_t *config)
{
    bool seen[DISCOVERY_MAX_NODES] = {false};
    size_t queue[DISCOVERY_MAX_NODES], read = 0U, write = 0U, i;
    if (topology == NULL || config == NULL || gateway_index >= topology->node_count)
        return false;
    seen[gateway_index] = true; queue[write++] = gateway_index;
    while (read < write) {
        size_t current = queue[read++];
        for (i = 0U; i < topology->node_count; ++i) {
            if (seen[i] || !topology->nodes[i].selected_relay) continue;
            if (discovery_link_is_valid(topology, current, i, config)) {
                seen[i] = true; queue[write++] = i;
            }
        }
    }
    for (i = 0U; i < topology->node_count; ++i)
        if (topology->nodes[i].selected_relay && !seen[i]) return false;
    return true;
}

discovery_result_t discovery_simulate_relay_failure(
    const discovery_topology_t *t, size_t gateway, size_t primary, size_t backup,
    const discovery_configuration_t *c, discovery_relay_failure_simulation_t *out)
{
    uint8_t distance[DISCOVERY_MAX_NODES];
    size_t queue[DISCOVERY_MAX_NODES], read = 0U, write = 0U, i;
    uint16_t affected = 0U;
    uint8_t maximum = 0U;
    if (t == NULL || c == NULL || out == NULL || gateway >= t->node_count ||
        primary >= t->node_count || backup >= t->node_count || primary == gateway)
        return DISCOVERY_INVALID_ARGUMENT;
    memset(distance, DISCOVERY_UNKNOWN_RING, sizeof(distance));
    distance[gateway] = 0U; queue[write++] = gateway;
    while (read < write) {
        size_t current = queue[read++];
        bool expands = current == gateway ||
            ((t->nodes[current].selected_relay || current == backup) && current != primary);
        if (!expands) continue;
        for (i = 0U; i < t->node_count; ++i) {
            if (i == primary || distance[i] != DISCOVERY_UNKNOWN_RING ||
                !discovery_link_is_valid(t, current, i, c)) continue;
            distance[i] = (uint8_t)(distance[current] + 1U);
            queue[write++] = i;
        }
    }
    for (i = 0U; i < t->node_count; ++i) {
        if (i == primary) continue;
        if (distance[i] == DISCOVERY_UNKNOWN_RING || distance[i] > c->maximum_ttl) ++affected;
        else if (distance[i] > maximum) maximum = distance[i];
    }
    out->primary_relay = t->nodes[primary].address;
    out->backup_relay = t->nodes[backup].address;
    out->affected_nodes = affected;
    out->resulting_maximum_ring = maximum;
    out->connectivity_preserved = affected == 0U;
    return DISCOVERY_OK;
}

static int backup_score(const discovery_topology_t *t, size_t primary, size_t candidate,
                        const discovery_configuration_t *c,
                        const uint8_t load[DISCOVERY_MAX_NODES])
{
    size_t i;
    int shared = 0, backward = 0, forward = 0, overlap = 0;
    const discovery_directed_link_t *pc = &t->links[primary][candidate];
    const discovery_directed_link_t *cp = &t->links[candidate][primary];
    int pdr = pc->pdr_per_mille < cp->pdr_per_mille ? pc->pdr_per_mille : cp->pdr_per_mille;
    int rssi = pc->average_rssi < cp->average_rssi ? pc->average_rssi : cp->average_rssi;
    for (i = 0U; i < t->node_count; ++i) {
        if (!discovery_link_is_valid(t, candidate, i, c)) continue;
        if (t->nodes[i].calculated_ring < t->nodes[candidate].calculated_ring &&
            t->nodes[i].selected_relay) ++backward;
        if (t->nodes[i].calculated_ring > t->nodes[candidate].calculated_ring) ++forward;
        if (discovery_link_is_valid(t, primary, i, c)) ++shared;
        if (t->nodes[i].selected_relay &&
            t->nodes[i].calculated_ring == t->nodes[candidate].calculated_ring) ++overlap;
    }
    return shared * 100 + backward * 80 + forward * 80 + pdr / 10 +
           (rssi + 128) * 2 +
           (t->nodes[candidate].role == NODE_ROLE_DEDICATED_REPEATER ? 300 :
            t->nodes[candidate].role == NODE_ROLE_RELAY_CANDIDATE ? 150 : 0) -
           load[candidate] * 50 - overlap * 20;
}

static bool has_backward_relay(const discovery_topology_t *t, size_t candidate,
                               size_t failed, const discovery_configuration_t *c)
{
    size_t i;
    for (i = 0U; i < t->node_count; ++i) {
        if (i == failed || !t->nodes[i].selected_relay ||
            t->nodes[i].calculated_ring >= t->nodes[candidate].calculated_ring) continue;
        if (discovery_link_is_valid(t, candidate, i, c)) return true;
    }
    return false;
}

discovery_result_t discovery_select_relay_backups(
    const discovery_topology_t *t, size_t gateway,
    const discovery_configuration_t *c, uint16_t generation,
    discovery_relay_watchdog_plan_t plans[DISCOVERY_MAX_NODES], size_t *plan_count,
    discovery_relay_failure_simulation_t simulations[DISCOVERY_MAX_NODES])
{
    uint8_t load[DISCOVERY_MAX_NODES] = {0};
    size_t primary, count = 0U;
    bool incomplete = false;
    if (t == NULL || c == NULL || plans == NULL || plan_count == NULL ||
        simulations == NULL || gateway >= t->node_count)
        return DISCOVERY_INVALID_ARGUMENT;
    memset(plans, 0, sizeof(*plans) * DISCOVERY_MAX_NODES);
    memset(simulations, 0, sizeof(*simulations) * DISCOVERY_MAX_NODES);
    for (primary = 0U; primary < t->node_count; ++primary) {
        discovery_relay_watchdog_plan_t *plan;
        size_t choice;
        bool used[DISCOVERY_MAX_NODES] = {false};
        if (primary == gateway || !t->nodes[primary].selected_relay) continue;
        plan = &plans[count];
        plan->primary_relay = t->nodes[primary].address;
        plan->configuration_generation = generation;
        plan->beacon_period_ms = DISCOVERY_RELAY_BEACON_PERIOD_MS;
        plan->missed_beacon_threshold = DISCOVERY_RELAY_MISSED_BEACON_THRESHOLD;
        plan->recovery_stable_ms = DISCOVERY_RELAY_RECOVERY_STABLE_MS;
        plan->minimum_relay_active_ms = DISCOVERY_RELAY_MINIMUM_ACTIVE_MS;
        for (choice = 0U; choice < DISCOVERY_MAX_RELAY_BACKUPS; ++choice) {
            size_t candidate, best = DISCOVERY_INVALID_INDEX;
            int best_score = INT_MIN;
            discovery_relay_failure_simulation_t best_sim;
            memset(&best_sim, 0, sizeof(best_sim));
            for (candidate = 0U; candidate < t->node_count; ++candidate) {
                discovery_relay_failure_simulation_t sim;
                int score;
                if (used[candidate] || candidate == gateway || candidate == primary ||
                    t->nodes[candidate].selected_relay || !t->nodes[candidate].relay_capable ||
                    t->nodes[candidate].unstable || t->nodes[candidate].energy_restricted ||
                    !discovery_link_is_valid(t, primary, candidate, c) ||
                    !has_backward_relay(t, candidate, primary, c)) continue;
                if (discovery_simulate_relay_failure(t, gateway, primary, candidate, c,
                                                     &sim) != DISCOVERY_OK ||
                    !sim.connectivity_preserved) continue;
                score = backup_score(t, primary, candidate, c, load);
                if (score > best_score || (score == best_score && best != DISCOVERY_INVALID_INDEX &&
                                           t->nodes[candidate].address < t->nodes[best].address)) {
                    best = candidate; best_score = score; best_sim = sim;
                }
            }
            if (best == DISCOVERY_INVALID_INDEX) break;
            used[best] = true; ++load[best];
            plan->backup_nodes[plan->backup_count++] = t->nodes[best].address;
            if (choice == 0U) simulations[count] = best_sim;
        }
        if (plan->backup_count == 0U) incomplete = true;
        ++count;
    }
    *plan_count = count;
    return incomplete ? DISCOVERY_NO_CANDIDATE : DISCOVERY_OK;
}
