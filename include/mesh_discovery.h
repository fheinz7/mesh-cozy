#ifndef MESH_DISCOVERY_H
#define MESH_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISCOVERY_PROTOCOL_VERSION 1U
#define DISCOVERY_MAX_NODES 128U
#define DISCOVERY_MAX_NEIGHBORS 24U
#define DISCOVERY_MAX_PROBES 16U
#define DISCOVERY_MAX_REPORT_FRAGMENTS 32U
#define DISCOVERY_MAX_RELAY_BACKUPS 2U
#define DISCOVERY_RELAY_BEACON_PERIOD_MS 2000U
#define DISCOVERY_RELAY_MISSED_BEACON_THRESHOLD 3U
#define DISCOVERY_RELAY_RECOVERY_STABLE_MS 10000U
#define DISCOVERY_RELAY_MINIMUM_ACTIVE_MS 15000U
#define DISCOVERY_RELAY_HANDOVER_OVERLAP_MS 3000U
#define DISCOVERY_RELAY_PROMOTION_WINDOW_MS 1800000UL
#define DISCOVERY_RELAY_PROMOTION_TAKEOVERS 3U
#define DISCOVERY_UNKNOWN_RING UINT8_MAX
#define DISCOVERY_INVALID_INDEX ((size_t)-1)
#define DISCOVERY_GATEWAY_GROUP 0xC100U
#define DISCOVERY_NEIGHBOR_GROUP 0xC101U
#define DISCOVERY_STATUS_GROUP 0xC102U
#define DISCOVERY_RELAY_GROUP 0xC103U

typedef enum {
    DISCOVERY_OK = 0,
    DISCOVERY_INVALID_ARGUMENT = -1,
    DISCOVERY_CAPACITY = -2,
    DISCOVERY_NOT_FOUND = -3,
    DISCOVERY_STALE_EPOCH = -4,
    DISCOVERY_MALFORMED = -5,
    DISCOVERY_DISCONNECTED = -6,
    DISCOVERY_NO_CANDIDATE = -7,
    DISCOVERY_WRONG_STATE = -8,
    DISCOVERY_IO_ERROR = -9
} discovery_result_t;

typedef enum {
    MSG_DISCOVERY_ENTER = 0x01, MSG_DISCOVERY_READY, MSG_RING_PROBE,
    MSG_RING_RESPONSE, MSG_SLOT_ASSIGN, MSG_NEIGHBOR_PROBE,
    MSG_PROBE_PHASE_COMPLETE, MSG_REPORT_REQUEST, MSG_NEIGHBOR_REPORT,
    MSG_REPORT_ACK, MSG_RELAY_PLAN, MSG_VERIFY_REQUEST,
    MSG_VERIFY_RESPONSE, MSG_DISCOVERY_COMMIT, MSG_DISCOVERY_ABORT,
    MSG_RELAY_ALIVE_BEACON, MSG_RELAY_TAKEOVER_BEACON,
    MSG_RELAY_TAKEOVER_EVENT, MSG_RELAY_RESTORED_EVENT
} discovery_message_type_t;

typedef enum {
    NODE_ROLE_COMMON = 0,
    NODE_ROLE_RELAY_CANDIDATE,
    NODE_ROLE_DEDICATED_REPEATER,
    NODE_ROLE_GATEWAY
} discovery_node_role_t;

typedef enum {
    DISCOVERY_STATE_IDLE = 0, DISCOVERY_STATE_ENTER, DISCOVERY_STATE_WAIT_READY,
    DISCOVERY_STATE_RING_DISCOVERY, DISCOVERY_STATE_LOCAL_DISCOVERY,
    DISCOVERY_STATE_COLLECT_REPORTS, DISCOVERY_STATE_BUILD_GRAPH,
    DISCOVERY_STATE_CALCULATE_RINGS, DISCOVERY_STATE_SELECT_RELAYS,
    DISCOVERY_STATE_ENABLE_NEW_RELAYS, DISCOVERY_STATE_VERIFY_NEW_RELAYS,
    DISCOVERY_STATE_DISABLE_OLD_RELAYS, DISCOVERY_STATE_VERIFY_NETWORK,
    DISCOVERY_STATE_COMMIT, DISCOVERY_STATE_ROLLBACK, DISCOVERY_STATE_COMPLETE
} discovery_state_t;

typedef enum {
    DISCOVERY_HEALTHY = 0, DISCOVERY_DEGRADED, DISCOVERY_FAILED
} discovery_health_t;

typedef struct {
    uint8_t protocol_version;
    uint8_t message_type;
    uint16_t epoch;
    uint16_t sequence;
} discovery_header_t;

typedef struct { discovery_header_t header; uint32_t start_delay_ms; uint16_t expected_nodes;
    uint16_t slot_duration_ms; uint8_t probes_per_node; uint8_t maximum_ttl; } discovery_enter_t;
typedef struct { discovery_header_t header; uint16_t node_address; uint8_t relay_supported;
    uint8_t current_relay_state; uint8_t node_role; } discovery_ready_t;
typedef struct { discovery_header_t header; uint16_t gateway_address; uint8_t requested_ttl; }
    discovery_ring_probe_t;
typedef struct { discovery_header_t header; uint16_t node_address; uint8_t first_observed_ttl;
    int8_t received_rssi; } discovery_ring_response_t;
typedef struct { discovery_header_t header; uint16_t node_address; uint16_t slot_index;
    uint32_t phase_start_time_ms; uint16_t slot_duration_ms; uint8_t probe_count; }
    discovery_slot_assign_t;
typedef struct { discovery_header_t header; uint16_t source_address; uint8_t probe_index;
    uint8_t probe_count; } discovery_neighbor_probe_t;
typedef struct { discovery_header_t header; uint16_t target_address; uint8_t attempt; }
    discovery_verify_request_t;
typedef struct { discovery_header_t header; uint16_t node_address; uint8_t current_relay_state;
    uint8_t calculated_ring; } discovery_verify_response_t;
typedef struct { discovery_header_t header; uint16_t relay_count; uint8_t maximum_ring; }
    discovery_commit_t;
typedef struct { discovery_header_t header; uint16_t relay_address;
    uint16_t configuration_generation; uint16_t beacon_sequence;
    uint8_t relay_state; uint8_t relay_priority; } discovery_relay_alive_beacon_t;
typedef struct { discovery_header_t header; uint16_t failed_relay;
    uint16_t replacement_relay; uint16_t configuration_generation;
    uint16_t beacon_sequence; uint8_t backup_priority; } discovery_relay_takeover_beacon_t;
typedef struct { discovery_header_t header; uint16_t failed_relay;
    uint16_t replacement_relay; uint32_t last_primary_beacon_age_ms;
    uint8_t backup_priority; uint8_t local_relay_state; } discovery_relay_takeover_event_t;
typedef struct { uint32_t relayed_messages; uint32_t received_network_pdus;
    uint16_t buffer_failures; uint16_t uptime_seconds; } discovery_relay_health_counters_t;

typedef struct { uint16_t epoch; bool ring_response_sent; uint8_t first_observed_ttl; }
    discovery_node_ring_state_t;
typedef struct { uint8_t message_type; uint16_t source; uint16_t epoch; uint16_t sequence;
    uint8_t fragment_index; } discovery_duplicate_key_t;
typedef struct { uint16_t last_committed_epoch; uint8_t committed_relay_state;
    uint8_t last_known_ring; } discovery_persistent_node_state_t;
typedef struct { uint16_t epoch; uint16_t node_address; uint8_t ring; uint8_t relay_selected;
    uint16_t last_pdr; int8_t last_rssi; uint32_t last_seen_timestamp; }
    discovery_persistent_topology_entry_t;
typedef struct { uint16_t primary_relay; uint16_t backup_relays[2]; uint8_t backup_count; }
    discovery_relay_backup_plan_t;

typedef struct {
    uint16_t primary_relay;
    uint16_t backup_nodes[DISCOVERY_MAX_RELAY_BACKUPS];
    uint8_t backup_count;
    uint16_t configuration_generation;
    uint16_t beacon_period_ms;
    uint8_t missed_beacon_threshold;
    uint16_t takeover_delay_ms;
    uint16_t recovery_stable_ms;
    uint16_t minimum_relay_active_ms;
} discovery_relay_watchdog_plan_t;

typedef struct {
    uint16_t primary_relay;
    uint16_t backup_relay;
    bool connectivity_preserved;
    uint16_t affected_nodes;
    uint8_t resulting_maximum_ring;
} discovery_relay_failure_simulation_t;

typedef enum {
    RELAY_BACKUP_NORMAL = 0, RELAY_BACKUP_SUSPECTING,
    RELAY_BACKUP_WAIT_TAKEOVER, RELAY_BACKUP_ACTIVE,
    RELAY_BACKUP_RECOVERY_MONITORING, RELAY_BACKUP_RETURNING_NORMAL
} discovery_relay_backup_state_t;

typedef struct {
    uint16_t local_address;
    uint16_t primary_relay;
    uint16_t epoch;
    uint16_t expected_generation;
    uint16_t last_beacon_sequence;
    uint32_t last_beacon_time_ms;
    uint32_t state_enter_time_ms;
    uint32_t relay_activation_time_ms;
    uint32_t recovery_start_time_ms;
    uint16_t beacon_period_ms;
    uint16_t takeover_delay_ms;
    uint16_t recovery_stable_ms;
    uint16_t minimum_relay_active_ms;
    uint8_t missed_threshold;
    uint8_t backup_priority;
    bool local_relay_enabled;
    bool received_beacon;
    bool primary_relay_enabled;
    uint16_t recovery_received_beacons;
    uint16_t recovery_expected_beacons;
    uint16_t takeover_beacon_sequence;
    uint32_t last_takeover_beacon_time_ms;
    uint32_t first_takeover_time_ms;
    uint8_t takeover_count;
    discovery_relay_backup_state_t state;
} discovery_relay_backup_monitor_t;

typedef struct {
    uint16_t relay_address;
    uint16_t epoch;
    uint16_t configuration_generation;
    uint16_t beacon_sequence;
    uint16_t beacon_period_ms;
    uint32_t last_beacon_time_ms;
    uint8_t relay_priority;
} discovery_relay_beacon_sender_t;

typedef struct {
    void *context;
    int (*set_local_relay)(void *context, bool enabled);
    int (*send_takeover_beacon)(void *context, uint16_t primary, uint16_t replacement,
                                uint16_t generation, uint16_t sequence,
                                uint8_t priority);
    int (*send_takeover_event)(void *context, uint16_t primary, uint16_t replacement,
                               uint16_t generation, uint8_t priority,
                               uint32_t beacon_age_ms);
    int (*send_restored_event)(void *context, uint16_t primary, uint16_t replacement,
                               uint16_t generation);
} discovery_relay_backup_io_t;

typedef struct {
    uint8_t maximum_ttl;
    uint8_t probes_per_node;
    uint16_t probe_slot_ms;
    uint16_t probe_interval_ms;
    uint16_t minimum_pdr_per_mille;
    int8_t minimum_rssi_dbm;
    uint8_t required_relay_coverage;
    uint8_t maximum_relays_per_ring; /* 0 means unlimited */
    uint8_t verify_attempts;
    uint8_t minimum_verify_successes;
    uint8_t maximum_report_retries;
    uint16_t report_timeout_ms;
    uint8_t relay_retransmit_count;
    uint16_t relay_retransmit_interval_ms;
} discovery_configuration_t;

extern const discovery_configuration_t discovery_default_configuration;

typedef struct {
    uint16_t address;
    uint16_t received_bitmap;
    uint8_t received_count;
    int16_t rssi_sum;
    int8_t rssi_min;
    int8_t rssi_max;
    bool valid;
} discovery_neighbor_entry_t;

typedef struct {
    uint16_t owner_address;
    uint16_t epoch;
    discovery_neighbor_entry_t entries[DISCOVERY_MAX_NEIGHBORS];
    uint8_t entry_count;
} discovery_neighbor_table_t;

typedef struct {
    uint16_t neighbor_address;
    uint16_t pdr_per_mille;
    int8_t average_rssi;
    int8_t minimum_rssi;
    int8_t maximum_rssi;
    uint8_t received_probes;
} discovery_report_entry_t;

typedef struct {
    bool measured;
    uint16_t pdr_per_mille;
    int8_t average_rssi;
    int8_t minimum_rssi;
    int8_t maximum_rssi;
} discovery_directed_link_t;

typedef struct {
    uint16_t address;
    uint8_t role;
    bool relay_capable;
    bool currently_relay;
    bool selected_relay;
    bool unstable;
    bool energy_restricted;
    bool ring_mismatch;
    uint8_t preliminary_ring;
    uint8_t calculated_ring;
} discovery_topology_node_t;

typedef struct {
    discovery_topology_node_t nodes[DISCOVERY_MAX_NODES];
    discovery_directed_link_t links[DISCOVERY_MAX_NODES][DISCOVERY_MAX_NODES];
    size_t node_count;
} discovery_topology_t;

typedef struct {
    bool previous_relay[DISCOVERY_MAX_NODES];
    bool planned_relay[DISCOVERY_MAX_NODES];
    bool applied_relay[DISCOVERY_MAX_NODES];
} discovery_relay_transaction_t;

typedef struct {
    uint16_t reporter_address;
    uint16_t epoch;
    uint32_t received_fragments;
    uint8_t expected_fragments;
    bool complete;
} discovery_report_reassembly_t;

typedef struct {
    discovery_state_t state;
    uint16_t epoch;
    uint16_t sequence;
    size_t gateway_index;
    discovery_configuration_t configuration;
    discovery_topology_t topology;
    discovery_relay_transaction_t transaction;
    uint8_t coverage[DISCOVERY_MAX_NODES];
    bool absent[DISCOVERY_MAX_NODES];
    bool degraded[DISCOVERY_MAX_NODES];
} discovery_gateway_t;

typedef struct {
    void *context;
    int (*set_relay)(void *context, uint16_t address, bool enabled,
                     uint8_t retransmit_count, uint16_t interval_ms);
    int (*verify)(void *context, uint16_t address, uint8_t attempts,
                  uint8_t *successes);
    int (*persist)(void *context, const discovery_gateway_t *gateway);
    int (*publish_commit)(void *context, uint16_t epoch, uint16_t relay_count,
                          uint8_t maximum_ring);
    int (*publish_abort)(void *context, uint16_t epoch, int reason);
} discovery_gateway_io_t;

bool discovery_epoch_is_newer(uint16_t candidate, uint16_t current);
uint16_t discovery_next_epoch(uint16_t current);
discovery_health_t discovery_classify_pdr(uint16_t pdr_per_mille);
uint32_t discovery_slot_start_ms(uint32_t local_phase_start_ms,
                                 uint16_t slot_index, uint16_t slot_duration_ms);
uint8_t discovery_report_ttl(uint8_t estimated_ring, bool ring_reliable,
                             uint8_t maximum_ttl, uint8_t margin);

void discovery_neighbor_table_init(discovery_neighbor_table_t *table,
                                   uint16_t owner, uint16_t epoch);
discovery_result_t discovery_register_probe(discovery_neighbor_table_t *table,
                                            uint16_t source, uint8_t probe_index,
                                            int8_t rssi);
int8_t discovery_average_rssi(const discovery_neighbor_entry_t *entry);
uint16_t discovery_pdr_per_mille(const discovery_neighbor_entry_t *entry,
                                 uint8_t expected_probes);
discovery_result_t discovery_make_report(const discovery_neighbor_table_t *table,
                                         uint8_t expected_probes,
                                         discovery_report_entry_t *entries,
                                         size_t capacity, size_t *count);

void discovery_topology_init(discovery_topology_t *topology);
discovery_result_t discovery_topology_add_node(discovery_topology_t *topology,
                                               const discovery_topology_node_t *node,
                                               size_t *index);
size_t discovery_topology_find_node(const discovery_topology_t *topology,
                                    uint16_t address);
discovery_result_t discovery_topology_add_report(discovery_topology_t *topology,
                                                 uint16_t reporter,
                                                 const discovery_report_entry_t *entries,
                                                 size_t count);
bool discovery_link_is_valid(const discovery_topology_t *topology, size_t a,
                             size_t b, const discovery_configuration_t *config);
bool discovery_link_is_marginal(const discovery_topology_t *topology, size_t a,
                                size_t b);
discovery_result_t discovery_calculate_rings(discovery_topology_t *topology,
                                             size_t gateway_index,
                                             const discovery_configuration_t *config);
discovery_result_t discovery_select_relays(discovery_topology_t *topology,
                                           size_t gateway_index,
                                           const discovery_configuration_t *config,
                                           uint8_t coverage[DISCOVERY_MAX_NODES],
                                           bool degraded[DISCOVERY_MAX_NODES]);
bool discovery_relay_set_is_connected(const discovery_topology_t *topology,
                                      size_t gateway_index,
                                      const discovery_configuration_t *config);
discovery_result_t discovery_select_relay_backups(
    const discovery_topology_t *topology, size_t gateway_index,
    const discovery_configuration_t *config, uint16_t generation,
    discovery_relay_watchdog_plan_t plans[DISCOVERY_MAX_NODES], size_t *plan_count,
    discovery_relay_failure_simulation_t simulations[DISCOVERY_MAX_NODES]);
discovery_result_t discovery_simulate_relay_failure(
    const discovery_topology_t *topology, size_t gateway_index, size_t primary_index,
    size_t backup_index, const discovery_configuration_t *config,
    discovery_relay_failure_simulation_t *simulation);

uint32_t discovery_calculate_takeover_delay_ms(uint8_t backup_priority,
                                               uint16_t local_address);
void discovery_relay_beacon_sender_init(discovery_relay_beacon_sender_t *sender,
                                        uint16_t relay_address, uint16_t epoch,
                                        uint16_t generation, uint8_t relay_priority,
                                        uint16_t period_ms, uint32_t now_ms);
bool discovery_relay_beacon_prepare(discovery_relay_beacon_sender_t *sender,
                                    bool relay_enabled, uint32_t now_ms,
                                    discovery_relay_alive_beacon_t *beacon);
void discovery_relay_backup_init(discovery_relay_backup_monitor_t *monitor,
                                 uint16_t local_address, uint16_t epoch,
                                 const discovery_relay_watchdog_plan_t *plan,
                                 uint8_t backup_priority, uint32_t now_ms);
discovery_result_t discovery_relay_backup_on_beacon(
    discovery_relay_backup_monitor_t *monitor, uint16_t epoch,
    uint16_t source_address, uint16_t generation, uint16_t sequence,
    uint8_t received_ttl, bool primary_relay_enabled, int8_t rssi,
    int8_t minimum_rssi, uint32_t now_ms);
discovery_result_t discovery_relay_backup_on_takeover_beacon(
    discovery_relay_backup_monitor_t *monitor, uint16_t epoch,
    uint16_t failed_relay, uint16_t replacement_relay, uint16_t generation,
    uint8_t replacement_priority, uint8_t received_ttl, uint32_t now_ms);
bool discovery_relay_beacon_timed_out(const discovery_relay_backup_monitor_t *monitor,
                                      uint32_t now_ms);
bool discovery_relay_backup_should_promote(const discovery_relay_backup_monitor_t *monitor,
                                           uint32_t now_ms);
discovery_result_t discovery_relay_backup_process(
    discovery_relay_backup_monitor_t *monitor, uint32_t now_ms,
    const discovery_relay_backup_io_t *io);

void discovery_reassembly_init(discovery_report_reassembly_t *state,
                               uint16_t reporter, uint16_t epoch,
                               uint8_t fragment_count);
discovery_result_t discovery_reassembly_accept(discovery_report_reassembly_t *state,
                                               uint16_t epoch,
                                               uint8_t fragment_index);
uint32_t discovery_reassembly_missing(const discovery_report_reassembly_t *state);

#define DISCOVERY_HEADER_WIRE_SIZE 6U
#define DISCOVERY_REPORT_ENTRY_WIRE_SIZE 8U
size_t discovery_encode_header(uint8_t *output, size_t capacity,
                               const discovery_header_t *header);
discovery_result_t discovery_decode_header(discovery_header_t *header,
                                           const uint8_t *input, size_t length);
size_t discovery_encode_report_entry(uint8_t *output, size_t capacity,
                                     const discovery_report_entry_t *entry);
discovery_result_t discovery_decode_report_entry(discovery_report_entry_t *entry,
                                                 const uint8_t *input, size_t length);

void discovery_gateway_init(discovery_gateway_t *gateway,
                            const discovery_configuration_t *configuration);
discovery_result_t discovery_gateway_start(discovery_gateway_t *gateway,
                                           size_t gateway_index);
discovery_result_t discovery_gateway_compute_plan(discovery_gateway_t *gateway);
discovery_result_t discovery_gateway_apply_plan(discovery_gateway_t *gateway,
                                                const discovery_gateway_io_t *io);
void discovery_gateway_abort(discovery_gateway_t *gateway,
                             const discovery_gateway_io_t *io, int reason);

#ifdef __cplusplus
}
#endif
#endif
