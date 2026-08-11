# Embedded C Style Guide (Simple Edition)

A short, practical style guide for embedded C.

---

## 1. Naming

- `snake_case` for functions and variables: `motor_set_speed()`, `tick_count`.
- Types end in `_t`: `typedef struct { ... } sensor_reading_t;`
- Constants and macros in `ALL_CAPS`: `#define MAX_RETRIES 3`
- Prefix module-scope (file-static) globals with the module name: `static uint8_t uart_rx_buf[64];`
- Booleans read like questions: `is_ready`, `has_error`, `should_reset`.

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
  /**
   * Set motor speed.
   * speed: 0-100 (%). Values outside range are clamped.
   * Returns STATUS_OK, or STATUS_ERR_TIMEOUT if the driver didn't ack.
   */
  status_t motor_set_speed(uint8_t speed);
  ```
- Flag hacks, dummies, placeholders and workarounds: `/* TODO: remove once errata #42 is fixed */`


## 5. File Structure

- One module = one `.c`/`.h` pair. Header exposes only what's needed, everything else is `static`.
- Header guards:
  ```c
  #ifndef MOTOR_H
  #define MOTOR_H
  ...
  #endif /* MOTOR_H */
  ```
- Order in a `.c` file: includes -> local defines/typedefs -> static variables -> static function prototypes -> public functions -> static function definitions.
