#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t   fruits_init(void);
bool        fruits_is_touching(void);
uint16_t    fruits_read_pot(void);
int         fruits_active_index(void);
const char *fruits_name(int i);
int         fruits_count(void);
