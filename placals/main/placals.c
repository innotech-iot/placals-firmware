#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

/** ======================================< GPIOS >====================================== */

#define activate_t bool

#define ACTIVATE_IN_HIGH true
#define ACTIVATE_IN_LOW  false

typedef enum {
    SINAL_LAV_LIVRE = 0,
    SINAL_LAV_OCUPADA,
    SINAL_SEC_LIVRE,
    SINAL_SEC_OCUPADA,
    ESTAGIO_1,
    ESTAGIO_2,
    ESTAGIO_3,
    ESTAGIO_4,
    DONGLE_LAV,
    DONGLE_SEC,
    BOTAO_REDE,
    INPUTS_PIN_COUNT
} inputs_num_t;

typedef enum {
    RELE_LAV = 0,
    RELE_SEC,
    LED_ENERGIA,
    LED_WIFI,
    LED_SERVIDOR,
    MCP_RESET,
    OUTPUTS_PIN_COUNT
} outputs_num_t;

static const gpio_num_t inputs_gpio[INPUTS_PIN_COUNT] = {
    [SINAL_LAV_LIVRE]   = GPIO_NUM_15,
    [SINAL_LAV_OCUPADA] = GPIO_NUM_7,
    [SINAL_SEC_LIVRE]   = GPIO_NUM_17,
    [SINAL_SEC_OCUPADA] = GPIO_NUM_16,
    [ESTAGIO_1]         = GPIO_NUM_12,
    [ESTAGIO_2]         = GPIO_NUM_13,
    [ESTAGIO_3]         = GPIO_NUM_14,
    [ESTAGIO_4]         = GPIO_NUM_21,
    [DONGLE_LAV]        = GPIO_NUM_47,
    [DONGLE_SEC]        = GPIO_NUM_48,
    [BOTAO_REDE]        = GPIO_NUM_4
};

static const gpio_num_t outputs_gpio[OUTPUTS_PIN_COUNT] = {
    [RELE_LAV]     = GPIO_NUM_5,
    [RELE_SEC]     = GPIO_NUM_6,
    [LED_ENERGIA]  = GPIO_NUM_18,
    [LED_WIFI]     = GPIO_NUM_10,
    [LED_SERVIDOR] = GPIO_NUM_11,
    [MCP_RESET]    = GPIO_NUM_41
};

typedef struct {
    gpio_num_t gpio;
    activate_t activate;
} IO_t;

IO_t inputs[INPUTS_PIN_COUNT] = { 0 };
IO_t outputs[OUTPUTS_PIN_COUNT] = { 0 };

void init_inputs(){
    for(int i = 0; i < INPUTS_PIN_COUNT; i++){
        gpio_set_direction(inputs_gpio[i], GPIO_MODE_INPUT);
        inputs[i].gpio = inputs_gpio[i];
        inputs[i].activate = i <= ESTAGIO_4 ? ACTIVATE_IN_HIGH : ACTIVATE_IN_LOW;
    }
}

void init_outputs(){
    for(int i = 0; i < OUTPUTS_PIN_COUNT; i++){
        gpio_set_direction(outputs_gpio[i], GPIO_MODE_OUTPUT);
        gpio_set_level(outputs_gpio[i], 0);

        outputs[i].gpio = outputs_gpio[i];
        outputs[i].activate = i != MCP_RESET ? ACTIVATE_IN_HIGH : ACTIVATE_IN_LOW;
    }

    gpio_set_level(outputs[MCP_RESET].gpio, 1);
}

bool gpio_state(gpio_num_t gpio){
    return gpio_get_level(gpio) ? ACTIVATE_IN_HIGH : ACTIVATE_IN_LOW;
}

bool input_is_activate(inputs_num_t in){
    return gpio_state(inputs[in].gpio) == inputs[in].activate;
}

void print_input(inputs_num_t in){
    printf("gpio ( %d ): %s\n", (int)inputs[in].gpio, input_is_activate(in) ? "activate" : "deactivate");
}

void print_all_inputs(){
    printf("\n\n=========< INPUTS >=========\n\n");

    for(int i = 0; i < INPUTS_PIN_COUNT; i++){
        printf("    ");
        print_input(i);
    }

    printf("\n============================\n\n");
}

void output_write(gpio_num_t gpio, bool level){
    gpio_set_level(gpio, level);
}

void output_activate(outputs_num_t out){
    IO_t *output = &outputs[out];
    output_write(output->gpio, output->activate);
}

void output_deactivate(outputs_num_t out){
    IO_t *output = &outputs[out];
    output_write(output->gpio, !output->activate);
}

/** ================================================================================================ */

/** ======================================< Two Wire >====================================== */

#include "driver/i2c_types.h"
#include "driver/i2c_master.h"

#define _TwoWire_STARTED 2077

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    SemaphoreHandle_t xmutex;
    int started;
} TwoWire_mediator_t;

typedef struct {
    i2c_master_dev_handle_t dev;

    /** TX */
    uint8_t *tx;
    size_t   tx_len;

    /** RX */
    uint8_t *rx;
    size_t   rx_len;

    uint32_t timeout_ms;

    esp_err_t result;
    SemaphoreHandle_t done;
} tw_request_t;

static TaskHandle_t tw_task = NULL;
static SemaphoreHandle_t req_mutex;

TwoWire_mediator_t TwoWire = { 0 };
static QueueHandle_t req_queue;

void TwoWire_task(void *params);

bool TwoWire_mediator_start(i2c_master_bus_config_t *i2c_master_bus_cfg){
    if (i2c_master_bus_cfg == NULL) return false;

    TwoWire.started = 0;

    esp_err_t err = i2c_new_master_bus(i2c_master_bus_cfg, &TwoWire.i2c_bus);
    if (err) return false;

    TwoWire.xmutex = xSemaphoreCreateMutex();

    if(TwoWire.xmutex == NULL){
        i2c_del_master_bus(TwoWire.i2c_bus);
        return false;
    }

    req_mutex = xSemaphoreCreateMutex();
    req_queue = xQueueCreate(1, sizeof(tw_request_t*));

    xTaskCreatePinnedToCore(
        TwoWire_task, "TwoWire", 3072,
        NULL, 5, &tw_task, 0
    );

    TwoWire.started = _TwoWire_STARTED;
    return true;
}

bool TwoWire_add_device(i2c_master_dev_handle_t *i2c_dev, i2c_device_config_t *dev_cfg){
    if (TwoWire.started != _TwoWire_STARTED || i2c_dev == NULL || dev_cfg == NULL) return false;

    esp_err_t err = i2c_master_bus_add_device(TwoWire.i2c_bus, dev_cfg, i2c_dev);
    if (err != ESP_OK) return false;

    return true;
}

esp_err_t TwoWire_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *buf, size_t size, int timeout_ms){
    if (TwoWire.started != _TwoWire_STARTED || i2c_dev == NULL || buf == NULL || !size) return ESP_ERR_INVALID_ARG;

    if(!xSemaphoreTake(TwoWire.xmutex, pdMS_TO_TICKS(20000)))
        return ESP_ERR_TIMEOUT;

    esp_err_t err = i2c_master_transmit(
        i2c_dev, buf, size,
        pdMS_TO_TICKS(timeout_ms)
    );

    if(err != ESP_OK){
        xSemaphoreGive(TwoWire.xmutex);
        return err;
    }

    err = i2c_master_bus_wait_all_done(TwoWire.i2c_bus, pdMS_TO_TICKS(100));
    xSemaphoreGive(TwoWire.xmutex);

    return err;
}

esp_err_t TwoWire_transmit_receive(
    i2c_master_dev_handle_t i2c_dev, 
    const uint8_t *tx_buf, size_t tx_size, 
    uint8_t *rx_buf, size_t rx_size, 
    int timeout_ms
){
    if(TwoWire.started != _TwoWire_STARTED || i2c_dev == NULL || tx_buf == NULL || !tx_size || rx_buf == NULL || !rx_size)
        return ESP_ERR_INVALID_ARG;

    if(!xSemaphoreTake(TwoWire.xmutex, pdMS_TO_TICKS(20000)))
        return ESP_ERR_TIMEOUT;

    esp_err_t err = i2c_master_transmit_receive(
        i2c_dev, tx_buf, tx_size,
        rx_buf, rx_size,
        pdMS_TO_TICKS(timeout_ms)
    );

    if(err != ESP_OK){
        xSemaphoreGive(TwoWire.xmutex);
        return err;
    }

    err = i2c_master_bus_wait_all_done(TwoWire.i2c_bus, pdMS_TO_TICKS(100));
    xSemaphoreGive(TwoWire.xmutex);

    return err;
}

esp_err_t TwoWire_remove_device(i2c_master_dev_handle_t i2c_dev){
    if (TwoWire.started != _TwoWire_STARTED || i2c_dev == NULL) return ESP_ERR_INVALID_ARG;

    esp_err_t err = i2c_master_bus_rm_device(i2c_dev);
    if (err != ESP_OK) return err;
    
    return ESP_OK;
}

esp_err_t TwoWire_submit(tw_request_t *r){
    if (!r || !r->dev) return ESP_ERR_INVALID_ARG;

    r->done = xSemaphoreCreateBinary();
    if (!r->done) return ESP_ERR_NO_MEM;

    xQueueSend(req_queue, &r, portMAX_DELAY);

    if(xSemaphoreTake(r->done, pdMS_TO_TICKS(r->timeout_ms)) == pdFALSE){
        vSemaphoreDelete(r->done);
        return ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(r->done);
    return r->result;
}

esp_err_t TwoWire_mediator_delete(){
    if (!TwoWire.started) return ESP_ERR_INVALID_ARG;

    vTaskDelete(tw_task);

    vSemaphoreDelete(req_mutex);
    vSemaphoreDelete(TwoWire.xmutex);
    i2c_del_master_bus(TwoWire.i2c_bus);

    TwoWire.started = 0;
    return ESP_OK;
}

void TwoWire_task(void *arg){
    tw_request_t *r;

    for(;;){
        if(!xQueueReceive(req_queue, &r, pdMS_TO_TICKS(10000))){
            printf("nothing recv in queue\n");
            continue;
        }

        if(r->rx && r->rx_len){
            // printf("Before I2C transmit_receive\n");
            r->result = TwoWire_transmit_receive(
                r->dev, r->tx, r->tx_len,
                r->rx, r->rx_len,
                r->timeout_ms
            );
            // printf("After I2C transmit_receive\n");
        } 
        
        else {
            // printf("Before I2C transmit\n");
            r->result = TwoWire_transmit(
                r->dev, r->tx, r->tx_len,
                r->timeout_ms
            );
            // printf("After I2C transmit\n");
        }

        if(r->done == NULL)
            printf("r->done is null\n");

        xSemaphoreGive(r->done);
    }
}

/** ======================================< mcp23017 >====================================== */

/* Registradores MCP23017 — BANK = 0 */

#define REG_IODIRA       0x00
#define REG_IPOLA        0x02
#define REG_GPINTENA     0x04
#define REG_IOCON        0x0A
#define REG_GPPUA        0x0C
#define REG_GPIOA        0x12
#define REG_OLATA        0x14

#define I2C_TIMEOUT_MS   100

#define MCP23017_DEFAULT_ADDR 0x20
#define MCP23017_GPIO_COUNT   16

typedef enum {
    MCP23017_GPIO_A0 = 0,
    MCP23017_GPIO_A1,
    MCP23017_GPIO_A2,
    MCP23017_GPIO_A3,
    MCP23017_GPIO_A4,
    MCP23017_GPIO_A5,
    MCP23017_GPIO_A6,
    MCP23017_GPIO_A7,

    MCP23017_GPIO_B0,
    MCP23017_GPIO_B1,
    MCP23017_GPIO_B2,
    MCP23017_GPIO_B3,
    MCP23017_GPIO_B4,
    MCP23017_GPIO_B5,
    MCP23017_GPIO_B6,
    MCP23017_GPIO_B7
} mcp23017_gpio_t;

typedef enum {
    MCP23017_MODE_OUTPUT = 0,
    MCP23017_MODE_INPUT
} mcp23017_mode_t;

typedef struct {
    i2c_master_dev_handle_t handle;

    uint16_t iodir;
    uint16_t olat;
    uint16_t gppu;

    uint8_t address;
    bool initialized;
} mcp23017_t;

static bool valid_device(const mcp23017_t *dev){
    return dev != NULL && dev->initialized;
}

static bool valid_pin(mcp23017_gpio_t pin){
    return (unsigned)pin < MCP23017_GPIO_COUNT;
}

static esp_err_t write_reg(mcp23017_t *dev, uint8_t reg, uint8_t value){
    uint8_t tx[2] = { reg, value };
    return TwoWire_transmit(dev->handle, tx, sizeof(tx), I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(mcp23017_t *dev, uint8_t reg, uint8_t *value){
    return TwoWire_transmit_receive(
        dev->handle,
        &reg,
        sizeof(reg),
        value,
        sizeof(*value),
        I2C_TIMEOUT_MS
    );
}

static esp_err_t write_reg16(mcp23017_t *dev, uint8_t reg, uint16_t value){
    uint8_t tx[3] = {
        reg,
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };

    return TwoWire_transmit(
        dev->handle,
        tx,
        sizeof(tx),
        I2C_TIMEOUT_MS
    );
}

static esp_err_t read_reg16(mcp23017_t *dev, uint8_t reg, uint16_t *value){
    uint8_t rx[2];

    esp_err_t err = TwoWire_transmit_receive(
        dev->handle,
        &reg,
        sizeof(reg),
        rx,
        sizeof(rx),
        I2C_TIMEOUT_MS
    );

    if (err != ESP_OK) {
        return err;
    }

    *value = (uint16_t)rx[0]
           | ((uint16_t)rx[1] << 8);

    return ESP_OK;
}

esp_err_t mcp23017_init(mcp23017_t *dev, uint8_t address, uint32_t scl_speed_hz){
    if (dev == NULL ||
        address < 0x20 ||
        address > 0x27 ||
        scl_speed_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz
    };


    if (!TwoWire_add_device(&dev->handle, &cfg)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    dev->address = address;
    esp_err_t err = write_reg(dev, REG_IOCON, 0x00);

    if (err != ESP_OK) {
        goto fail;
    }

    dev->iodir = 0xFFFF;
    dev->olat  = 0x0000;
    dev->gppu  = 0x0000;

    err = write_reg16(dev, REG_OLATA, dev->olat);

    if (err != ESP_OK) {
        goto fail;
    }

    err = write_reg16(dev, REG_IODIRA, dev->iodir);

    if (err != ESP_OK) {
        goto fail;
    }

    err = write_reg16(dev, REG_GPPUA, dev->gppu);

    if (err != ESP_OK) {
        goto fail;
    }

    err = write_reg16(dev, REG_GPINTENA, 0x0000);

    if (err != ESP_OK) {
        goto fail;
    }

    dev->initialized = true;

    return ESP_OK;

fail:
    TwoWire_remove_device(dev->handle);
    memset(dev, 0, sizeof(*dev));

    return err;
}

esp_err_t mcp23017_deinit(mcp23017_t *dev){
    if (!valid_device(dev)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = TwoWire_remove_device(dev->handle);

    if (err != ESP_OK) {
        return err;
    }

    memset(dev, 0, sizeof(*dev));

    return ESP_OK;
}

esp_err_t mcp23017_set_mode(mcp23017_t *dev, mcp23017_gpio_t pin, mcp23017_mode_t mode){
    if (!valid_device(dev) ||
        !valid_pin(pin) ||
        (mode != MCP23017_MODE_INPUT &&
         mode != MCP23017_MODE_OUTPUT)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t mask = (uint16_t)(1U << pin);
    uint16_t previous = dev->iodir;

    if (mode == MCP23017_MODE_INPUT) {
        dev->iodir |= mask;
    } else {
        dev->iodir &= (uint16_t)~mask;
    }

    esp_err_t err = write_reg16(
        dev,
        REG_IODIRA,
        dev->iodir
    );

    if (err != ESP_OK) {
        dev->iodir = previous;
    }

    return err;
}

esp_err_t mcp23017_set_pullup(mcp23017_t *dev, mcp23017_gpio_t pin, bool enable){
    if (!valid_device(dev) || !valid_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t mask = (uint16_t)(1U << pin);
    uint16_t previous = dev->gppu;

    if (enable) {
        dev->gppu |= mask;
    } else {
        dev->gppu &= (uint16_t)~mask;
    }

    esp_err_t err = write_reg16(
        dev,
        REG_GPPUA,
        dev->gppu
    );

    if (err != ESP_OK) {
        dev->gppu = previous;
    }

    return err;
}

esp_err_t mcp23017_write(mcp23017_t *dev, mcp23017_gpio_t pin, bool level){
    if (!valid_device(dev) || !valid_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t mask = (uint16_t)(1U << pin);
    uint16_t previous = dev->olat;

    if (level) {
        dev->olat |= mask;
    } else {
        dev->olat &= (uint16_t)~mask;
    }

    esp_err_t err = write_reg16(
        dev,
        REG_OLATA,
        dev->olat
    );

    if (err != ESP_OK) {
        dev->olat = previous;
    }

    return err;
}

esp_err_t mcp23017_read(mcp23017_t *dev, mcp23017_gpio_t pin, bool *level){
    if (!valid_device(dev) ||
        !valid_pin(pin) ||
        level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = (pin < MCP23017_GPIO_B0)
        ? REG_GPIOA
        : REG_GPIOA + 1;

    uint8_t value;

    esp_err_t err = read_reg(dev, reg, &value);

    if (err != ESP_OK) {
        return err;
    }

    uint8_t bit = (uint8_t)(pin % 8);

    *level = ((value >> bit) & 1U) != 0;

    return ESP_OK;
}

esp_err_t mcp23017_read_port(mcp23017_t *dev, uint8_t port, uint8_t *value){
    if (!valid_device(dev) ||
        port > 1 ||
        value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return read_reg(
        dev,
        REG_GPIOA + port,
        value
    );
}

esp_err_t mcp23017_write_port(mcp23017_t *dev, uint8_t port, uint8_t value){
    if (!valid_device(dev) || port > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t previous = dev->olat;

    if (port == 0) {
        dev->olat = (dev->olat & 0xFF00U) | value;
    } else {
        dev->olat = (dev->olat & 0x00FFU)
                  | ((uint16_t)value << 8);
    }

    esp_err_t err = write_reg16(
        dev,
        REG_OLATA,
        dev->olat
    );

    if (err != ESP_OK) {
        dev->olat = previous;
    }

    return err;
}

esp_err_t mcp23017_set_polarity(mcp23017_t *dev, uint16_t invert){
    if (!valid_device(dev)) {
        return ESP_ERR_INVALID_ARG;
    }

    return write_reg16(
        dev,
        REG_IPOLA,
        invert
    );
}

esp_err_t mcp23017_set_interrupts(mcp23017_t *dev, uint16_t mask){
    if (!valid_device(dev)) {
        return ESP_ERR_INVALID_ARG;
    }

    return write_reg16(
        dev,
        REG_GPINTENA,
        mask
    );
}

mcp23017_t mcp = { 0 };

void mcp23017_toggle_gpa(bool level){
    for (mcp23017_gpio_t pin = MCP23017_GPIO_A1;
         pin <= MCP23017_GPIO_A7;
         pin++) {

        mcp23017_write(&mcp, pin, level);
    }
}

/** ======================================< DS3231 >====================================== */

// #define DS3231_DEFAULT_ADDR  0x68

// #define REG_SECONDS      0x00
// #define REG_CONTROL      0x0E
// #define REG_STATUS       0x0F
// #define REG_TEMP_MSB     0x11

// #define STATUS_OSF       (1U << 7)

// #define DS3231_TIMEOUT_MS 100

// static bool valid_device(const ds3231_t *dev)
// {
//     return dev != NULL && dev->initialized;
// }

// static uint8_t bin_to_bcd(uint8_t value)
// {
//     return (uint8_t)(((value / 10U) << 4) | (value % 10U));
// }

// static uint8_t bcd_to_bin(uint8_t value)
// {
//     return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
// }

// static esp_err_t ds3231_write_reg(
//     ds3231_t *dev,
//     uint8_t reg,
//     uint8_t value
// )
// {
//     uint8_t tx[2] = {
//         reg,
//         value
//     };

//     return TwoWire_transmit(
//         dev->handle,
//         tx,
//         sizeof(tx),
//         DS3231_TIMEOUT_MS
//     );
// }

// static esp_err_t read_regs(
//     ds3231_t *dev,
//     uint8_t reg,
//     uint8_t *buffer,
//     size_t length
// )
// {
//     if (buffer == NULL || length == 0) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     return TwoWire_transmit_receive(
//         dev->handle,
//         &reg,
//         sizeof(reg),
//         buffer,
//         length,
//         DS3231_TIMEOUT_MS
//     );
// }

// /* ============================================================
//  * Inicialização
//  * ============================================================ */

// esp_err_t ds3231_init(
//     ds3231_t *dev,
//     uint8_t address,
//     uint32_t scl_speed_hz
// )
// {
//     if (dev == NULL ||
//         address > 0x7F ||
//         scl_speed_hz == 0) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     memset(dev, 0, sizeof(*dev));

//     i2c_device_config_t cfg = {
//         .dev_addr_length = I2C_ADDR_BIT_LEN_7,
//         .device_address = address,
//         .scl_speed_hz = scl_speed_hz
//     };

//     esp_err_t err = TwoWire_add_device(
//         &dev->handle,
//         &cfg
//     );

//     if (err != ESP_OK) {
//         return err;
//     }

//     dev->address = address;
//     dev->initialized = true;

//     /*
//      * Não altera a hora, os alarmes ou os registradores
//      * de controle durante a inicialização.
//      */

//     return ESP_OK;
// }

// /* ============================================================
//  * Finalização
//  * ============================================================ */

// esp_err_t ds3231_deinit(ds3231_t *dev)
// {
//     if (!valid_device(dev)) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     esp_err_t err = TwoWire_remove_device(dev->handle);

//     if (err != ESP_OK) {
//         return err;
//     }

//     memset(dev, 0, sizeof(*dev));

//     return ESP_OK;
// }

// /* ============================================================
//  * Leitura de data e hora
//  * ============================================================ */

// esp_err_t ds3231_get_datetime(
//     ds3231_t *dev,
//     ds3231_datetime_t *datetime
// )
// {
//     if (!valid_device(dev) || datetime == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t reg[7];

//     esp_err_t err = read_regs(
//         dev,
//         REG_SECONDS,
//         reg,
//         sizeof(reg)
//     );

//     if (err != ESP_OK) {
//         return err;
//     }

//     /* Segundos: bit 7 indica CH em outros RTCs,
//        mas no DS3231 os bits de segundos são BCD. */
//     datetime->second = bcd_to_bin(reg[0] & 0x7F);
//     datetime->minute = bcd_to_bin(reg[1] & 0x7F);

//     /* O registrador de horas pode estar em modo 12 h ou 24 h. */
//     if (reg[2] & (1U << 6)) {
//         uint8_t hour = bcd_to_bin(reg[2] & 0x1F);
//         bool pm = (reg[2] & (1U << 5)) != 0;

//         /* Converte de 12 h para 24 h. */
//         datetime->hour = (uint8_t)(
//             (hour % 12U) + (pm ? 12U : 0U)
//         );
//     } else {
//         datetime->hour = bcd_to_bin(reg[2] & 0x3F);
//     }

//     datetime->weekday = bcd_to_bin(reg[3] & 0x07);
//     datetime->day = bcd_to_bin(reg[4] & 0x3F);

//     datetime->month = bcd_to_bin(reg[5] & 0x1F);
//     datetime->year = (uint16_t)(2000U + bcd_to_bin(reg[6]));

//     if (reg[5] & (1U << 7)) {
//         datetime->year += 100U;
//     }

//     return ESP_OK;
// }

// /* ============================================================
//  * Ajuste de data e hora
//  * ============================================================ */

// esp_err_t ds3231_set_datetime(
//     ds3231_t *dev,
//     const ds3231_datetime_t *datetime
// )
// {
//     if (!valid_device(dev) || datetime == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     if (datetime->year < 2000 || datetime->year > 2199 ||
//         datetime->month < 1 || datetime->month > 12 ||
//         datetime->day < 1 || datetime->day > 31 ||
//         datetime->weekday < 1 || datetime->weekday > 7 ||
//         datetime->hour > 23 ||
//         datetime->minute > 59 ||
//         datetime->second > 59) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t tx[8] = {
//         REG_SECONDS,
//         bin_to_bcd(datetime->second),
//         bin_to_bcd(datetime->minute),
//         bin_to_bcd(datetime->hour),
//         bin_to_bcd(datetime->weekday),
//         bin_to_bcd(datetime->day),
//         (uint8_t)(
//             bin_to_bcd(datetime->month) |
//             (datetime->year >= 2100 ? 0x80 : 0x00)
//         ),
//         bin_to_bcd((uint8_t)(datetime->year % 100))
//     };

//     return TwoWire_transmit(
//         dev->handle,
//         tx,
//         sizeof(tx),
//         DS3231_TIMEOUT_MS
//     );
// }

// /* ============================================================
//  * Temperatura
//  * ============================================================ */

// esp_err_t ds3231_get_temperature(
//     ds3231_t *dev,
//     float *temperature
// )
// {
//     if (!valid_device(dev) || temperature == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t reg[2];

//     esp_err_t err = read_regs(
//         dev,
//         REG_TEMP_MSB,
//         reg,
//         sizeof(reg)
//     );

//     if (err != ESP_OK) {
//         return err;
//     }

//     int8_t integer = (int8_t)reg[0];
//     uint8_t fraction = (reg[1] >> 6) & 0x03;

//     *temperature = (float)integer + ((float)fraction * 0.25f);

//     return ESP_OK;
// }

// /* ============================================================
//  * Oscillator Stop Flag
//  * ============================================================ */

// esp_err_t ds3231_oscillator_stopped(
//     ds3231_t *dev,
//     bool *stopped
// )
// {
//     if (!valid_device(dev) || stopped == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t status;

//     esp_err_t err = read_regs(
//         dev,
//         REG_STATUS,
//         &status,
//         sizeof(status)
//     );

//     if (err != ESP_OK) {
//         return err;
//     }

//     *stopped = (status & STATUS_OSF) != 0;

//     return ESP_OK;
// }

// esp_err_t ds3231_clear_oscillator_flag(ds3231_t *dev){
//     if (!valid_device(dev)) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     uint8_t status;

//     esp_err_t err = read_regs(
//         dev,
//         REG_STATUS,
//         &status,
//         sizeof(status)
//     );

//     if (err != ESP_OK) {
//         return err;
//     }

//     status &= (uint8_t)~STATUS_OSF;

//     return ds3231_write_reg(dev, REG_STATUS, status);
// }

// typedef struct {
//     uint16_t year;       /* 2000–2199 */
//     uint8_t month;       /* 1–12 */
//     uint8_t day;         /* 1–31 */
//     uint8_t weekday;     /* 1–7 */
//     uint8_t hour;        /* 0–23 */
//     uint8_t minute;      /* 0–59 */
//     uint8_t second;      /* 0–59 */
// } ds3231_datetime_t;

// typedef struct {
//     i2c_master_dev_handle_t handle;
//     uint8_t address;
//     bool initialized;
// } ds3231_t;

// ds3231_t rtc = { 0 };

void app_main(void){
    init_inputs();
    init_outputs();

    i2c_master_bus_config_t i2c_master_bus_cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,

        .glitch_ignore_cnt = 7,
        .intr_priority     = 2,
        .trans_queue_depth = 8,

        .flags = {
            .enable_internal_pullup = true
        }
    };

    if(!TwoWire_mediator_start(&i2c_master_bus_cfg)){
        for(;;){
            printf("[-]critical error ( TwoWire_mediator start )\n");
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t err = mcp23017_init(&mcp, MCP23017_DEFAULT_ADDR, 100000);

    if(err != ESP_OK){
        printf("[-] MCP23017 init failed: %s\n", esp_err_to_name(err));

        for(;;){
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    bool gpa_level = false;

    // err = ds3231_init(&rtc, DS3231_DEFAULT_ADDR, 100000);

    // if(err != ESP_OK){
    //     printf("[-] DS3231 initialization failed: %s\n",
    //            esp_err_to_name(err));
    //     return;
    // }

    // bool stopped;
    // err = ds3231_oscillator_stopped(&rtc, &stopped);

    // if(err != ESP_OK){
    //     printf("[-] Failed to read oscillator status: %s\n", esp_err_to_name(err));
    // } 
    
    // else if(stopped){
    //     printf("[!] RTC oscillator stopped; time may be invalid\n");
    // }

    for(;;){
        // ds3231_datetime_t datetime;
        // err = ds3231_get_datetime(&rtc, &datetime);

        // if(err == ESP_OK){
        //     printf(
        //         "%04u-%02u-%02u %02u:%02u:%02u | Weekday: %u\n",
        //         (unsigned)datetime.year,
        //         (unsigned)datetime.month,
        //         (unsigned)datetime.day,
        //         (unsigned)datetime.hour,
        //         (unsigned)datetime.minute,
        //         (unsigned)datetime.second,
        //         (unsigned)datetime.weekday
        //     );
        // } 
        
        // else {
        //     printf("[-] RTC read error: %s\n", esp_err_to_name(err));
        // }

        print_all_inputs();

        // for(int i = 0; i < MCP_RESET; i++){
        //     if(input_is_activate(i)){
        //         output_activate(i);
        //     }

        //     else {
        //         output_deactivate(i);
        //     }

        // }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        mcp23017_toggle_gpa(gpa_level);
        gpa_level = !gpa_level;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
