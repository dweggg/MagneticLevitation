# Embedded C Style Guide (Simple Edition)

A short, practical style guide for embedded C.

---

## 1. Naming

- `snake_case` for functions and variables: `motor_set_speed()`, `tick_count`.
- Types end in `_t`: `typedef struct { ... } sensor_reading_t;`
- Constants and macros in `ALL_CAPS`: `#define MAX_RETRIES 3`
- Prefix module-scope (file-static) globals with the module name: `static uint8_t uart_rx_buf[64];`
- Booleans read like questions: `is_ready`, `has_error`, `should_reset`.
- Functions use `module_verb[_object]`: `current_control_init()`, `setpoint_get_i_sp()`.
- Physical quantities use stable symbol prefixes: current `i_`, voltage `v_`, position `x_`, temperature `temp_`, and magnetic flux density `B_`.
- Add a lowercase SI-unit suffix to variables when it clarifies a stored or returned value: `_a`, `_v`, `_m`, `_c`, `_t`, `_w`, `_hz`, `_ms`, `_ns`. Put representation suffixes last, such as `temp_c_q16`; raw ADC counts end in `_adc_raw`.
- Defines use `ALL_CAPS` with a subsystem prefix. Put uppercase unit suffixes before a final representation suffix, for example `FAULT_OVERCURRENT_LIMIT_A_Q16`, `TASK_CURRENT_CONTROL_HZ`, and `USB_CDC_RX_RING_SIZE_BYTES`. Unitless constants omit unit suffixes. Keep names required by external libraries or hardware callbacks unchanged.

## 2. Functions

- One job per function. If you need "and" to describe it, split it.
- Always specify return type and full parameter list with types.

## 3. Control Flow

- Always use braces, even for one-line bodies:
  ```c
  if (ready) {
      start();
  }
  ```
- `switch` statements: always have a `default`, always `break` (or a
  clear `/* fallthrough */` comment if intentional).

## 4. Comments

- Comment *why*, not *what*. The code already says what it does.
- Every public function gets a short header: purpose, parameters, return value, and any preconditions. No need for Doxygen style, but acceptable.
  ```c
  /* Sample a stream now: copies every member variable and queues one frame
   * stamped with the firmware tick. Never blocks; drops (and counts) if the
   * transmit ring cannot take the whole frame. */
  void vars_stream_emit(stream_id_t stream);
  ```
- Flag hacks, dummies, placeholders and workarounds: `/* TODO: remove once errata #42 is fixed */`


## 5. File Structure

- One module = one `.c`/`.h` pair. Header exposes only what's needed, everything else is `static`.
- Header guards:
  ```c
  #ifndef FSM_H
  #define FSM_H

  ...
  #endif /* FSM_H */
  ```
- Order in a `.c` file: includes -> local defines/typedefs -> static variables -> static function prototypes -> public functions -> static function definitions.
