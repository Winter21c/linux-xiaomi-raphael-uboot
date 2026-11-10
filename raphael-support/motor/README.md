# Front pop-up camera motor

## Pins (from the shipped DTB's `gpio-line-names` + `motor_gpio_default` pinctrl)

| TLMM gpio | name | use |
|---|---|---|
| 9 | MOTOR_DIR | direction |
| 21 | MOTOR_M0 | H-bridge input 0 |
| 33 | MOTOR_M1 | H-bridge input 1 |
| 35 | MOTOR_SLEEP | driver nSLEEP |
| 36 | MOTOR_FAULT | driver nFAULT (**input**, high = ok) |
| 130 | MOTOR_EN | motor enable |
| 27 | DIGITAL_HALL1_INT | hall feedback (**input**) |
| 11 | DIGITAL_HALL1_RSTN | hall reset |

## How the pins are exposed

Neither `/sys/class/gpio` nor the chardev uAPI works in this kernel build (both
line-request ioctls return EINVAL), so the five outputs are exposed through a
`gpio-leds` node added to the DTB:

```
/sys/class/leds/motor:{en,sleep,dir,m0,m1}/brightness
```

The node was added to the working DTB source and the resulting blob is
`raphael-with-motor.dtb` (installed as `/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb`).
Display and modem were re-verified after booting it (`drm=5`, `modem=running`).

## Drive sequence that works at the pin level

```
en=1, sleep=1            # wake the H-bridge
dir=<0|1>, m0=1, m1=0    # or m0=0, m1=1
sleep <duration>
m0=0, m1=0               # stop
sleep=0, en=0
```

Verified with `/sys/kernel/debug/gpio`: `en`, `sleep`, `dir`, `m0`, `m1` all follow
the writes and `MOTOR_FAULT` stays high (no fault reported).

## Open question

`DIGITAL_HALL1_INT` stays **low** through every polarity combination tried
(0.3 s .. 2 s, four dir/M0/M1 permutations), so either the motor is not actually
turning, the hall is not a position sensor, or the module needs a supply that is not
`MOTOR_EN`.  This needs a visual check of the phone while driving the motor.
