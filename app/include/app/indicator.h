/*
 * Copyright (c) 2022-2023 XiNGRZ
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/types.h>
#include <zmk/events/layer_state_changed.h>  // 新增图层事件支持
#include <zmk/keymap.h>                      // 新增图层状态获取支持
struct indicator_settings {
	bool enable;
	uint8_t brightness_active;
	uint8_t brightness_inactive;
};

uint32_t indicator_set_bits(uint32_t bits);

uint32_t indicator_clear_bits(uint32_t bits);

void indicator_set_enable(bool enable);
void indicator_set_brightness_active(uint8_t brightness);
void indicator_set_brightness_inactive(uint8_t brightness);
const struct indicator_settings *indicator_get_settings(void);
int indicator_save_settings(void);

// 新增图层状态声明
extern uint8_t current_layer;  // 暴露当前激活图层
