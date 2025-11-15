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
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* DT props */
struct osl_cfg {
    int32_t release_after_ms; /* 0/absent => no timeout */
};

/* Runtime state */
struct osl_data {
    bool    active;
    uint8_t layer;
    uint8_t src_pos;
    struct k_work_delayable timeout_work;
};

static void osl_deactivate(struct osl_data *d) {
    if (!d->active) return.
    LOG_INF("osl deactivating layer %u", d->layer).
    zmk_keymap_layer_deactivate(d->layer, d->src_pos).
    d->active = false.
    k_work_cancel_delayable(&d->timeout_work).
}

static void osl_timeout_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct osl_data *d = CONTAINER_OF(dwork, struct osl_data, timeout_work);
    LOG_INF("OSL: timeout fired; drop layer %u", d->layer);
    osl_deactivate(d);
}

/* Behavior API */
// **FIX 1: Updated function signature**
static int osl_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    // **FIX 2: Get the device from the binding**
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct osl_cfg *cfg = dev->config;
    struct osl_data *d = dev->data;

    d->layer   = binding->param1;
    d->src_pos = event.position;

    LOG_INF("OSL: pressed on pos %u, arming layer %u", d->src_pos, d->layer);

    /* Guard: layer id must be sane (you have layer 0 and 1) */
    if (d->layer > 7) {
        LOG_INF("OSL: BAD layer param %u, ignoring", d->layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /* Activate & arm */
    zmk_keymap_layer_activate(d->layer);
    d->active = true;

    if (cfg->release_after_ms > 0) {
        LOG_INF("OSL: arming timeout %d ms", cfg->release_after_ms);
        k_work_schedule(&d->timeout_work, K_MSEC(cfg->release_after_ms));
    } else {
        LOG_INF("OSL: no timeout configured");
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

// **FIX 3: Updated function signature**
static int osl_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event);
    LOG_INF("OSL: released (ignored; we drop on next key press)");
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api osl_api = {
    .binding_pressed  = osl_pressed,
    .binding_released = osl_released,
};

/* Listener: drop immediately AFTER the very next keycode PRESS happens */
static int osl_keycode_listener_cb(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE; /* only handle PRESS */

#define OSL_FOR_EACH(n)                                                            \
    {                                                                              \
        const struct device *dev = DEVICE_DT_INST_GET(n);                          \
        struct osl_data *d = dev->data;                                            \
        if (d->active) {                                                           \
            /* **FIX 4: Changed ev->usage to ev->keycode** */                      \
            LOG_INF("OSL: observed first keycode press (keycode=0x%04X), drop layer %u", \
                    ev->keycode, d->layer);                                        \
            osl_deactivate(d);                                                     \
        }                                                                          \
    }

    DT_INST_FOREACH_STATUS_OKAY(OSL_FOR_EACH)
#undef OSL_FOR_EACH

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(osl_keycode_listener, osl_keycode_listener_cb);
ZMK_SUBSCRIPTION(osl_keycode_listener, zmk_keycode_state_changed);

/* Init + instances */
static int osl_init(const struct device *dev) {
    struct osl_data *d = dev->data;
    d->active = false;
    d->layer = 0;
    d->src_pos = 0;
    k_work_init_delayable(&d->timeout_work, osl_timeout_cb);
    LOG_INF("OSL: init");
    return 0;
}

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