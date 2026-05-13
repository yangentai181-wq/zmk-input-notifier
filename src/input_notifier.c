/*
 * zmk-input-notifier
 *
 * Streams PMW3610 (or any Zephyr input subsystem source) pointer deltas
 * and ZMK rotary-encoder ticks over the existing zmk-raw-hid channel
 * (vendor usage page 0xFF60).
 */

#include <string.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/sensor_event.h>

#include <raw_hid/events.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define POINTER_MARKER 0xF2
#define ENCODER_MARKER 0xF3

static uint8_t hid_pointer_buf[CONFIG_RAW_HID_REPORT_SIZE];
static uint8_t hid_encoder_buf[CONFIG_RAW_HID_REPORT_SIZE];

/* Accumulators for one logical pointer frame (between two SYN events) plus
 * any extra frames that arrive while the throttle work item is pending. */
static int32_t acc_dx;
static int32_t acc_dy;
static int32_t acc_wheel;
static int32_t acc_hwheel;
static uint8_t acc_buttons;
static struct k_spinlock pointer_lock;

static void pointer_flush_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(pointer_flush, pointer_flush_work);
static bool pointer_flush_pending;

static int16_t clamp_i16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return (int16_t)v;
}

static void send_pointer_locked(void) {
    /* Caller holds pointer_lock or we are inside the work item which is
     * the only consumer. */
    int16_t dx = clamp_i16(acc_dx);
    int16_t dy = clamp_i16(acc_dy);
    int16_t wheel = clamp_i16(acc_wheel);
    int16_t hwheel = clamp_i16(acc_hwheel);

    memset(hid_pointer_buf, 0, sizeof(hid_pointer_buf));
    hid_pointer_buf[0] = POINTER_MARKER;
    hid_pointer_buf[1] = 9; /* payload byte count after the marker/length */
    memcpy(&hid_pointer_buf[2], &dx, sizeof(dx));
    memcpy(&hid_pointer_buf[4], &dy, sizeof(dy));
    memcpy(&hid_pointer_buf[6], &wheel, sizeof(wheel));
    memcpy(&hid_pointer_buf[8], &hwheel, sizeof(hwheel));
    hid_pointer_buf[10] = acc_buttons;

    acc_dx = 0;
    acc_dy = 0;
    acc_wheel = 0;
    acc_hwheel = 0;

    raise_raw_hid_sent_event((struct raw_hid_sent_event){
        .data = hid_pointer_buf,
        .length = sizeof(hid_pointer_buf),
    });
}

static void pointer_flush_work(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&pointer_lock);
    pointer_flush_pending = false;
    bool has_data = acc_dx || acc_dy || acc_wheel || acc_hwheel;
    if (has_data) {
        send_pointer_locked();
    }
    k_spin_unlock(&pointer_lock, key);
}

static void pointer_cb(struct input_event *evt) {
    if (!evt) return;

    bool merged = false;
    k_spinlock_key_t key = k_spin_lock(&pointer_lock);
    switch (evt->code) {
    case INPUT_REL_X:
        acc_dx += evt->value;
        merged = true;
        break;
    case INPUT_REL_Y:
        acc_dy += evt->value;
        merged = true;
        break;
    case INPUT_REL_WHEEL:
        acc_wheel += evt->value;
        merged = true;
        break;
    case INPUT_REL_HWHEEL:
        acc_hwheel += evt->value;
        merged = true;
        break;
    case INPUT_BTN_0:
    case INPUT_BTN_LEFT:
        WRITE_BIT(acc_buttons, 0, evt->value);
        merged = true;
        break;
    case INPUT_BTN_1:
    case INPUT_BTN_RIGHT:
        WRITE_BIT(acc_buttons, 1, evt->value);
        merged = true;
        break;
    case INPUT_BTN_2:
    case INPUT_BTN_MIDDLE:
        WRITE_BIT(acc_buttons, 2, evt->value);
        merged = true;
        break;
    default:
        break;
    }
    k_spin_unlock(&pointer_lock, key);

    if (!merged || !evt->sync) return;

    /* SYN event finalizes the frame. Throttle to BURST_MS to bound the
     * effective send rate over BLE without dropping any motion. */
    const k_timeout_t delay =
        CONFIG_ZMK_INPUT_NOTIFIER_MOUSE_BURST_MS == 0
            ? K_NO_WAIT
            : K_MSEC(CONFIG_ZMK_INPUT_NOTIFIER_MOUSE_BURST_MS);

    key = k_spin_lock(&pointer_lock);
    if (!pointer_flush_pending) {
        pointer_flush_pending = true;
        k_work_schedule(&pointer_flush, delay);
    }
    k_spin_unlock(&pointer_lock, key);
}

/* dev=NULL means listen to every input device — that covers PMW3610 and any
 * future pointer drivers without referencing a specific compatible. */
INPUT_CALLBACK_DEFINE(NULL, pointer_cb);

/* ---------- Encoder ---------- */

static int encoder_listener(const zmk_event_t *eh) {
    const struct zmk_sensor_event *ev = as_zmk_sensor_event(eh);
    if (!ev || ev->channel_data_size == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    int32_t raw_delta = ev->channel_data[0].value.val1;
    int8_t delta = (int8_t)CLAMP(raw_delta, -127, 127);
    if (delta == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    memset(hid_encoder_buf, 0, sizeof(hid_encoder_buf));
    hid_encoder_buf[0] = ENCODER_MARKER;
    hid_encoder_buf[1] = 2;
    hid_encoder_buf[2] = (uint8_t)ev->sensor_index;
    hid_encoder_buf[3] = (uint8_t)delta;

    raise_raw_hid_sent_event((struct raw_hid_sent_event){
        .data = hid_encoder_buf,
        .length = sizeof(hid_encoder_buf),
    });
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zin_encoder, encoder_listener);
ZMK_SUBSCRIPTION(zin_encoder, zmk_sensor_event);
