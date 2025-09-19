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

/* --- Devicetree-configured behavior properties --- */
struct osl_cfg {
    int32_t release_after_ms;
    bool pre_cancel;
};

/* --- Runtime state (single instance is enough for typical keyboards) --- */
struct osl_state {
    bool active;
    uint8_t layer;
    uint8_t src_pos;                 /* key position that armed the oneshot */
    bool pre_cancel;                 /* cached from DT prop for fast check   */
    struct k_work_delayable timeout_work;
};

static struct osl_state OSL;

/* --- Helpers --- */
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

/* --- Behavior API --- */
static int osl_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding);
    const struct osl_cfg *cfg = dev->config;

    OSL.layer    = binding->param1;   /* first cell = target layer index */
    OSL.src_pos  = event.position;
    OSL.pre_cancel = cfg->pre_cancel;
    OSL.active   = true;

    zmk_keymap_layer_activate(OSL.layer);
    k_work_schedule(&OSL.timeout_work, K_MSEC(cfg->release_after_ms));
    return ZMK_BEHAVIOR_OPAQUE;       /* this behavior doesn’t emit a keycode */
}

static int osl_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    /* release of the arming key is ignored; timer or next press will cancel */
    return ZMK_BEHAVIOR_OPAQUE;
}

/* --- Global listener: cancel BEFORE dispatching any other key press --- */
static int osl_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE;   /* only on press */
    if (!OSL.active) return ZMK_EV_EVENT_BUBBLE;

    /* ignore the source key that armed the oneshot */
    if (ev->position == OSL.src_pos) return ZMK_EV_EVENT_BUBBLE;

    /* pre-cancel semantics: drop immediately so this press lands on base/normal layers */
    if (OSL.pre_cancel) {
        osl_deactivate();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

/* --- Driver init / instance boilerplate --- */
static int osl_init(const struct device *dev) {
    ARG_UNUSED(dev);
    OSL.active = false;
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

/* subscribe to physical switch position events */
ZMK_LISTENER(osl_listener, osl_listener_cb);
ZMK_SUBSCRIPTION(osl_listener, zmk_position_state_changed);
