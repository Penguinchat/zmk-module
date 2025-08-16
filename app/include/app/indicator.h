/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/drivers/led_strip.h>  // 包含struct led_rgb定义

#define MAX_LAYERS 16  // 最大支持图层数量

struct indicator_settings {
	bool enable;
	uint8_t brightness_active;
	uint8_t brightness_inactive;
	struct led_rgb layer_colors[MAX_LAYERS];  // 存储各图层颜色配置
};

uint32_t indicator_set_bits(uint32_t bits);
uint32_t indicator_clear_bits(uint32_t bits);

void indicator_set_enable(bool enable);
void indicator_set_brightness_active(uint8_t brightness);
void indicator_set_brightness_inactive(uint8_t brightness);
const struct indicator_settings *indicator_get_settings(void);

// 新增API：设置当前图层
void indicator_set_layer(uint8_t layer);

// 新增API：设置特定图层颜色
void indicator_set_layer_color(uint8_t layer, struct led_rgb color);

// 新增API：重置所有图层颜色为默认值
void indicator_reset_layer_colors(void);
