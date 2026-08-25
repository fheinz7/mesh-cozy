#include "mesh_discovery.h"

#include <string.h>

static bool elapsed(uint32_t now, uint32_t since, uint32_t duration)
{
    return (int32_t)(now - since) >= (int32_t)duration;
}

uint32_t discovery_calculate_takeover_delay_ms(uint8_t priority,
                                               uint16_t local_address)
{
    return (uint32_t)priority * 3000U + ((uint32_t)local_address * 37U) % 500U;
}

void discovery_relay_beacon_sender_init(discovery_relay_beacon_sender_t *s,
                                        uint16_t relay_address, uint16_t epoch,
                                        uint16_t generation, uint8_t priority,
                                        uint16_t period_ms, uint32_t now_ms)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->relay_address = relay_address;
    s->epoch = epoch;
    s->configuration_generation = generation;
    s->relay_priority = priority;
    s->beacon_period_ms = period_ms;
    s->last_beacon_time_ms = now_ms - period_ms; /* first call is immediately due */
}

bool discovery_relay_beacon_prepare(discovery_relay_beacon_sender_t *s,
                                    bool relay_enabled, uint32_t now_ms,
                                    discovery_relay_alive_beacon_t *beacon)
{
    if (s == NULL || beacon == NULL || s->beacon_period_ms == 0U ||
        !elapsed(now_ms, s->last_beacon_time_ms, s->beacon_period_ms)) return false;
    memset(beacon, 0, sizeof(*beacon));
    beacon->header.protocol_version = DISCOVERY_PROTOCOL_VERSION;
    beacon->header.message_type = MSG_RELAY_ALIVE_BEACON;
    beacon->header.epoch = s->epoch;
    beacon->header.sequence = (uint16_t)(s->beacon_sequence + 1U);
    s->beacon_sequence = beacon->header.sequence;
    beacon->relay_address = s->relay_address;
    beacon->configuration_generation = s->configuration_generation;
    beacon->beacon_sequence = s->beacon_sequence;
    beacon->relay_state = relay_enabled ? 1U : 0U;
    beacon->relay_priority = s->relay_priority;
    s->last_beacon_time_ms = now_ms;
    return true;
}

void discovery_relay_backup_init(discovery_relay_backup_monitor_t *m,
                                 uint16_t local_address, uint16_t epoch,
                                 const discovery_relay_watchdog_plan_t *plan,
                                 uint8_t priority, uint32_t now_ms)
{
    if (m == NULL || plan == NULL) return;
    memset(m, 0, sizeof(*m));
    m->local_address = local_address;
    m->primary_relay = plan->primary_relay;
    m->epoch = epoch;
    m->expected_generation = plan->configuration_generation;
    m->beacon_period_ms = plan->beacon_period_ms;
    m->missed_threshold = plan->missed_beacon_threshold;
    m->takeover_delay_ms = plan->takeover_delay_ms != 0U
                             ? plan->takeover_delay_ms
                             : (uint16_t)discovery_calculate_takeover_delay_ms(priority,
                                                                               local_address);
    m->recovery_stable_ms = plan->recovery_stable_ms;
    m->minimum_relay_active_ms = plan->minimum_relay_active_ms;
    m->backup_priority = priority;
    m->last_beacon_time_ms = now_ms;
    m->state_enter_time_ms = now_ms;
    m->state = RELAY_BACKUP_NORMAL;
}

bool discovery_relay_beacon_timed_out(const discovery_relay_backup_monitor_t *m,
                                      uint32_t now_ms)
{
    uint32_t timeout;
    if (m == NULL || m->beacon_period_ms == 0U || m->missed_threshold == 0U) return true;
    timeout = (uint32_t)m->beacon_period_ms * m->missed_threshold;
    return elapsed(now_ms, m->last_beacon_time_ms, timeout);
}

discovery_result_t discovery_relay_backup_on_beacon(
    discovery_relay_backup_monitor_t *m, uint16_t epoch,
    uint16_t source_address, uint16_t generation, uint16_t sequence,
    uint8_t received_ttl, bool primary_relay_enabled, int8_t rssi,
    int8_t minimum_rssi, uint32_t now_ms)
{
    if (m == NULL) return DISCOVERY_INVALID_ARGUMENT;
    if (epoch != m->epoch) return DISCOVERY_STALE_EPOCH;
    if (source_address != m->primary_relay || generation != m->expected_generation)
        return DISCOVERY_NOT_FOUND;
    if (received_ttl != 0U || rssi < minimum_rssi) return DISCOVERY_MALFORMED;
    if (m->received_beacon && (int16_t)(sequence - m->last_beacon_sequence) <= 0)
        return DISCOVERY_MALFORMED;
    if (m->state == RELAY_BACKUP_RECOVERY_MONITORING) {
        uint16_t delta = (uint16_t)(sequence - m->last_beacon_sequence);
        if ((uint32_t)m->recovery_expected_beacons + delta <= UINT16_MAX)
            m->recovery_expected_beacons = (uint16_t)(m->recovery_expected_beacons + delta);
        if (m->recovery_received_beacons < UINT16_MAX) ++m->recovery_received_beacons;
    }
    m->last_beacon_sequence = sequence;
    m->last_beacon_time_ms = now_ms;
    m->received_beacon = true;
    m->primary_relay_enabled = primary_relay_enabled;
    if (m->state == RELAY_BACKUP_SUSPECTING || m->state == RELAY_BACKUP_WAIT_TAKEOVER) {
        m->state = RELAY_BACKUP_NORMAL;
        m->state_enter_time_ms = now_ms;
    } else if (m->state == RELAY_BACKUP_ACTIVE && primary_relay_enabled) {
        m->recovery_start_time_ms = now_ms;
        m->recovery_received_beacons = 1U;
        m->recovery_expected_beacons = 1U;
        m->state = RELAY_BACKUP_RECOVERY_MONITORING;
        m->state_enter_time_ms = now_ms;
    }
    return DISCOVERY_OK;
}

bool discovery_relay_backup_should_promote(const discovery_relay_backup_monitor_t *m,
                                           uint32_t now_ms)
{
    return m != NULL && m->takeover_count >= DISCOVERY_RELAY_PROMOTION_TAKEOVERS &&
           !elapsed(now_ms, m->first_takeover_time_ms,
                    DISCOVERY_RELAY_PROMOTION_WINDOW_MS + 1U);
}

static void record_takeover(discovery_relay_backup_monitor_t *m, uint32_t now)
{
    if (m->takeover_count == 0U ||
        elapsed(now, m->first_takeover_time_ms, DISCOVERY_RELAY_PROMOTION_WINDOW_MS)) {
        m->first_takeover_time_ms = now;
        m->takeover_count = 1U;
    } else if (m->takeover_count < UINT8_MAX) {
        ++m->takeover_count;
    }
}

static void send_takeover_beacon(discovery_relay_backup_monitor_t *m, uint32_t now,
                                 const discovery_relay_backup_io_t *io)
{
    if (io->send_takeover_beacon == NULL) return;
    ++m->takeover_beacon_sequence;
    (void)io->send_takeover_beacon(io->context, m->primary_relay, m->local_address,
                                   m->expected_generation, m->takeover_beacon_sequence,
                                   m->backup_priority);
    m->last_takeover_beacon_time_ms = now;
}

discovery_result_t discovery_relay_backup_on_takeover_beacon(
    discovery_relay_backup_monitor_t *m, uint16_t epoch,
    uint16_t failed_relay, uint16_t replacement_relay, uint16_t generation,
    uint8_t replacement_priority, uint8_t received_ttl, uint32_t now_ms)
{
    if (m == NULL) return DISCOVERY_INVALID_ARGUMENT;
    if (epoch != m->epoch) return DISCOVERY_STALE_EPOCH;
    if (failed_relay != m->primary_relay || generation != m->expected_generation ||
        replacement_relay == m->local_address) return DISCOVERY_NOT_FOUND;
    if (received_ttl != 0U) return DISCOVERY_MALFORMED;
    if (replacement_priority >= m->backup_priority) return DISCOVERY_OK;
    if (m->state == RELAY_BACKUP_SUSPECTING || m->state == RELAY_BACKUP_WAIT_TAKEOVER ||
        m->state == RELAY_BACKUP_NORMAL) {
        m->last_beacon_time_ms = now_ms; /* replacement is now the local liveness source */
        m->state = RELAY_BACKUP_NORMAL;
        m->state_enter_time_ms = now_ms;
    }
    return DISCOVERY_OK;
}

static discovery_result_t activate(discovery_relay_backup_monitor_t *m,
                                   uint32_t now, const discovery_relay_backup_io_t *io)
{
    uint32_t age = now - m->last_beacon_time_ms;
    if (!m->local_relay_enabled) {
        if (io->set_local_relay(io->context, true) != 0) return DISCOVERY_IO_ERROR;
        m->local_relay_enabled = true;
    }
    m->relay_activation_time_ms = now;
    record_takeover(m, now);
    m->state = RELAY_BACKUP_ACTIVE;
    m->state_enter_time_ms = now;
    if (io->send_takeover_event != NULL)
        (void)io->send_takeover_event(io->context, m->primary_relay, m->local_address,
                                      m->expected_generation, m->backup_priority, age);
    send_takeover_beacon(m, now, io);
    return DISCOVERY_OK;
}

static discovery_result_t deactivate(discovery_relay_backup_monitor_t *m,
                                     uint32_t now, const discovery_relay_backup_io_t *io)
{
    if (io->set_local_relay(io->context, false) != 0) return DISCOVERY_IO_ERROR;
    m->local_relay_enabled = false;
    m->state = RELAY_BACKUP_NORMAL;
    m->state_enter_time_ms = now;
    m->recovery_start_time_ms = 0U;
    if (io->send_restored_event != NULL)
        (void)io->send_restored_event(io->context, m->primary_relay, m->local_address,
                                      m->expected_generation);
    return DISCOVERY_OK;
}

discovery_result_t discovery_relay_backup_process(
    discovery_relay_backup_monitor_t *m, uint32_t now,
    const discovery_relay_backup_io_t *io)
{
    bool timeout;
    if (m == NULL || io == NULL || io->set_local_relay == NULL)
        return DISCOVERY_INVALID_ARGUMENT;
    timeout = discovery_relay_beacon_timed_out(m, now);
    if (m->local_relay_enabled &&
        elapsed(now, m->last_takeover_beacon_time_ms, m->beacon_period_ms))
        send_takeover_beacon(m, now, io);
    switch (m->state) {
    case RELAY_BACKUP_NORMAL:
        if (timeout) { m->state = RELAY_BACKUP_SUSPECTING; m->state_enter_time_ms = now; }
        break;
    case RELAY_BACKUP_SUSPECTING:
        if (!timeout) m->state = RELAY_BACKUP_NORMAL;
        else { m->state = RELAY_BACKUP_WAIT_TAKEOVER; m->state_enter_time_ms = now; }
        break;
    case RELAY_BACKUP_WAIT_TAKEOVER:
        if (!timeout) m->state = RELAY_BACKUP_NORMAL;
        else if (elapsed(now, m->state_enter_time_ms, m->takeover_delay_ms))
            return activate(m, now, io);
        break;
    case RELAY_BACKUP_ACTIVE:
        if (!timeout && m->primary_relay_enabled) {
            m->recovery_start_time_ms = now;
            m->state = RELAY_BACKUP_RECOVERY_MONITORING;
            m->state_enter_time_ms = now;
        }
        break;
    case RELAY_BACKUP_RECOVERY_MONITORING:
        if (timeout || !m->primary_relay_enabled) {
            m->state = RELAY_BACKUP_ACTIVE; m->recovery_start_time_ms = 0U;
            record_takeover(m, now);
        } else if (m->recovery_expected_beacons != 0U &&
                   (uint32_t)m->recovery_received_beacons * 1000U /
                       m->recovery_expected_beacons >= 800U &&
                   elapsed(now, m->recovery_start_time_ms, m->recovery_stable_ms) &&
                   elapsed(now, m->relay_activation_time_ms, m->minimum_relay_active_ms)) {
            m->state = RELAY_BACKUP_RETURNING_NORMAL; m->state_enter_time_ms = now;
        }
        break;
    case RELAY_BACKUP_RETURNING_NORMAL:
        if (timeout || !m->primary_relay_enabled) {
            m->state = RELAY_BACKUP_ACTIVE;
            record_takeover(m, now);
        }
        else if (elapsed(now, m->state_enter_time_ms, DISCOVERY_RELAY_HANDOVER_OVERLAP_MS))
            return deactivate(m, now, io);
        break;
    default:
        m->state = RELAY_BACKUP_NORMAL; m->state_enter_time_ms = now; break;
    }
    return DISCOVERY_OK;
}
