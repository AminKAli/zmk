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
#include <esb.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

static struct esb_payload rx_payload;
static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); // struct zmk_position_state_changed = 14 bytes

void position_state_update(struct esb_payload position_update_payload) {
    struct zmk_position_state_changed ev;

	ev.timestamp = (int64_t)position_update_payload.data[0] | (int64_t)position_update_payload.data[1] << 8 | (int64_t)position_update_payload.data[2] << 16 | (int64_t)position_update_payload.data[3] << 24 | (int64_t)position_update_payload.data[4] << 32 | (int64_t)position_update_payload.data[5] << 40 | (int64_t)position_update_payload.data[6] << 48 | (int64_t)position_update_payload.data[7] << 56;
	ev.position  = (uint32_t)position_update_payload.data[8] | (uint32_t)position_update_payload.data[9] << 8 | (uint32_t)position_update_payload.data[10] << 16 | (uint32_t)position_update_payload.data[11] << 24;
    ev.source    = position_update_payload.data[12]; 
	ev.state      = position_update_payload.data[13];

	LOG_DBG("Received position update: len: %d, timestamp: %" PRId64 ", position: %lu, source: %u, state: %d.",
				tx_payload.length, ev.timestamp, ev.position, ev.source, ev.state);

	raise_zmk_position_state_changed(ev);
}

void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		LOG_DBG("TX SUCCESS EVENT");
		break;
	case ESB_EVENT_TX_FAILED:
		LOG_DBG("TX FAILED EVENT");
		break;
	case ESB_EVENT_RX_RECEIVED:
		if (esb_read_rx_payload(&rx_payload) == 0) {
			position_state_update(rx_payload);
		} else {
			LOG_ERR("Error while reading rx packet");
		}
		break;
	}
}

int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

	LOG_DBG("HF clock started");
	return 0;
}

int esb_initialize(void)
{
	int err;
	/* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
	uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	struct esb_config config = ESB_DEFAULT_CONFIG;
	if (IS_ENABLED(CONFIG_ZMK_ESB_DYNAMIC_PAYLOAD)){
		config.protocol = ESB_PROTOCOL_ESB_DPL;
		config.bitrate = ESB_BITRATE_2MBPS;
		config.mode = ESB_MODE_PRX;
		config.event_handler = event_handler;
	} else {
		config.protocol = ESB_PROTOCOL_ESB;
		config.bitrate = ESB_BITRATE_2MBPS;
		config.mode = ESB_MODE_PRX;
		config.event_handler = event_handler;
		config.payload_length = 14;
	}
	if (IS_ENABLED(CONFIG_ESB_FAST_RAMP_UP)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err) {
		return err;
	}

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}

	err = esb_set_rf_channel(2);
	if (err) {
		return err;
	}

	return 0;
}

// we will call esb_read_rx_payload in the event handler
// we will call esb_start_rx during initalization

static void zmk_esb_receiver_init(void) {
	int err;

	err = clocks_start();
	if (err) {
		LOG_ERR("HF Clock initialization failed, err %d", err);
		return 0;
	}

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

	err = esb_start_rx();
	if (err) {
		LOG_ERR("RX setup failed, err %d", err);
		return 0;
	} else {
		LOG_DBG("ESB RX started");
	}


}

SYS_INIT(zmk_esb_receiver_init, APPLICATION, 50);
