# Raphael out-of-tree kernel modules

Two self-contained modules, buildable against **any** distribution's kernel
headers as long as the kernel has the prepatch features they need:

| Module | Purpose | Kernel requirements |
|---|---|---|
| `input/pm8150b-haptics` | PM8150B haptics (ERM) on the secondary SPMI die, `ff-memless` rumble | `CONFIG_INPUT_FF_MEMLESS`, SPMI + regmap (all distros) |
| `of/dtbo-loader` | applies a device-tree overlay at runtime and instantiates the platform devices | `CONFIG_OF_OVERLAY`, `CONFIG_OF_DYNAMIC`, `CONFIG_OF_RESOLVE` (mainline ≥ 5.5) |

Build:

```
cd drivers
make CC=/usr/local/bin/gcc-noclang    # only needed on clang+ThinLTO kernels
sudo make install
```

The overlay sources live in `dts/` and are compiled with `dtc -@`.
See `../README.md` for the full bring-up flow and `../camera/` for the camera
port notes.
