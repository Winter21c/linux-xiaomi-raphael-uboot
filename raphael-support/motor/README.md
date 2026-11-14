# Front pop-up camera motor

## The mechanism (finally known, captured from the stock MIUI device tree)

The carriage is a **stepper motor driven by a TI DRV8846**. The stock device tree
node (`/sys/firmware/fdt`, saved as
`../camera/vendor-capture/data/vendor-fdt.dts`, search for `ti,drv8846`) is:

```
ti,drv8846@0 {
        compatible = "ti,drv8846";
        pwms = <&pm8150l_lpg 1 1000000>;     /* LPG channel 1 = STEP */
        motor,gpio-mode0 = <&tlmm 21 0>;
        motor,gpio-mode1 = <&tlmm 33 0>;
        motor,gpio-sleep = <&tlmm 35 0>;
        motor,gpio-dir   = <&tlmm 9  0>;
        motor,gpio-pwren = <&tlmm 130 0>;
        motor,rampup-pwm-period-ns  = <625000>;   /* 1.6 kHz ramp */
        motor,rampup-duration-ms    = <50>;
        motor,high-pwm-period-ns    = <52083>;    /* 19.2 kHz cruise */
        motor,high-duration-ms      = <720>;      /* full travel */
        motor,rampdown-pwm-period-ns = <625000>;
        motor,rampdown-duration-ms   = <50>;
        motor,step-mode = <2>;
};
```

The PWM comes from `qcom,pwm-lpg@bc00` (the PM8150L LPG block), channel 1 - the
same block that mainline drives with `leds-qcom-lpg`.

### Why the earlier GPIO-only attempt could not work

DRV8846 is a **stepper** driver: DIR selects direction and M0/M1 select the
microstep mode, but the motor only turns while **STEP pulses** arrive on the PWM
input.  Ramp-up/cruise/ramp-down give the speed profile.  Driving the GPIOs
statically (as we did) leaves the coil energised but the rotor stationary, which
is exactly what we observed (nFAULT stayed high, hall never changed).

Measured on MIUI: the five motor GPIOs are claimed and driven as **outputs**
(mainline leaves them as unclaimed inputs), and switching to the front camera
activates `csiphy1`, so the mechanism does work there.

## Driver

`driver/raphael-popup-motor.c` implements it for mainline:

* `devm_pwm_get()` for STEP, `devm_gpiod_get()` for the five control pins,
* the vendor timing as defaults, overridable from DT,
* `position` sysfs attribute: `echo 1 > position` extends, `echo 0` retracts.

Device tree node to add:

```dts
popup_motor: popup-motor {
        compatible = "xiaomi,raphael-popup-motor";
        pwms = <&pm8150l_lpg 1 1000000 0>;
        mode0-gpios = <&tlmm 21 0>;
        mode1-gpios = <&tlmm 33 0>;
        sleep-gpios = <&tlmm 35 0>;
        dir-gpios   = <&tlmm 9  0>;
        pwren-gpios = <&tlmm 130 0>;
};
```

(`pm8150l_lpg` must be the LPG node mainline already uses for the flash LEDs; if
the flash node consumes all channels, check `qcom,num-lpg-channels` for that
PMIC - the stock DTB says 2 channels.)

## Open items

* confirm the DIR polarity for "extend" vs "retract" on hardware (the driver
  assumes 1 = extend and can be flipped trivially);
* the vendor also drives a hall sensor (tlmm 27) for position feedback - not used
  by this driver yet;
* `motor,step-mode = <2>` selects a specific M0/M1 combination; the driver uses
  M0=1, M1=0 and may need adjusting to match.
