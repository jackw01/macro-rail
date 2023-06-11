#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#include "ripplecounter_motor.h"

static const uint32_t PinMotorISense = 26;

static const uint32_t PinMotorA = 18;
static const uint32_t PinMotorB = 19;

static const uint32_t PinCounterIn = 16;

static const uint32_t max_pulse_length = 20000;

static uint32_t slice;

static int32_t ripplecounter_position = 0;
static uint32_t ripplecounter_last_time = 0;
static uint32_t ripplecounter_last_dt = 0;

static bool ripplecounter_direction = false;

void encoder_gpio_callback(uint gpio, uint32_t events) {
  if (gpio == PinCounterIn) {
    uint32_t time = time_us_32();
    uint32_t dt = time - ripplecounter_last_time;
    if (ripplecounter_last_dt > max_pulse_length || dt * 5 > ripplecounter_last_dt * 3) {
      if (ripplecounter_direction) ripplecounter_position--;
      else ripplecounter_position++;
    }
    ripplecounter_last_time = time;
    ripplecounter_last_dt = dt;
  }
}

void motor_set_direction(bool direction) {
  ripplecounter_direction = direction;
}

void motor_set_power(uint32_t power) {
  if (ripplecounter_direction) {
    pwm_set_chan_level(slice, pwm_gpio_to_channel(PinMotorA), 8192);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(PinMotorB), 8192 - power);
  } else {
    pwm_set_chan_level(slice, pwm_gpio_to_channel(PinMotorB), 8192);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(PinMotorA), 8192 - power);
  }
}

void ripplecounter_motor_init() {
  adc_init();
  adc_gpio_init(PinMotorISense);

  slice = pwm_gpio_to_slice_num(PinMotorA);
  pwm_set_wrap(slice, 8192);

  gpio_init(PinMotorA);
  gpio_set_function(PinMotorA, GPIO_FUNC_PWM);
  gpio_init(PinMotorB);
  gpio_set_function(PinMotorB, GPIO_FUNC_PWM);
  motor_set_power(0);
  pwm_set_enabled(slice, true);

  gpio_init(PinCounterIn);
  gpio_set_dir(PinCounterIn, GPIO_IN);
  gpio_pull_down(PinCounterIn);

  gpio_set_irq_enabled(PinCounterIn, GPIO_IRQ_EDGE_FALL, true);
  gpio_set_irq_callback(&encoder_gpio_callback);
  irq_set_enabled(IO_IRQ_BANK0, true);
}

int32_t ripplecounter_get_current_ma() {
  adc_select_input(PinMotorISense - 26);
  int32_t raw_mv = (int32_t)adc_read() * 3300 / 4095 - 1650;
  // sense resistor: 0.04 mV/mA, gain: 20
  return raw_mv * 1000 / 800;
}

int32_t ripplecounter_get_position_counts() {
  return ripplecounter_position;
}

void ripplecounter_reset_position() {
  ripplecounter_position = 0;
}
