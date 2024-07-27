#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#include <zephyr/types.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <nrf.h>
#include <nrf_gzll.h>
#include <gzll_glue.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

/* Pipe 0 is used in this example. */
#define PIPE_NUMBER 0
const uint8_t CHANNEL_TABLE[] = {0,100,200};
#define CHANNEL_TABLE_SIZE 3

/* 1-byte payload length is used when transmitting. */
#define TX_PAYLOAD_LENGTH 1

/* Gazell Link Layer RX result structure */
struct gzll_rx_result {
	uint32_t pipe;
	nrf_gzll_host_rx_info_t info;
};

/* Placeholder for data payload received from host. */
static uint8_t data_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];

/* Payload to attach to ACK sent to device. */
static uint8_t ack_payload[TX_PAYLOAD_LENGTH];

/* Gazell Link Layer RX result message queue */
K_MSGQ_DEFINE(gzll_msgq,
	      sizeof(struct gzll_rx_result),
	      1,
	      sizeof(uint32_t));

static void gzll_rx_result_handler(struct gzll_rx_result *rx_result);
static void gzll_work_handler(struct k_work *work);

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info){}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info){}
void nrf_gzll_disabled(void){}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {
	int err;
	struct gzll_rx_result rx_result;
	
	uint32_t data_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
	
	/* Pop packet and write first byte of the payload to the GPIO port. */
	bool result_value = nrf_gzll_fetch_packet_from_rx_fifo(pipe,
							       data_payload,
							       &data_payload_length);

	struct zmk_position_state_changed ev;

	ev.timestamp = (int64_t)data_payload[0] | (int64_t)data_payload[1] << 8 | (int64_t)data_payload[2] << 16 | (int64_t)data_payload[3] << 24 | (int64_t)data_payload[4] << 32 | (int64_t)data_payload[5] << 40 | (int64_t)data_payload[6] << 48 | (int64_t)data_payload[7] << 56;
	ev.position  = (uint32_t)data_payload[8] | (uint32_t)data_payload[9] << 8 | (uint32_t)data_payload[10] << 16 | (uint32_t)data_payload[11] << 24;
    ev.source    = data_payload[12];
	ev.state      = data_payload[13];

	LOG_DBG("Received position update: len: %d, timestamp: %" PRId64 ", position: %lu, source: %u, state: %d.",
				data_payload_length, ev.timestamp, ev.position, ev.source, ev.state);

	raise_zmk_position_state_changed(ev);
	// ack_payload[0] = 0;
	// result_value = nrf_gzll_add_packet_to_tx_fifo(pipe, ack_payload, TX_PAYLOAD_LENGTH);
	// if (!result_value) {
	// 	LOG_ERR("TX fifo error");
	// }
}

static void gzll_rx_result_handler(struct gzll_rx_result *rx_result) {
	LOG_DBG("gzll_rx_result_handler-1");
	int err;
	uint32_t data_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
	
	/* Pop packet and write first byte of the payload to the GPIO port. */
	bool result_value = nrf_gzll_fetch_packet_from_rx_fifo(rx_result->pipe,
							       data_payload,
							       &data_payload_length);

	struct zmk_position_state_changed ev;

	ev.timestamp = (int64_t)data_payload[0] | (int64_t)data_payload[1] << 8 | (int64_t)data_payload[2] << 16 | (int64_t)data_payload[3] << 24 | (int64_t)data_payload[4] << 32 | (int64_t)data_payload[5] << 40 | (int64_t)data_payload[6] << 48 | (int64_t)data_payload[7] << 56;
	ev.position  = (uint32_t)data_payload[8] | (uint32_t)data_payload[9] << 8 | (uint32_t)data_payload[10] << 16 | (uint32_t)data_payload[11] << 24;
    ev.source    = data_payload[12]; 
	ev.state      = data_payload[13];

	LOG_DBG("Received position update: len: %d, timestamp: %" PRId64 ", position: %lu, source: %u, state: %d.",
				data_payload_length, ev.timestamp, ev.position, ev.source, ev.state);

	raise_zmk_position_state_changed(ev);
}

static void zmk_gazell_receiver_init(void) {
	int err;

	// k_work_init(&gzll_work, gzll_work_handler);

	/* Initialize Gazell Link Layer glue */
	if (!gzll_glue_init()) {
		LOG_ERR("Cannot initialize GZLL glue code");
		return 0;
	}
	LOG_DBG("initialized gzll glue");

	/* Initialize the Gazell Link Layer */
	if (!nrf_gzll_init(NRF_GZLL_MODE_HOST)) {
		LOG_ERR("Cannot initialize GZLL");
		return 0;
	}
	LOG_DBG("initialized gzll link layer");

	if (!nrf_gzll_set_timeslot_period(600)){
		LOG_ERR("Cannot change timeslot to 600us");
		return 0;
	}
	LOG_DBG("setting timeslot period to 600us");

	if (!nrf_gzll_set_datarate(NRF_GZLL_DATARATE_2MBIT)){
		LOG_ERR("Cannot set datarate to 2mbit");
		return 0;
	}
	LOG_DBG("setting datarate to 2mbit");

	if(!nrf_gzll_set_channel_table(CHANNEL_TABLE, CHANNEL_TABLE_SIZE)){
		LOG_ERR("Failed setting channel table to size 3");
		return 0;
	}
	LOG_DBG("setting table to 3");

	if(!nrf_gzll_set_device_channel_selection_policy(NRF_GZLL_DEVICE_CHANNEL_SELECTION_POLICY_USE_CURRENT)){
		LOG_ERR("Failed setting selection policy to USE_CURRENT");
		return 0;
	}
	LOG_DBG("setting selection policy to USE_CURRENT");
	
	LOG_DBG("GZLL Timeslots per Channel: %d, Timeslot period: %dus", nrf_gzll_get_timeslots_per_channel(), nrf_gzll_get_timeslot_period());

	if (!nrf_gzll_enable()) {
		LOG_ERR("Cannot enable GZLL");
		return 0;
	}
	LOG_DBG("gzll enabled!");

}

SYS_INIT(zmk_gazell_receiver_init, APPLICATION, 50);
