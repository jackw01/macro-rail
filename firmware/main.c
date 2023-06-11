#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "ripplecounter_motor.h"

static const uint32_t PinLimitSwitch = 15;
static const uint32_t PinShutter = 14;

static const uint32_t TickIntervalUs = 1000;

static const uint32_t MotorCurrentMeasurementIntervalMs = 100;
static const uint32_t PreShutterDelayMs = 100;
static const uint32_t ShutterPulseDelayMs = 100;
static const uint32_t MaxTravelDistanceUm = 85000;

enum SystemState {
    SystemStateInit = 0,
    SystemStateHoming,
    SystemStateIdle,
    SystemStateStartMove,
    SystemStateMoving,
    SystemStateShutterActuation
};

uint32_t system_state = SystemStateInit;
uint32_t saved_state = SystemStateIdle;
bool is_paused = false;

uint32_t program_step = 0;
uint32_t program_delay = 0;
uint32_t program_count = 0;

int32_t program_start_position;
uint32_t program_counter = 0;
uint32_t program_timer = 0;

uint32_t motor_current_measurement_timer = 1;
int32_t last_position = 0;

// Camera

void camera_activate_shutter() {
  gpio_put(PinShutter, true);
  sleep_ms(ShutterPulseDelayMs);
  gpio_put(PinShutter, false);
}

// UART

static const char PacketStartByte = 254;
static const char PacketEndByte = 255;

enum CmdType {
    CmdTypeStop = 0,
    CmdTypeForward,
    CmdTypeReverse,
    CmdTypeShutter,
    CmdTypeStart,
    CmdTypePause,
    CmdTypeReset,
    CmdTypeMax
};

char packet_type;
char incoming_packet[128];
uint32_t packet_index = 0;
uint32_t packet_end;

void handle_uart_packet() {
  if (packet_type == CmdTypeStop) {
    motor_set_power(0);
  } else if (packet_type == CmdTypeForward) {
    motor_set_direction(true);
    motor_set_power(8000);
  } else if (packet_type == CmdTypeReverse) {
    motor_set_direction(false);
    motor_set_power(8000);
  } else if (packet_type == CmdTypeShutter) {
    camera_activate_shutter();
  } else if (packet_type == CmdTypeStart) {
    if (system_state == SystemStateIdle) {
      program_step = incoming_packet[0] << 24 |
                     incoming_packet[1] << 16 |
                     incoming_packet[2] << 8 |
                     incoming_packet[3];
      program_delay = incoming_packet[4] << 24 |
                      incoming_packet[5] << 16 |
                      incoming_packet[6] << 8 |
                      incoming_packet[7];
      program_count = incoming_packet[8];

      program_start_position = last_position;
      program_counter = 1;
      program_timer = 0;

      system_state = SystemStateStartMove;
      is_paused = false;
    }
  } else if (packet_type == CmdTypePause) {
    is_paused = !is_paused;
    if (is_paused) {
      motor_set_power(0);
      saved_state = system_state;
      system_state = SystemStateIdle;
    } else {
      if (saved_state == SystemStateMoving) saved_state = SystemStateStartMove;
      system_state = saved_state;
    }
  } else if (packet_type == CmdTypeReset) {
    system_state = SystemStateInit;
    is_paused = false;
  }
}

void handle_uart_input(char c) {
  if (packet_index == 0 && c == PacketStartByte) {
    // Packet start
    packet_index++;
  } else if (packet_index == 1) {
    // Packet type
    if (c < CmdTypeMax) {
      packet_type = c;
      packet_index++;
    } else packet_index = 0;
  } else if (packet_index == 2) {
      // Data Length
      packet_end = c + 2;
      packet_index++;
  } else if (packet_index >= 3 && packet_index < packet_end && packet_index - 3 < sizeof(incoming_packet) - 1) {
    // Data start
    incoming_packet[packet_index - 3] = c;
    packet_index++;
  } else {
    // End
    incoming_packet[packet_index - 3] = c;
    incoming_packet[packet_index - 2] = 0;
    handle_uart_packet();
    packet_index = 0;
  }
}

enum MessageType {
    MessageTypePosition = 0,
    MessageTypeCurrent = 1,
    MessageTypeDone = 2
};

void write_uart_message(char type, int32_t value) {
  putchar_raw(PacketStartByte);
  putchar_raw(type);
  putchar_raw((value >> 24) & 0xff);
  putchar_raw((value >> 16) & 0xff);
  putchar_raw((value >> 8) & 0xff);
  putchar_raw(value & 0xff);
  putchar_raw(PacketEndByte);
}

// Utility

int32_t position_counts_to_um(int32_t counts) {
  // 53.33 * 3 * 2 * 2
  return counts * 10000 / 6399;
}

// Main loop

void tick(uint32_t millis, uint32_t dt_micros) {
  while (true) {
    int32_t c = getchar_timeout_us(0);
    if (c != PICO_ERROR_TIMEOUT) handle_uart_input(c);
    else break;
  }

  if (millis >= motor_current_measurement_timer) {
    write_uart_message(MessageTypeCurrent, ripplecounter_get_current_ma());
    motor_current_measurement_timer = millis + MotorCurrentMeasurementIntervalMs;
  }

  int32_t position = position_counts_to_um(ripplecounter_get_position_counts());

  if (system_state == SystemStateInit) {
    motor_set_direction(true);
    motor_set_power(8000);
    system_state = SystemStateHoming;
  } else if (system_state == SystemStateHoming) {
    if (gpio_get(PinLimitSwitch)) {
      motor_set_power(0);
      system_state = SystemStateIdle;
      ripplecounter_reset_position();
    }
  } else if (system_state == SystemStateIdle) {

  } else if (system_state == SystemStateStartMove) {
    motor_set_direction(false);
    motor_set_power(8000);
    system_state = SystemStateMoving;
  } else if (system_state == SystemStateMoving) {
    if (position > program_start_position + (int32_t)(program_step * program_counter)) {
      if (program_timer == 0) {
        motor_set_power(0);
        program_timer = millis + PreShutterDelayMs;
      } else if (millis >= program_timer) {
        program_timer = 0;
        system_state = SystemStateShutterActuation;
      }
    }
  } else if (system_state == SystemStateShutterActuation) {
    if (program_timer == 0) {
        camera_activate_shutter();
        program_timer = millis + ShutterPulseDelayMs + program_delay;
      } else if (millis >= program_timer) {
        program_timer = 0;
        if (++program_counter == program_count + 1) {
          system_state = SystemStateIdle;
          write_uart_message(MessageTypeDone, 0);
        } else {
          system_state = SystemStateStartMove;
        }
      }
  }

  if (position != last_position) write_uart_message(MessageTypePosition, position);
  last_position = position;
}

int main() {
  stdio_init_all();

  gpio_init(PinLimitSwitch);
  gpio_set_dir(PinLimitSwitch, GPIO_IN);
  gpio_pull_up(PinLimitSwitch);

  gpio_init(PinShutter);
  gpio_set_dir(PinShutter, GPIO_OUT);
  gpio_put(PinShutter, false);

  ripplecounter_motor_init();

  uint64_t micros;
  uint32_t dt;

  while (true) {
    micros = time_us_64();
    tick(us_to_ms(micros), dt);
    dt = time_us_64() - micros;
    if (dt < TickIntervalUs) {
      absolute_time_t target;
      update_us_since_boot(&target, micros + TickIntervalUs);
      busy_wait_until(target);
    }
  }
}
