// SPDX-License-Identifier: MIT
#define DT_DRV_COMPAT zmk_behavior_oneshot_layer

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/position-state-changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct osl_cfg {
    uint8_t layer;
    int32_t release_after_ms;
    bool pre_cancel;
};

struct osl_data {
    bool    active;
    uint8_t source_pos;
    int64_t deadline;
};

static int osl_init(const struct device *dev) {
    struct osl_data *data = dev->data;
    data->active = false;
    data->deadline = 0;
    return 0;
}

static int osl_pressed(const struct device *dev,
                       struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct osl_cfg *cfg = dev->config;
    struct osl_data *data = dev->data;

    data->active = true;
    data->source_pos = event.position;
    data->deadline = (cfg->release_after_ms > 0)
                         ? (k_uptime_get() + cfg->release_after_ms)
                         : SYS_FOREVER_MS;

    zmk_keymap_layer_activate(cfg->layer);
    LOG_DBG("osl_pre: activate L%d (src pos=%d)", cfg->layer, data->source_pos);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int osl_released(const struct device *dev,
                        struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    /* Do nothing on release. Timeout/next-press will deactivate. */
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api osl_api = {
    .binding_pressed = osl_pressed,
    .binding_released = osl_released,
};

/* Global listener: pre-cancel on first *other* press, or when deadline passes. */
static int osl_listener_cb(const struct zmk_event_header *eh) {
    int64_t now = k_uptime_get();

    if (!is_zmk_position_state_changed(eh)) return ZMK_EV_EVENT_BUBBLE;
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev->state) return ZMK_EV_EVENT_BUBBLE; /* only on key press */

#define OSL_EACH(n)                                                                 \
    {                                                                               \
        const struct device *dev = DEVICE_DT_INST_GET(n);                           \
        const struct osl_cfg *cfg = dev->config;                                    \
        struct osl_data *data = dev->data;                                          \
        if (!data->active) {                                                        \
            /* Nothing to do for this instance. */                                  \
        } else if ((cfg->pre_cancel && (ev->position != data->source_pos)) ||       \
                   (now >= data->deadline)) {                                       \
            zmk_keymap_layer_deactivate(cfg->layer);                                \
            data->active = false;                                                   \
            LOG_DBG("osl_pre: deactivate L%d (pos=%d, now=%lld, deadline=%lld)",    \
                    cfg->layer, ev->position, now, data->deadline);                 \
        }                                                                           \
    }

    DT_INST_FOREACH_STATUS_OKAY(OSL_EACH)
#undef OSL_EACH

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(osl_pre, osl_listener_cb);
ZMK_SUBSCRIPTION(osl_pre, zmk_position_state_changed);

/* Instantiate behavior devices for all DT instances */
#define OSL_INST(n)                                                                  \
    static struct osl_data osl_data_##n;                                             \
    static const struct osl_cfg osl_cfg_##n = {                                      \
        .layer = DT_INST_PROP(n, param1),                                            \
        .release_after_ms = DT_INST_NODE_HAS_PROP(n, release_after_ms)               \
                                ? DT_INST_PROP(n, release_after_ms)                  \
                                : 800,                                               \
        .pre_cancel = DT_INST_NODE_HAS_PROP(n, pre_cancel),                          \
    };                                                                               \
    BEHAVIOR_DT_INST_DEFINE(n, osl_init, NULL, &osl_data_##n, &osl_cfg_##n,          \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &osl_api);

DT_INST_FOREACH_STATUS_OKAY(OSL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
