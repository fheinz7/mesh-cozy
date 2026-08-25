#include "mesh_discovery.h"

#include <stdio.h>
#include <string.h>

typedef struct { bool relay_enabled; } node_platform_t;

/* In Zephyr this must update the state used by the Configuration Server. */
static int set_local_relay(void *context, bool enabled)
{
    node_platform_t *platform = context;
    platform->relay_enabled = enabled;
    printf("Local Relay is now %s\n", enabled ? "enabled" : "disabled");
    return 0;
}

/* Publish this vendor message with TTL=0 and Application Transmit=0. */
static int send_takeover_beacon(void *context, uint16_t primary,
                                uint16_t replacement, uint16_t generation,
                                uint16_t sequence, uint8_t priority)
{
    (void)context;
    printf("TAKEOVER_BEACON TTL=0: failed=0x%04X replacement=0x%04X "
           "generation=%u sequence=%u priority=%u\n",
           primary, replacement, generation, sequence, priority);
    return 0;
}

/* Send toward the gateway with TTL = calculated ring + margin. */
static int send_takeover_event(void *context, uint16_t primary,
                               uint16_t replacement, uint16_t generation,
                               uint8_t priority, uint32_t beacon_age_ms)
{
    (void)context;
    printf("TAKEOVER_EVENT: failed=0x%04X replacement=0x%04X generation=%u "
           "priority=%u last_beacon_age=%lu ms\n", primary, replacement,
           generation, priority, (unsigned long)beacon_age_ms);
    return 0;
}

static int send_restored_event(void *context, uint16_t primary,
                               uint16_t replacement, uint16_t generation)
{
    (void)context;
    printf("RESTORED_EVENT: primary=0x%04X backup=0x%04X generation=%u\n",
           primary, replacement, generation);
    return 0;
}

static void primary_relay_example(void)
{
    discovery_relay_beacon_sender_t sender;
    discovery_relay_alive_beacon_t beacon;
    uint32_t now_ms;

    puts("\nPrimary relay behavior:");
    discovery_relay_beacon_sender_init(&sender, 0x0101U, 12U, 4U, 0U,
                                       DISCOVERY_RELAY_BEACON_PERIOD_MS, 0U);
    for (now_ms = 0U; now_ms <= 6000U; now_ms += 1000U) {
        if (discovery_relay_beacon_prepare(&sender, true, now_ms, &beacon)) {
            printf("RELAY_ALIVE TTL=0: relay=0x%04X epoch=%u generation=%u sequence=%u\n",
                   beacon.relay_address, beacon.header.epoch,
                   beacon.configuration_generation, beacon.beacon_sequence);
            /* bt_mesh_model_send(..., .send_ttl = 0); */
        }
    }
}

static void backup_node_example(void)
{
    discovery_relay_watchdog_plan_t plan;
    discovery_relay_backup_monitor_t monitor;
    discovery_relay_backup_io_t io;
    node_platform_t platform = {false};
    uint32_t now_ms;

    memset(&plan, 0, sizeof(plan));
    plan.primary_relay = 0x0101U;
    plan.backup_nodes[0] = 0x0102U;
    plan.backup_count = 1U;
    plan.configuration_generation = 4U;
    plan.beacon_period_ms = DISCOVERY_RELAY_BEACON_PERIOD_MS;
    plan.missed_beacon_threshold = DISCOVERY_RELAY_MISSED_BEACON_THRESHOLD;
    plan.recovery_stable_ms = DISCOVERY_RELAY_RECOVERY_STABLE_MS;
    plan.minimum_relay_active_ms = DISCOVERY_RELAY_MINIMUM_ACTIVE_MS;

    memset(&io, 0, sizeof(io));
    io.context = &platform;
    io.set_local_relay = set_local_relay;
    io.send_takeover_beacon = send_takeover_beacon;
    io.send_takeover_event = send_takeover_event;
    io.send_restored_event = send_restored_event;

    puts("\nBackup node behavior:");
    discovery_relay_backup_init(&monitor, 0x0102U, 12U, &plan, 0U, 0U);

    /* One valid direct beacon arrives, then the primary disappears. */
    (void)discovery_relay_backup_on_beacon(&monitor, 12U, 0x0101U, 4U, 1U,
                                           0U, true, -68, -90, 0U);
    for (now_ms = 1000U; now_ms <= 18000U; now_ms += 250U)
        (void)discovery_relay_backup_process(&monitor, now_ms, &io);

    /* The primary returns. Consecutive direct beacons prove stability. */
    for (now_ms = 19000U; now_ms <= 31000U; now_ms += 2000U) {
        uint16_t sequence = (uint16_t)(2U + (now_ms - 19000U) / 2000U);
        (void)discovery_relay_backup_on_beacon(&monitor, 12U, 0x0101U, 4U,
                                               sequence, 0U, true, -67, -90,
                                               now_ms);
        (void)discovery_relay_backup_process(&monitor, now_ms, &io);
    }
    for (now_ms = 31250U; now_ms <= 35000U; now_ms += 250U)
        (void)discovery_relay_backup_process(&monitor, now_ms, &io);

    printf("Final backup state=%d relay=%s promote=%s\n", monitor.state,
           platform.relay_enabled ? "enabled" : "disabled",
           discovery_relay_backup_should_promote(&monitor, now_ms) ? "yes" : "no");
}

int main(void)
{
    primary_relay_example();
    backup_node_example();
    return 0;
}
