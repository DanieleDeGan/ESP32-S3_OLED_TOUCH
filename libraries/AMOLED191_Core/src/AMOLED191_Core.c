#include "AMOLED191_Core.h"

#include "driver/i2c.h"
#include "esp_err.h"

void Core_I2CBusInit(void)
{
    static bool s_ready = false;
    if (s_ready) {
        return;
    }

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = (gpio_num_t)AMOLED191_CORE_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = (gpio_num_t)AMOLED191_CORE_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AMOLED191_CORE_I2C_CLOCK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config((i2c_port_t)AMOLED191_CORE_I2C_PORT, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install((i2c_port_t)AMOLED191_CORE_I2C_PORT, i2c_conf.mode, 0, 0, 0));

    s_ready = true;
}
