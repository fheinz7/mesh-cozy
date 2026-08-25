#include "mesh_discovery.h"

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

size_t discovery_encode_header(uint8_t *output, size_t capacity,
                               const discovery_header_t *header)
{
    if (output == NULL || header == NULL || capacity < DISCOVERY_HEADER_WIRE_SIZE) {
        return 0U;
    }
    output[0] = header->protocol_version;
    output[1] = header->message_type;
    put_le16(&output[2], header->epoch);
    put_le16(&output[4], header->sequence);
    return DISCOVERY_HEADER_WIRE_SIZE;
}

discovery_result_t discovery_decode_header(discovery_header_t *header,
                                           const uint8_t *input, size_t length)
{
    if (header == NULL || input == NULL) return DISCOVERY_INVALID_ARGUMENT;
    if (length < DISCOVERY_HEADER_WIRE_SIZE) return DISCOVERY_MALFORMED;
    if (input[0] != DISCOVERY_PROTOCOL_VERSION) return DISCOVERY_MALFORMED;
    if (input[1] < MSG_DISCOVERY_ENTER || input[1] > MSG_RELAY_RESTORED_EVENT)
        return DISCOVERY_MALFORMED;
    header->protocol_version = input[0];
    header->message_type = input[1];
    header->epoch = get_le16(&input[2]);
    header->sequence = get_le16(&input[4]);
    return DISCOVERY_OK;
}

size_t discovery_encode_report_entry(uint8_t *output, size_t capacity,
                                     const discovery_report_entry_t *entry)
{
    if (output == NULL || entry == NULL || capacity < DISCOVERY_REPORT_ENTRY_WIRE_SIZE)
        return 0U;
    put_le16(output, entry->neighbor_address);
    put_le16(&output[2], entry->pdr_per_mille);
    output[4] = (uint8_t)entry->average_rssi;
    output[5] = (uint8_t)entry->minimum_rssi;
    output[6] = (uint8_t)entry->maximum_rssi;
    output[7] = entry->received_probes;
    return DISCOVERY_REPORT_ENTRY_WIRE_SIZE;
}

discovery_result_t discovery_decode_report_entry(discovery_report_entry_t *entry,
                                                 const uint8_t *input, size_t length)
{
    if (entry == NULL || input == NULL) return DISCOVERY_INVALID_ARGUMENT;
    if (length < DISCOVERY_REPORT_ENTRY_WIRE_SIZE) return DISCOVERY_MALFORMED;
    entry->neighbor_address = get_le16(input);
    entry->pdr_per_mille = get_le16(&input[2]);
    if (entry->pdr_per_mille > 1000U) return DISCOVERY_MALFORMED;
    entry->average_rssi = (int8_t)input[4];
    entry->minimum_rssi = (int8_t)input[5];
    entry->maximum_rssi = (int8_t)input[6];
    entry->received_probes = input[7];
    return DISCOVERY_OK;
}

void discovery_reassembly_init(discovery_report_reassembly_t *state,
                               uint16_t reporter, uint16_t epoch,
                               uint8_t fragment_count)
{
    if (state == NULL) return;
    state->reporter_address = reporter;
    state->epoch = epoch;
    state->received_fragments = 0U;
    state->expected_fragments = fragment_count <= DISCOVERY_MAX_REPORT_FRAGMENTS
                                    ? fragment_count : 0U;
    state->complete = false;
}

discovery_result_t discovery_reassembly_accept(discovery_report_reassembly_t *state,
                                               uint16_t epoch,
                                               uint8_t fragment_index)
{
    uint32_t expected;
    if (state == NULL) return DISCOVERY_INVALID_ARGUMENT;
    if (epoch != state->epoch) return DISCOVERY_STALE_EPOCH;
    if (state->expected_fragments == 0U || fragment_index >= state->expected_fragments)
        return DISCOVERY_MALFORMED;
    state->received_fragments |= UINT32_C(1) << fragment_index;
    expected = state->expected_fragments == 32U
                 ? UINT32_MAX : ((UINT32_C(1) << state->expected_fragments) - 1U);
    state->complete = state->received_fragments == expected;
    return DISCOVERY_OK;
}

uint32_t discovery_reassembly_missing(const discovery_report_reassembly_t *state)
{
    uint32_t expected;
    if (state == NULL || state->expected_fragments == 0U) return 0U;
    expected = state->expected_fragments == 32U
                 ? UINT32_MAX : ((UINT32_C(1) << state->expected_fragments) - 1U);
    return expected & ~state->received_fragments;
}
