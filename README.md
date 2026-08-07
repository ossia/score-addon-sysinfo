# score-addon-sysinfo

A [ossia score](https://ossia.io) add-on which exposes the machine score runs on as a
read-only device: CPU load, memory pressure, free disk space, network throughput, GPU
load, battery level, the screens, and the hardware description around them.

The address tree is the same on Linux, macOS and Windows. What a given platform cannot
report is published as an empty string or a zero rather than being omitted, so a score
authored on one machine keeps resolving on another.

Built on [hwinfo](https://github.com/lfreist/hwinfo), vendored as a submodule, plus Qt
and a small platform layer for what hwinfo does not cover.

Everything is compiled into the plug-in itself: hwinfo goes in as an object library, and
NVML is loaded at runtime only if the NVIDIA driver happens to be installed. There is
nothing to deploy next to the `.so` / `.dll` / `.dylib`.

## Building

```bash
git clone --recursive https://github.com/ossia/score-addon-sysinfo
```

Then either drop it in `score/src/addons/`, or build it against an installed score SDK:

```bash
cmake -B build -DSCORE_SOURCE_DIR=/path/to/score
cmake --build build
```

## Settings

| Setting          | Default | Meaning                                                            |
|------------------|---------|--------------------------------------------------------------------|
| Refresh rate     | 1000 ms | How often the changing values are re-read                          |
| Per-thread CPU   | on      | Whether to expose `/cpu/thread/usage` and `/cpu/thread/frequency`   |

The hardware description is read once, when the device connects. The readings that block —
CPU load is a delta measured over a sampling window, free space on a network mount can
stall — happen on a worker thread, so a slow device never holds up the network context.
The clock under `/time` is pushed independently of that worker and keeps ticking either
way.

## Addresses

Sizes and frequencies are floats: ossia integers are 32-bit, which byte counts overflow.
32 GiB is then accurate to about 4 kB. Ratios are floats in `[0; 1]`. **live** marks the
values refreshed at the configured rate.

`N` below is an index; `*/count` says how many there are, and nothing is created for a
component the machine does not have.

### Machine

| Address              | Type   |                                              |
|----------------------|--------|----------------------------------------------|
| `/hostname`          | string |                                              |
| `/uptime`            | float  | **live**, seconds since boot                 |
| `/os/name`           | string |                                              |
| `/os/product`        | string | Product name as Qt reports it                |
| `/os/version`        | string |                                              |
| `/os/kernel`         | string |                                              |
| `/os/architecture`   | string | `x86_64`, `arm64`, ...                       |
| `/os/bits`           | int    | 32, 64, or 0 if unknown                      |
| `/os/endianness`     | string | `little` or `big`                            |
| `/qt/version`        | string |                                              |
| `/qt/platform`       | string | QPA plugin: `xcb`, `wayland`, `cocoa`, ...   |
| `/qt/style`          | string | Widget style in use                          |
| `/qt/abi`            | string | ABI score was built for                      |
| `/mainboard/vendor`, `/mainboard/name`, `/mainboard/version`, `/mainboard/serial` | string | |

### Time

`/time/iso`, `/time/date`, `/time/clock` (strings), `/time/year`, `/month`, `/day`,
`/weekday` (1 is Monday), `/hour`, `/minute`, `/second`, `/unix`, `/utc_offset` (minutes)
as ints, `/time/day_seconds` as a float, `/time/timezone` as a string. All **live**.

### Load

`/load/1m`, `/load/5m`, `/load/15m` — **live**. Zero on Windows, which has no equivalent.

### CPU

| Address                          | Type       |                                     |
|----------------------------------|------------|-------------------------------------|
| `/cpu/count`                     | int        | Physical packages                   |
| `/cpu/threads`                   | int        | Logical threads                     |
| `/cpu/usage`                     | float 0-1  | **live**                            |
| `/cpu/thread/usage`              | list float | **live**, one per thread            |
| `/cpu/thread/frequency`          | list float | **live**, Hz, one per thread        |
| `/cpu/N/vendor`, `/cpu/N/model`  | string     |                                     |
| `/cpu/N/cores/physical`, `/cpu/N/cores/logical` | int |                      |
| `/cpu/N/frequency/base`, `/cpu/N/frequency/max` | float | Hz                 |
| `/cpu/N/cache/l1d`, `/l1i`, `/l2`, `/l3`        | float | bytes              |
| `/cpu/N/flags`                   | list string| Instruction set extensions          |

### GPU

| Address                  | Type       |                                              |
|--------------------------|------------|----------------------------------------------|
| `/gpu/count`             | int        |                                              |
| `/gpu/N/vendor`, `/name`, `/driver` | string |                                     |
| `/gpu/N/vendor_id`, `/device_id`    | string | PCI ids                             |
| `/gpu/N/cores`           | int        |                                              |
| `/gpu/N/memory/total`    | float      | **live**, bytes                              |
| `/gpu/N/memory/used`     | float      | **live**, bytes                              |
| `/gpu/N/memory/shared`   | float      | Memory shared with the host                  |
| `/gpu/N/memory/usage`    | float 0-1  | **live**                                     |
| `/gpu/N/frequency`       | float      | **live**, Hz                                 |
| `/gpu/N/usage`           | float 0-1  | **live**                                     |
| `/gpu/N/temperature`     | float      | **live**, degrees Celsius                    |

Load, clock, temperature and memory come from NVML for NVIDIA cards (loaded at runtime if
the driver is present, no build dependency) and from the amdgpu / i915 sysfs nodes on
Linux. They read zero where no such source exists — notably on macOS, where hwinfo does
not enumerate GPUs at all.

### Memory

| Address              | Type      |                                              |
|----------------------|-----------|----------------------------------------------|
| `/memory/total`      | float     | bytes                                        |
| `/memory/free`       | float     | **live**, bytes not in use at all            |
| `/memory/available`  | float     | **live**, bytes available for allocation     |
| `/memory/used`       | float     | **live**, `total - available`                |
| `/memory/usage`      | float 0-1 | **live**                                     |
| `/memory/module/count`, `/memory/module/N/vendor`, `/name`, `/model`, `/serial`, `/size`, `/frequency` | | Windows only |

### Disks

| Address           | Type       |                                                    |
|-------------------|------------|----------------------------------------------------|
| `/disk/count`     | int        |                                                    |
| `/disk/total`     | float      | **live**, capacity of every mounted filesystem     |
| `/disk/free`      | float      | **live**                                           |
| `/disk/usage`     | float 0-1  | **live**                                           |
| `/disk/N/vendor`, `/model`, `/serial` | string |                                 |
| `/disk/N/size`    | float      | Capacity of the device itself                      |
| `/disk/N/interface` | string   | `NVMe`, `SATA`, `USB3 (10 Gbit/s)`, ...            |
| `/disk/N/mountpoints` | list string | Paths the disk is mounted on                  |
| `/disk/N/total`, `/free`, `/usage` | | **live**, over the mounted filesystems of that disk |

### Network

| Address              | Type   |                                                     |
|----------------------|--------|-----------------------------------------------------|
| `/network/count`     | int    |                                                     |
| `/network/rx`, `/tx` | float  | **live**, total throughput in bytes per second      |
| `/network/N/index`, `/description`, `/mac`, `/ipv4`, `/ipv6` | string | |
| `/network/N/rx`, `/tx` | float | **live**, bytes per second                         |
| `/network/N/rx_total`, `/tx_total` | float | **live**, bytes since the interface came up |

### Battery

`/battery/count`, `/battery/level` (0-1, first battery, **live**), `/battery/charging`
(**live**), and per battery `/battery/N/vendor`, `/model`, `/serial`, `/technology`,
`/level`, `/energy/now`, `/energy/full`, `/charging`, `/state`. Not reported on Windows.

### Displays

From Qt, so they follow whatever the QPA plugin sees.

`/display/count`, and per screen `/display/N/name`, `/manufacturer`, `/model`, `/serial`,
`/width`, `/height`, `/x`, `/y`, `/available/width`, `/available/height`, `/refresh_rate`,
`/dpi/logical`, `/dpi/physical`, `/scale`, `/depth`, `/physical/width`,
`/physical/height` (millimetres), `/orientation`, `/primary`.

## Platform coverage

hwinfo cannot fill every field everywhere. Notably, memory modules are only enumerated on
Windows, batteries are not reported on Windows, and no GPU is enumerated on macOS. Those
addresses exist regardless and read as `""` or `0`.
