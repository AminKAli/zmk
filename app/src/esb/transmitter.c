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
#include <esb.h>
#include <nrfx_clock.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#define CLOCK_SLEEP_TIMEOUT_US 1000 // 1ms
static u_int32_t last_activity;

static bool ready = true;
static struct esb_payload rx_payload;
static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); // struct zmk_position_state_changed = 14 bytes

#if IS_ENABLED(CONFIG_ZMK_ESB_REDUCE_CONSUMPTION)
#define TIMER DT_NODELABEL(rtc0)
#define DELAY_US 250
#define ALARM_CHANNEL_ID 0
const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
struct counter_alarm_cfg alarm_cfg;

static void esb_idle_timeout(const struct device *counter_dev, uint8_t chan_id, uint32_t ticks, void *user_data){
	if(esb_is_idle()){
		LOG_DBG("ESB is idle, stopping IDLE");
		nrfx_clock_hfclk_stop();
	} else {
		LOG_DBG("ESB is not idle yet");
	}

	counter_cancel_channel_alarm(counter_dev, ALARM_CHANNEL_ID);
}

void reset_idle_checker(){
	int err;

	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY_US);
	alarm_cfg.callback = esb_idle_timeout;
	alarm_cfg.user_data = &alarm_cfg;

	err = counter_cancel_channel_alarm(counter_dev, ALARM_CHANNEL_ID);
	if (err) {
		LOG_ERR("Failed to cancel existing idle counter");
	} else {
		LOG_DBG("Counter alarm cancelled successfully");
	}

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, &alarm_cfg);
	if (-EINVAL == err) {
		printk("Alarm settings invalid\n");
	} else if (-ENOTSUP == err) {
		printk("Alarm setting request not supported\n");
	} else if (err != 0) {
		printk("Error\n");
	} else {
		LOG_DBG("Alarm set");
	}
}
#endif

void event_handler(struct esb_evt const *event)
{
	ready = true;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		LOG_DBG("TX SUCCESS EVENT: %d", event->tx_attempts);
		break;
	case ESB_EVENT_TX_FAILED:
		LOG_DBG("TX FAILED EVENT: %d", event->tx_attempts);
		break;
	case ESB_EVENT_RX_RECEIVED:
		if (esb_read_rx_payload(&rx_payload) == 0) {
			LOG_DBG("Packet received, len %d : "
				"position: %lu, state: %i",
				rx_payload.length,
				rx_payload.data[1] << 24 | rx_payload.data[2] << 16 | rx_payload.data[3] << 8 | rx_payload.data[4],
				rx_payload.data[5]);
		} else {
			LOG_ERR("Error while reading rx packet");
		}
		break;
	}
	if(IS_ENABLED(CONFIG_ZMK_ESB_REDUCE_CONSUMPTION)){
		reset_idle_checker();
	}
}

int clocks_start(void)
{
	LOG_DBG("Starting HF Clock");
	nrfx_clock_hfclk_start();
	while (!nrfx_clock_hfclk_is_running()) {
	}
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
		config.retransmit_delay = 600;
		config.bitrate = ESB_BITRATE_2MBPS;
		config.event_handler = event_handler;
		config.mode = ESB_MODE_PTX;
	} else {
		config.protocol = ESB_PROTOCOL_ESB;
		config.retransmit_delay = 600;
		config.bitrate = ESB_BITRATE_2MBPS;
		config.event_handler = event_handler;
		config.mode = ESB_MODE_PTX;
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

int pos_change_listener(const zmk_event_t *eh) {
    LOG_DBG("zmk pos event received");
	if(IS_ENABLED(CONFIG_ZMK_ESB_REDUCE_CONSUMPTION)){
		clocks_start();
	}
	const struct zmk_position_state_changed *pos_ev;
	if ((pos_ev = as_zmk_position_state_changed(eh)) != NULL) {
		if (pos_ev->state){
			tx_payload.data[0]  = (char)pos_ev->timestamp;
			tx_payload.data[1]  = (char)((pos_ev->timestamp >> 8) & 0xFF);
			tx_payload.data[2]  = (char)((pos_ev->timestamp >> 16) & 0xFF);
			tx_payload.data[3]  = (char)((pos_ev->timestamp >> 24) & 0xFF);
			tx_payload.data[4]  = (char)((pos_ev->timestamp >> 32) & 0xFF);
			tx_payload.data[5]  = (char)((pos_ev->timestamp >> 40) & 0xFF);
			tx_payload.data[6]  = (char)((pos_ev->timestamp >> 48) & 0xFF);
			tx_payload.data[7]  = (char)((pos_ev->timestamp >> 56) & 0xFF);
			tx_payload.data[8]  = (char)pos_ev->position;
			tx_payload.data[9]  = (char)((pos_ev->position >> 8) & 0xFF);
			tx_payload.data[10]  = (char)((pos_ev->position >> 16) & 0xFF);
			tx_payload.data[11]  = (char)((pos_ev->position >> 24) & 0xFF);
			tx_payload.data[12] = pos_ev->source;
			tx_payload.data[13] = 1; // position pressed
		} else {
			tx_payload.data[0]  = (char)pos_ev->timestamp;
			tx_payload.data[1]  = (char)((pos_ev->timestamp >> 8) & 0xFF);
			tx_payload.data[2]  = (char)((pos_ev->timestamp >> 16) & 0xFF);
			tx_payload.data[3]  = (char)((pos_ev->timestamp >> 24) & 0xFF);
			tx_payload.data[4]  = (char)((pos_ev->timestamp >> 32) & 0xFF);
			tx_payload.data[5]  = (char)((pos_ev->timestamp >> 40) & 0xFF);
			tx_payload.data[6]  = (char)((pos_ev->timestamp >> 48) & 0xFF);
			tx_payload.data[7]  = (char)((pos_ev->timestamp >> 56) & 0xFF);
			tx_payload.data[8]  = (char)pos_ev->position;
			tx_payload.data[9]  = (char)((pos_ev->position >> 8) & 0xFF);
			tx_payload.data[10]  = (char)((pos_ev->position >> 16) & 0xFF);
			tx_payload.data[11]  = (char)((pos_ev->position >> 24) & 0xFF);
			tx_payload.data[12] = pos_ev->source;
			tx_payload.data[13] = 0; // position released
		}
		LOG_DBG("sending payload: len %d, timestamp: %" PRId64 ", position: %lu, source: %u, state: %s.",
				tx_payload.length,
				(int64_t)tx_payload.data[0] | (int64_t)tx_payload.data[1] << 8 | (int64_t)tx_payload.data[2] << 16 | (int64_t)tx_payload.data[3] << 24 | (int64_t)tx_payload.data[4] << 32 | (int64_t)tx_payload.data[5] << 40 | (int64_t)tx_payload.data[6] << 48 | (int64_t)tx_payload.data[7] << 56,
				(uint32_t)tx_payload.data[8] | (uint32_t)tx_payload.data[9] << 8 | (uint32_t)tx_payload.data[10] << 16 | (uint32_t)tx_payload.data[11] << 24,
				(uint8_t)tx_payload.data[12],
				tx_payload.data[13] ? "pressed" : "released");
		return esb_write_payload(&tx_payload);
	}
	return ZMK_EV_EVENT_HANDLED;
}

static void zmk_esb_transmitter_init(void) {
	int err;

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}
	LOG_DBG("ESB Initialization success");

	if(IS_ENABLED(CONFIG_ZMK_ESB_REDUCE_CONSUMPTION)){
		err = counter_start(counter_dev);
		if (err) {
			LOG_ERR("Counter initialization failed, err %d", err);
			return 0;
		}
		LOG_DBG("Counter Initialization success");

		reset_idle_checker();
	} else {
		clocks_start();
	}
}


SYS_INIT(zmk_esb_transmitter_init, APPLICATION, 50);
ZMK_LISTENER(transmitter, pos_change_listener);
ZMK_SUBSCRIPTION(transmitter, zmk_position_state_changed);