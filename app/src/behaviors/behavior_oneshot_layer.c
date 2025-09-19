// app/src/behaviors/behavior_oneshot_layer.c
// SPDX-License-Identifier: MIT
#define DT_DRV_COMPAT zmk_behavior_oneshot_layer

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>

/* Devicetree-configured behavior properties */
struct osl_cfg {
    int32_t release_after_ms;
    bool pre_cancel; /* kept for compatibility; not used in this version */
};

/* Runtime state (single instance) */
struct osl_state {
    bool active;
    bool consumed;                 /* first foreign key already seen */
    uint8_t layer;                 /* target layer */
    uint8_t src_pos;               /* arming key position */
    struct k_work_delayable timeout_work;
};

static struct osl_state OSL;

/* Helpers */
static void osl_deactivate(void) {
    if (!OSL.active) return;
    zmk_keymap_layer_deactivate(OSL.layer);
    OSL.active = false;
    k_work_cancel_delayable(&OSL.timeout_work);
}

static void osl_timeout(struct k_work *work) {
    ARG_UNUSED(work);
    osl_deactivate();
}

/* Behavior API */
static int osl_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding);
    const struct osl_cfg *cfg = dev->config;

    OSL.layer   = binding->param1;   /* first cell = layer id */
    OSL.src_pos = event.position;
    OSL.active  = true;
    OSL.consumed = false;

    zmk_keymap_layer_activate(OSL.layer);
    k_work_schedule(&OSL.timeout_work, K_MSEC(cfg->release_after_ms));
    return ZMK_BEHAVIOR_OPAQUE;      /* no underlying key */
}

static int osl_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    /* release of arming key is ignored; timeout or next key handles cancel */
    return ZMK_BEHAVIOR_OPAQUE;
}

/*
 * Global listeners
 *
 * 1) position_state_changed: ignore arming key press; used only if you want
 *    to react on raw switch events (not needed for cancel timing here).
 * 2) keycode_state_changed: fires after mapping to keycodes; we cancel on the
 *    first foreign key PRESS so that key is produced on the oneshot layer,
 *    then subsequent keys land on base/normal layers.
 */

/* (Optional) position listener kept for completeness; does nothing now */
static int osl_pos_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE;
    /* ignore the arming key; we cancel based on keycode event below */
    if (OSL.active && ev->position == OSL.src_pos) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

/* Cancel AFTER first foreign key is mapped (exactly one key uses the layer) */
static int osl_keycode_listener_cb(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE; /* only on keycode press */
    if (!OSL.active || OSL.consumed) return ZMK_EV_EVENT_BUBBLE;

    /* First produced keycode after arming: let it go through on the layer,
       then drop the layer immediately. */
    OSL.consumed = true;
    osl_deactivate();
    return ZMK_EV_EVENT_BUBBLE;
}

static int osl_init(const struct device *dev) {
    ARG_UNUSED(dev);
    OSL.active = false;
    OSL.consumed = false;
    k_work_init_delayable(&OSL.timeout_work, osl_timeout);
    return 0;
}

static const struct behavior_driver_api osl_api = {
    .binding_pressed  = osl_pressed,
    .binding_released = osl_released,
};

#define OSL_INST(n)                                                             \
    static const struct osl_cfg osl_cfg_##n = {                                 \
        .release_after_ms = DT_INST_PROP_OR(n, release_after_ms, 1000),         \
        .pre_cancel       = DT_INST_PROP_OR(n, pre_cancel, true),               \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(n, osl_init, NULL, NULL, &osl_cfg_##n,                \
                          APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &osl_api);

DT_INST_FOREACH_STATUS_OKAY(OSL_INST)

/* Subscriptions */
ZMK_LISTENER(osl_pos_listener, osl_pos_listener_cb);
ZMK_SUBSCRIPTION(osl_pos_listener, zmk_position_state_changed);

ZMK_LISTENER(osl_keycode_listener, osl_keycode_listener_cb);
ZMK_SUBSCRIPTION(osl_keycode_listener, zmk_keycode_state_changed);
