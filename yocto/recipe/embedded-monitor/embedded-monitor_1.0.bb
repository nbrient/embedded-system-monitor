# embedded-monitor_1.0.bb
#
# Yocto recipe for the embedded-monitor daemon.
#
# Build system : Meson.
# Dependencies : libcyaml for YAML config parsing,
#                systemd for service/log integration.

SUMMARY = "Lightweight monitoring module for embedded Linux"
DESCRIPTION = "Collects CPU, memory, IRQ and process statistics from /proc and logs them through system facilities at a configurable interval."

LICENSE     = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=231fb0970ed5a88aa921f3ab2e148cc8"

SRC_URI = "git://github.com/nbrient/embedded-monitor.git;protocol=https;branch=main"
SRCREV = "81709a81edda6c77e76c84641565051789f834c6"
PV = "1.0.0+git${SRCPV}"
S = "${WORKDIR}/git"

inherit meson pkgconfig systemd

REQUIRED_DISTRO_FEATURES += "systemd"

DEPENDS += "cyaml systemd"

RDEPENDS:${PN} += "cyaml systemd"

MONITOR_CONFIG_FILE = "${S}/config/monitor.yaml"

do_install:append() {
    install -d ${D}${sysconfdir}/embedded-monitor

    # Install default configuration file
    install -m 0644 ${MONITOR_CONFIG_FILE} \
        ${D}${sysconfdir}/embedded-monitor/monitor.yaml
}

FILES:${PN} += "${sysconfdir}/embedded-monitor"

SYSTEMD_SERVICE:${PN} = "embedded-monitor.service"