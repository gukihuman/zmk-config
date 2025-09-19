// app/src/behaviors/behavior_oneshot_layer.c
// SPDX-License-Identifier: MIT
#define DT_DRV_COMPAT zmk_behavior_oneshot_layer

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* DT props */
struct osl_cfg {
    int32_t release_after_ms; /* 0/absent => no timeout */
};

/* Runtime state (one per DT instance) */
struct osl_data {
    bool    active;           /* armed and layer is ON */
    uint8_t layer;            /* target layer (from binding cell) */
    uint8_t src_pos;          /* physical position that armed it */
    struct k_work_delayable timeout_work;
};

/* --------- helpers --------- */
static void osl_deactivate(struct osl_data *d) {
    if (!d->active) return;
    zmk_keymap_layer_deactivate(d->layer);
    d->active = false;
    k_work_cancel_delayable(&d->timeout_work);
}

static void osl_timeout_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct osl_data *d = CONTAINER_OF(dwork, struct osl_data, timeout_work);
    osl_deactivate(d);
}

/* --------- behavior api (3-arg signatures) --------- */
static int osl_pressed(const struct device *dev,
                       struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct osl_cfg *cfg = dev->config;
    struct osl_data *d = dev->data;

    d->layer   = binding->param1;   /* layer id comes from keymap cell */
    d->src_pos = event.position;
    d->active  = true;

    zmk_keymap_layer_activate(d->layer);

    if (cfg->release_after_ms > 0) {
        k_work_schedule(&d->timeout_work, K_MSEC(cfg->release_after_ms));
    }
    return ZMK_BEHAVIOR_OPAQUE;     /* this behavior emits no keycode by itself */
}

static int osl_released(const struct device *dev,
                        struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(dev); ARG_UNUSED(binding); ARG_UNUSED(event);
    /* release of arming key is irrelevant; we drop after the NEXT key press */
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api osl_api = {
    .binding_pressed  = osl_pressed,
    .binding_released = osl_released,
};

/*
 * Core rule (exactly what you asked):
 * - When the oneshot is PRESSED: turn layer ON.
 * - The VERY NEXT key PRESS (any keycode) should be produced on that layer.
 * - Immediately AFTER that press is produced, turn the layer OFF.
 *
 * Implementation detail:
 * - We listen to keycode_state_changed (post-mapping). On the first keycode
 *   press while armed, we deactivate. That preserves the just-produced
 *   key on the oneshot layer and returns to base for everything after.
 */
static int osl_keycode_listener_cb(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE; /* only on PRESS */

#define OSL_FOR_EACH(n)                                                            \
    {                                                                              \
        const struct device *dev = DEVICE_DT_INST_GET(n);                          \
        struct osl_data *d = dev->data;                                            \
        if (d->active) {                                                           \
            /* This keycode was just produced with the layer ON -> drop now */     \
            osl_deactivate(d);                                                     \
        }                                                                          \
    }

    DT_INST_FOREACH_STATUS_OKAY(OSL_FOR_EACH)
#undef OSL_FOR_EACH

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(osl_keycode_listener, osl_keycode_listener_cb);
ZMK_SUBSCRIPTION(osl_keycode_listener, zmk_keycode_state_changed);

/* --------- init + instances --------- */
static int osl_init(const struct device *dev) {
    struct osl_data *d = dev->data;
    d->active = false;
    d->layer = 0;
    d->src_pos = 0;
    k_work_init_delayable(&d->timeout_work, osl_timeout_cb);
    return 0;
}

/* Register as a BEHAVIOR device */
#define OSL_INST(n)                                                                \
    static struct osl_data osl_data_##n;                                           \
    static const struct osl_cfg osl_cfg_##n = {                                    \
        .release_after_ms = DT_INST_PROP_OR(n, release_after_ms, 800),             \
    };                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n,                                                     \
        osl_init, NULL,                                                            \
        &osl_data_##n, &osl_cfg_##n,                                               \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &osl_api)

DT_INST_FOREACH_STATUS_OKAY(OSL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
