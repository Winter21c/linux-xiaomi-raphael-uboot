# Front pop-up camera motor: pins identified

The shipped raphael DTB carries a complete `gpio-line-names` table, which names the
pop-up camera's motor signals:

| TLMM gpio | name | role (inferred from the name) |
|---|---|---|
| 9 | MOTOR_DIR | direction |
| 21 | MOTOR_M0 | H-bridge input M0 |
| 33 | MOTOR_M1 | H-bridge input M1 |
| 35 | MOTOR_SLEEP | driver nSLEEP |
| 36 | MOTOR_FAULT | driver nFAULT (input) |
| 130 | MOTOR_EN | motor supply / enable |
| 27 | DIGITAL_HALL1_INT | hall sensor (position) |
| 11 | DIGITAL_HALL1_RSTN | hall reset |

`raphael-pinctrl.dtsi` confirms the same set:

```
motor_gpio_default {
	mux    { pins = "gpio9", "gpio21", "gpio33", "gpio35", "gpio130"; function = "gpio"; };
	config { bias-pull-up; drive-strength = <2>; };
};
```

Measured state on mainline (via `/sys/kernel/debug/gpio`): every motor line is an
**input with pull-down** (`gpio36` reads high through its pull-up), i.e. nothing in
mainline drives them and the H-bridge is asleep.

## Access problem

Neither `/sys/class/gpio` nor the chardev ABI is usable here:

* `/sys/class/gpio/` does not exist (no legacy sysfs class),
* `GPIO_GET_LINEHANDLE_IOCTL` (v1) and `GPIO_V2_GET_LINE_IOCTL` (v2) both return
  `EINVAL` even for a plain input request on the PMIC chip, so the uAPI path is not
  available in this build,
* a kernel module using the legacy `gpio_request(34 + n)` returns `-EPROBE_DEFER`,
  i.e. the global numbering assumption does not hold.

`gpiochip3` is the TLMM (`3100000.pinctrl`, 176 lines); the PMIC chips are
`gpiochip0..2` and the codec is `gpiochip4`.

## Planned approach

Expose the five output lines through a `gpio-leds` node in the DTB (one "led" per
line), which gives `/sys/class/leds/<name>/brightness` control without a custom
driver, then drive the sequence:

1. `motor_en` = 1
2. `motor_sleep` = 1
3. `motor_dir` = 0/1, `motor_m0` = 1, `motor_m1` = 0
4. wait ~700 ms, then `m0 = m1 = 0`, `sleep = 0`

with `motor_fault` and `motor_hall` left as inputs to read back position.
