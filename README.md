# embedded-monitor

Lightweight daemon that periodically collects system metrics from `/proc`
and logs them to journalctl or syslog.

## Build

```bash
meson setup build && cd build && ninja
```

## Usage

```bash
./embedded-monitor [config_dir_or_file]
```
