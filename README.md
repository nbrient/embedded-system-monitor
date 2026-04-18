# Embedded System Monitor

Lightweight monitoring daemon for embedded Linux systems.

This project periodically reads system information from `/proc` and reports:
- CPU load
- memory usage
- interrupts activity
- process resource usage

The daemon is designed for embedded targets where observability must remain simple, lightweight, and easy to integrate.

This project can be useful for:
- monitoring an embedded target during development
- tracking CPU / RAM / IRQ activity
- observing the behavior of specific processes
- improving debug and system analysis on Linux-based boards

---

## Features

- periodic CPU usage monitoring
- RAM usage monitoring
- IRQ activity collection
- process monitoring
- configurable thresholds
- structured logging
- Linux embedded oriented design
- Meson-based build system

---

## Project goals

The purpose of this project is to provide a small and maintainable monitoring service for embedded Linux systems.

Typical use cases:
- basic health monitoring on an embedded target
- troubleshooting performance issues
- tracking abnormal CPU or RAM usage
- detecting interrupt storms or misbehaving processes
- integrating monitoring into a custom BSP or Yocto image

---

## Architecture

The application is split into small dedicated modules:

- **Common**  
  shared structures, utilities and configuration helpers

- **CpuCheck**  
  reads CPU statistics from `/proc/stat` and computes CPU load

- **RamCheck**  
  reads memory information from `/proc/meminfo`

- **Interrupts**  
  parses `/proc/interrupts` and tracks IRQ activity

- **Process**  
  monitors selected processes and their resource usage

- **Logger**  
  sends events and alerts to the logging backend

- **Watchdog**  
  supervises periodic execution and internal health checks

This modular design keeps the project maintainable and close to embedded software practices.

---

## How it works

The daemon runs periodically and performs the following sequence:

1. read system information from `/proc`
2. compute metrics and deltas
3. compare results against configured thresholds
4. emit logs or alerts when required
5. wait for the next monitoring period

The current implementation focuses on lightweight local monitoring rather than cloud connectivity or complex dashboards.

---

## Logging

The monitoring daemon is intended to log through standard Linux system facilities.

Possible backends include:
- `systemd-journald`
- `syslog`

---

## Configuration

The monitor is configured through an external configuration file.

Typical configurable parameters:
- monitoring period
- CPU alert threshold
- RAM alert threshold
- process list to monitor
- process-specific thresholds
- logging behavior

Example configuration path:

```bash
/etc/embedded-monitor/config.yaml
```

## Build

This project uses the **Meson** build system. Meson usually relies on **Ninja** to compile the project.

From the root of the repository:

### Configure the build directory

```bash
meson setup build
```

### Build the project

```bash
meson compile -C build
```
You can also use Ninja directly:

```bash
ninja -C build
```

### Run the executable 

Depending on the current Meson layout, the executable can be located in the build directory.

```bash
./build/src/monitoringExec
```

If the monitor uses a configuration file, you can also start it with a custom config path.

```bash
./build/src/monitoringExec /etc/embedded-monitor/generic.yaml
```

## How to read logs

The monitor can send logs through different backends depending on the current configuration.

### Journald

```bash
meson setup build
```

If the backend is configured to use journald, logs can be read with journalctl.

Display recent logs in json format:
```bash
journalctl -r -o json-pretty
```
Display only the logs of the monitoring service:
```bash
journalctl -u embedded-monitor.service
```

You can add -f to follow logs in real time.

Filter logs with a specific MESSAGE_ID:
```bash
journalctl MESSAGE_ID=31ec43a6b4c24545bf21791c041c9f89
```
### Syslog
If the backend is configured to use syslog, logs can usually be read from the system log file.
On many Linux systems, the log file is: /var/log/syslog

You can also follow logs in real time:
```bash
tail -f /var/log/syslog
```
For the application :
```bash
grep embedded-monitor /var/log/syslog
```
