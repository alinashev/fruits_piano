# Optional patch for existing esp-scope main.c

This file is only needed if you want to integrate the fruit piano logic into an
existing `esp-scope` project. The current repository can now be built as a
standalone ESP-IDF app using `main/main.c`.

Add include near the other includes:

```c
#include "fruits.h"
```

Call fruit/buzzer initialization in `app_main()` after the existing hardware/app initialization:

```c
ESP_ERROR_CHECK(fruits_init());
```

Inside `adc_read_task`, in the loop where samples are written to `out_buf`, replace ADC values while a fruit is being touched:

```c
bool touch = fruits_is_touching();
uint16_t pot = touch ? fruits_read_pot() : 0;

for (...) {
    uint32_t val = ADC_GET_DATA(...);

    if (touch) {
        val = pot;
    }

    out_buf[idx++] = val;
}
```

If your loop already has `val` and `idx`, keep the original variable names from esp-scope and only add the `if (touch) val = pot;` line.
