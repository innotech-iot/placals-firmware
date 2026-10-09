#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

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

void app_main(void){
    init_inputs();
    init_outputs();

    for(;;){
        print_all_inputs();

        for(int i = 0; i < OUTPUTS_PIN_COUNT; i++){
            if(input_is_activate(i)){
                output_activate(i);
            }

            else {
                output_deactivate(i);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
