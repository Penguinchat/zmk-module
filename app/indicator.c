/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/drivers/led_strip_remap.h>

#include <zmk/workqueue.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/layer_state_changed.h>  // 新增图层事件支持

#include <app/indicator.h>

#define STRIP_CHOSEN          DT_CHOSEN(zmk_underglow)
#define STRIP_STATUS_LABEL    "STATUS"
#define STRIP_LAYER_LABEL     "LAYER"   // 新增图层灯标签

#define RGB(R, G, B)  ((struct led_rgb){.r = (R), .g = (G), .b = (B)})
#define BRI(rgb, bri) RGB(rgb.r *bri / 255, rgb.g * bri / 255, rgb.b * bri / 255)

// 预设颜色
#define RED      (RGB(0xFF, 0x00, 0x00))
#define GREEN    (RGB(0x00, 0xFF, 0x00))
#define BLUE     (RGB(0x00, 0x00, 0xFF))
#define MAGENTA  (RGB(0xFF, 0x00, 0xFF))
#define CYAN     (RGB(0x00, 0xFF, 0xFF))
#define WHITE    (RGB(0xFF, 0xFF, 0xFF))
#define OFF      (RGB(0x00, 0x00, 0x00))

// 默认图层颜色 (1-6层)
static const struct led_rgb default_layer_colors[] = {
    RED,      // 层1
    GREEN,    // 层2
    BLUE,     // 层3
    MAGENTA,  // 层4
    CYAN,     // 层5
    WHITE,    // 层6
    // 7-16层默认为白色
    WHITE, WHITE, WHITE, WHITE,
    WHITE, WHITE, WHITE, WHITE,
    WHITE, WHITE
};

static const struct device *led_strip;

struct indicator_settings {
    bool enable;
    uint8_t brightness_active;
    uint8_t brightness_inactive;
    struct led_rgb layer_colors[16];  // 存储各层颜色 (索引0-15对应层1-16)
};

static struct indicator_settings settings = {
    .enable = true,
    .brightness_active = CONFIG_HW75_INDICATOR_BRIGHTNESS_ACTIVE,
    .brightness_inactive = CONFIG_HW75_INDICATOR_BRIGHTNESS_INACTIVE,
    .layer_colors = {
        // 初始化时复制默认颜色
        [0] = RED,      [1] = GREEN,    [2] = BLUE,
        [3] = MAGENTA,  [4] = CYAN,     [5] = WHITE,
        // 6-15层
        [6 ... 15] = WHITE
    }
};

static struct k_mutex lock;

static struct led_rgb status_color;
static uint8_t current_layer = 0;  // 当前激活的图层
static bool active = true;          // 系统活动状态

static uint32_t state = 0;          // 其他状态位

// 初始化时复制默认图层颜色
static void init_layer_colors(void)
{
    memcpy(settings.layer_colors, default_layer_colors, sizeof(default_layer_colors));
}

static inline struct led_rgb apply_brightness(struct led_rgb color, uint8_t bri)
{
    return RGB(color.r * bri / 255, color.g * bri / 255, color.b * bri / 255);
}

static void indicator_update(struct k_work *work)
{
    if (!settings.enable) {
        unsigned int key = irq_lock();
        led_strip_remap_clear(led_strip, STRIP_STATUS_LABEL);
        led_strip_remap_clear(led_strip, STRIP_LAYER_LABEL);
        irq_unlock(key);
        return;
    }

    // 更新状态灯
    status_color = state ? RED : GREEN;
    uint8_t bri = active ? settings.brightness_active : settings.brightness_inactive;
    struct led_rgb status_rgb = BRI(status_color, bri);
    
    // 更新图层灯
    struct led_rgb layer_rgb = OFF;
    if (current_layer > 0 && current_layer <= 16) {
        layer_rgb = BRI(settings.layer_colors[current_layer - 1], bri);
    }

    LOG_DBG("Update: Layer %d, Status %02X%02X%02X, Layer %02X%02X%02X",
            current_layer,
            status_rgb.r, status_rgb.g, status_rgb.b,
            layer_rgb.r, layer_rgb.g, layer_rgb.b);

    unsigned int key = irq_lock();
    led_strip_remap_set(led_strip, STRIP_STATUS_LABEL, &status_rgb);
    
    // 仅当图层非0时设置图层灯
    if (current_layer > 0) {
        led_strip_remap_set(led_strip, STRIP_LAYER_LABEL, &layer_rgb);
    } else {
        led_strip_remap_clear(led_strip, STRIP_LAYER_LABEL);
    }
    
    irq_unlock(key);
}

K_WORK_DEFINE(indicator_update_work, indicator_update);

static inline void post_indicator_update(void)
{
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &indicator_update_work);
}

uint32_t indicator_set_bits(uint32_t bits)
{
    k_mutex_lock(&lock, K_FOREVER);
    state |= bits;
    k_mutex_unlock(&lock);
    post_indicator_update();
    return state;
}

uint32_t indicator_clear_bits(uint32_t bits)
{
    k_mutex_lock(&lock, K_FOREVER);
    state &= ~bits;
    k_mutex_unlock(&lock);
    post_indicator_update();
    return state;
}

// 设置当前图层
void indicator_set_layer(uint8_t layer)
{
    k_mutex_lock(&lock, K_FOREVER);
    current_layer = layer;
    k_mutex_unlock(&lock);
    post_indicator_update();
}

// 设置特定图层颜色
void indicator_set_layer_color(uint8_t layer, struct led_rgb color)
{
    if (layer < 1 || layer > 16) return;
    
    k_mutex_lock(&lock, K_FOREVER);
    settings.layer_colors[layer - 1] = color;
    k_mutex_unlock(&lock);
    
    // 如果当前正在显示该图层，立即更新
    if (current_layer == layer) {
        post_indicator_update();
    }
    
    indicator_save_settings();
}

// 重置图层颜色为默认
void indicator_reset_layer_colors(void)
{
    k_mutex_lock(&lock, K_FOREVER);
    init_layer_colors();
    k_mutex_unlock(&lock);
    
    // 如果当前有图层显示，立即更新
    if (current_layer > 0) {
        post_indicator_update();
    }
    
    indicator_save_settings();
}

#ifdef CONFIG_SETTINGS
static int indicator_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg, void *param)
{
    const char *next;
    int ret;

    if (settings_name_steq(name, "settings", &next) && !next) {
        if (len != sizeof(settings)) {
            return -EINVAL;
        }

        ret = read_cb(cb_arg, &settings, sizeof(settings));
        if (ret >= 0) {
            LOG_DBG("Loaded indicator settings");
            return 0;
        }
        return ret;
    }
    
    return -ENOENT;
}

static void indicator_save_settings_work(struct k_work *work)
{
    ARG_UNUSED(work);
    int ret = settings_save_one("app/indicator/settings", &settings, sizeof(settings));
    if (ret != 0) {
        LOG_ERR("Failed saving settings: %d", ret);
    } else {
        LOG_DBG("Saved indicator settings");
    }
}

K_WORK_DELAYABLE_DEFINE(indicator_save_work, indicator_save_settings_work);
#endif

int indicator_save_settings(void)
{
#ifdef CONFIG_SETTINGS
    int ret =
        k_work_reschedule(&indicator_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return MIN(ret, 0);
#else
    return 0;
#endif
}

static void indicator_clear_preview(struct k_work *work)
{
    ARG_UNUSED(work);
    post_indicator_update();
}

K_WORK_DELAYABLE_DEFINE(indicator_clear_preview_work, indicator_clear_preview);

static void indicator_preview_brightness(uint8_t brightness)
{
    // 状态灯预览
    struct led_rgb status_preview = BRI(status_color, brightness);
    
    // 图层灯预览
    struct led_rgb layer_preview = OFF;
    if (current_layer > 0 && current_layer <= 16) {
        layer_preview = BRI(settings.layer_colors[current_layer - 1], brightness);
    }

    unsigned int key = irq_lock();
    led_strip_remap_set(led_strip, STRIP_STATUS_LABEL, &status_preview);
    
    if (current_layer > 0) {
        led_strip_remap_set(led_strip, STRIP_LAYER_LABEL, &layer_preview);
    }
    
    irq_unlock(key);

    k_work_reschedule(&indicator_clear_preview_work, K_MSEC(2000));
}

void indicator_set_enable(bool enable)
{
    settings.enable = enable;
    indicator_save_settings();
    post_indicator_update();
}

void indicator_set_brightness_active(uint8_t brightness)
{
    settings.brightness_active = brightness;
    indicator_save_settings();
    indicator_preview_brightness(brightness);
}

void indicator_set_brightness_inactive(uint8_t brightness)
{
    settings.brightness_inactive = brightness;
    indicator_save_settings();
    indicator_preview_brightness(brightness);
}

const struct indicator_settings *indicator_get_settings(void)
{
    return &settings;
}

static int indicator_event_listener(const zmk_event_t *eh)
{
    // 处理活动状态变化
    struct zmk_activity_state_changed *activity_ev = as_zmk_activity_state_changed(eh);
    if (activity_ev != NULL) {
        active = activity_ev->state == ZMK_ACTIVITY_ACTIVE;
        post_indicator_update();
        return 0;
    }
    
    // 处理图层变化
    struct zmk_layer_state_changed *layer_ev = as_zmk_layer_state_changed(eh);
    if (layer_ev != NULL) {
        // 获取当前激活的最高图层
        uint8_t layer = zmk_keymap_highest_layer_active();
        indicator_set_layer(layer);
        return 0;
    }

    return -ENOTSUP;
}

static int indicator_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    int ret;

    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
    if (!device_is_ready(led_strip)) {
        LOG_ERR("LED strip device not ready");
        return -ENODEV;
    }

#ifdef CONFIG_SETTINGS
    ret = settings_subsys_init();
    if (ret) {
        LOG_ERR("Failed to initialize settings: %d", ret);
    }

    ret = settings_load_subtree_direct("app/indicator", indicator_settings_load_cb, NULL);
    if (ret) {
        LOG_ERR("Failed to load settings: %d", ret);
        // 加载失败时初始化默认图层颜色
        init_layer_colors();
    }
#else
    init_layer_colors();
#endif

    k_mutex_init(&lock);
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &indicator_update_work);

    return 0;
}

ZMK_LISTENER(indicator, indicator_event_listener);
ZMK_SUBSCRIPTION(indicator, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(indicator, zmk_layer_state_changed);  // 订阅图层变化事件

SYS_INIT(indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
