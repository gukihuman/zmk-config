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
#include <zmk/events/position_state_changed.h>  // NOTE: underscores, not hyphens

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Devicetree-configured props */
struct osl_cfg {
    int32_t release_after_ms;   /* timeout; 0/absent = no timeout */
    bool pre_cancel;            /* if true: cancel on first non-source press */
};

/* Per-instance runtime state */
struct osl_data {
    bool    active;
    uint8_t layer;              /* target layer for this arming */
    uint8_t src_pos;            /* arming key position */
    struct k_work_delayable timeout_work;
};

/* ---- helpers ---- */
static void osl_deactivate(struct osl_data *data) {
    if (!data->active) return;
    zmk_keymap_layer_deactivate(data->layer);
    data->active = false;
    k_work_cancel_delayable(&data->timeout_work);
}

/* timeout -> drop layer */
static void osl_timeout_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct osl_data *data = CONTAINER_OF(dwork, struct osl_data, timeout_work);
    osl_deactivate(data);
}

/* ---- behavior API ---- */
/* NOTE: pressed/released signatures are the 2-arg form; fetch dev via helper */
static int osl_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding);
    const struct osl_cfg *cfg = dev->config;
    struct osl_data *data = dev->data;

    data->active  = true;
    data->src_pos = event.position;
    data->layer   = binding->param1;                   /* layer from keymap cell */

    zmk_keymap_layer_activate(data->layer);

    if (cfg->release_after_ms > 0) {
        k_work_schedule(&data->timeout_work, K_MSEC(cfg->release_after_ms));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int osl_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    /* release ignored; timeout or next press will cancel */
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api osl_api = {
    .binding_pressed  = osl_pressed,
    .binding_released = osl_released,
};

/* ---- global listener: cancel on first non-source press (if pre_cancel) ---- */
static int osl_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE; /* only handle presses */

#define OSL_FOR_EACH_INST(n)                                                         \
    {                                                                                \
        const struct device *dev = DEVICE_DT_INST_GET(n);                            \
        const struct osl_cfg  *cfg = dev->config;                                    \
        struct osl_data       *dat = dev->data;                                      \
        if (dat->active && cfg->pre_cancel && ev->position != dat->src_pos) {        \
            osl_deactivate(dat);                                                     \
        }                                                                            \
    }

    DT_INST_FOREACH_STATUS_OKAY(OSL_FOR_EACH_INST)
#undef OSL_FOR_EACH_INST

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(osl_listener, osl_listener_cb);
ZMK_SUBSCRIPTION(osl_listener, zmk_position_state_changed);

/* ---- init + instances ---- */
static int osl_init(const struct device *dev) {
    struct osl_data *data = dev->data;
    data->active = false;
    k_work_init_delayable(&data->timeout_work, osl_timeout_cb);
    return 0;
}

/* Create one device per DT instance */
#define OSL_INST(n)                                                                \
    static struct osl_data osl_data_##n;                                           \
    static const struct osl_cfg osl_cfg_##n = {                                    \
        .release_after_ms = DT_INST_PROP_OR(n, release_after_ms, 800),             \
        .pre_cancel       = DT_INST_NODE_HAS_PROP(n, pre_cancel),                  \
    };                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n,                                                     \
        osl_init, NULL,                                                            \
        &osl_data_##n, &osl_cfg_##n,                                               \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &osl_api)

DT_INST_FOREACH_STATUS_OKAY(OSL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
