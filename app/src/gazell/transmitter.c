#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#include <zephyr/types.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <nrf.h>
#include <nrf_gzll.h>
#include <gzll_glue.h>
#include <nrfx_clock.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

/* Pipe 0 is used in this example. */
#define PIPE_NUMBER 0

const uint8_t CHANNEL_TABLE[] = {0,100,200};
#define CHANNEL_TABLE_SIZE 3

/* 1-byte payload length is used when transmitting. */
#define TX_PAYLOAD_LENGTH 14

/* Maximum number of transmission attempts */
#define MAX_TX_ATTEMPTS 100

/* Gazell Link Layer TX result structure */
struct gzll_tx_result {
	bool success;
	uint32_t pipe;
	nrf_gzll_device_tx_info_t info;
};

/* Payload to send to Host. */
static uint8_t data_payload[TX_PAYLOAD_LENGTH];

/* Placeholder for received ACK payloads from Host. */
static uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];

static void gzll_tx_result_handler(struct gzll_tx_result *tx_result){
	int err;
	bool result_value;
	uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;

	if (tx_result->success) {
		if (tx_result->info.payload_received_in_ack) {
			/* Pop packet and write first byte of the payload to the GPIO port. */
			nrf_gzll_fetch_packet_from_rx_fifo(tx_result->pipe, ack_payload, &ack_payload_length);
		}
	} else {
		LOG_ERR("Gazell transmission failed");
	}

	/* Load data payload into the TX queue. */
	data_payload[0] = ~dk_get_buttons();
	result_value = nrf_gzll_add_packet_to_tx_fifo(tx_result->pipe,
						      data_payload,
						      TX_PAYLOAD_LENGTH);
	if (!result_value) {
		LOG_ERR("TX fifo error");
	}
}

int pos_change_listener(const zmk_event_t *eh) {
    LOG_DBG("zmk pos event received");
	const struct zmk_position_state_changed *pos_ev;
	if ((pos_ev = as_zmk_position_state_changed(eh)) != NULL) {
		if (pos_ev->state){
			data_payload[0]  = (char)pos_ev->timestamp;
			data_payload[1]  = (char)((pos_ev->timestamp >> 8) & 0xFF);
			data_payload[2]  = (char)((pos_ev->timestamp >> 16) & 0xFF);
			data_payload[3]  = (char)((pos_ev->timestamp >> 24) & 0xFF);
			data_payload[4]  = (char)((pos_ev->timestamp >> 32) & 0xFF);
			data_payload[5]  = (char)((pos_ev->timestamp >> 40) & 0xFF);
			data_payload[6]  = (char)((pos_ev->timestamp >> 48) & 0xFF);
			data_payload[7]  = (char)((pos_ev->timestamp >> 56) & 0xFF);
			data_payload[8]  = (char)pos_ev->position;
			data_payload[9]  = (char)((pos_ev->position >> 8) & 0xFF);
			data_payload[10]  = (char)((pos_ev->position >> 16) & 0xFF);
			data_payload[11]  = (char)((pos_ev->position >> 24) & 0xFF);
			data_payload[12] = pos_ev->source;
			data_payload[13] = 1; // position pressed
		} else {
			data_payload[0]  = (char)pos_ev->timestamp;
			data_payload[1]  = (char)((pos_ev->timestamp >> 8) & 0xFF);
			data_payload[2]  = (char)((pos_ev->timestamp >> 16) & 0xFF);
			data_payload[3]  = (char)((pos_ev->timestamp >> 24) & 0xFF);
			data_payload[4]  = (char)((pos_ev->timestamp >> 32) & 0xFF);
			data_payload[5]  = (char)((pos_ev->timestamp >> 40) & 0xFF);
			data_payload[6]  = (char)((pos_ev->timestamp >> 48) & 0xFF);
			data_payload[7]  = (char)((pos_ev->timestamp >> 56) & 0xFF);
			data_payload[8]  = (char)pos_ev->position;
			data_payload[9]  = (char)((pos_ev->position >> 8) & 0xFF);
			data_payload[10]  = (char)((pos_ev->position >> 16) & 0xFF);
			data_payload[11]  = (char)((pos_ev->position >> 24) & 0xFF);
			data_payload[12] = pos_ev->source;
			data_payload[13] = 0; // position released
		}
		return nrf_gzll_add_packet_to_tx_fifo(PIPE_NUMBER, data_payload, TX_PAYLOAD_LENGTH);
	}
	return ZMK_EV_EVENT_HANDLED;
}

static void zmk_gazell_transmitter_init(void) {
	int err;
	bool result_value;

	/* Initialize Gazell Link Layer glue */
	if (!gzll_glue_init()) {
		LOG_ERR("Cannot initialize GZLL glue code");
		return 0;
	}

	/* Initialize the Gazell Link Layer */
	if (!nrf_gzll_init(NRF_GZLL_MODE_DEVICE)) {
		LOG_ERR("Cannot initialize GZLL");
		return 0;
	}

	nrf_gzll_set_max_tx_attempts(MAX_TX_ATTEMPTS);
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

	if (!nrf_gzll_enable()) {
		LOG_ERR("Cannot enable GZLL");
		return 0;
	}
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info){
	LOG_DBG("successful transmission");
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info){
	LOG_DBG("failed transmission");
}

void nrf_gzll_disabled(void) {}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info){}

SYS_INIT(zmk_gazell_transmitter_init, APPLICATION, 50);
ZMK_LISTENER(transmitter, pos_change_listener);
ZMK_SUBSCRIPTION(transmitter, zmk_position_state_changed);