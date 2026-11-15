# Sedona for ESP32-P4

An independent Sedona firmware development line for ESP32-P4, with SOX engineering
connectivity, platform services and host-side simulation tooling.

## Scope

Waveshare ESP32-P4-ETH, Ethernet, controller I/O services, UART/I2C/SPI,
Modbus RTU, MQTT/MQTTS, HTTPS firmware update/rollback and SNTP implementation.
The default network setup uses DHCP. Electrical I/O and real-bus acceptance
must be established for the actual board and wiring.

This is the preserved Bclimate-kit development line, distinct from the spro kits
in [SedonaPro](https://github.com/SedonaPro/sedonapro). Kit checksums and native
interfaces must match the selected firmware image.

## Build

Use Linux, Python 3, a JDK and GCC. Firmware builds additionally require an
initialized ESP-IDF 6.0.3 environment. A runtime source snapshot is included in
runtime/; no private sibling checkout is required. SEDONA_RUNTIME can select a
different compatible runtime deliberately.

```sh
git clone https://github.com/SedonaPro/sedona-esp32-p4.git
cd sedona-esp32-p4
python3 build.py sedona --profile p4
python3 build.py fw --profile p4
```

Host-side experimentation:

```sh
python3 build.py sim
python3 tests/sim_test.py
```

These commands describe the development workflow, not electrical emulation or a
claim of complete firmware acceptance. See build.py --help, modules.py,
[source notes](README_RU.md) and [publication scope](PUBLICATION.md).

## Hardware and compatibility

Configure the exact board, allowed pins and network locally. Firmware uses
32-bit references; host simulation uses 64-bit references. S3 and P4 have separate
source and release tracks. Shared ancestry does not make images interchangeable.
Generated I/O lists and software tests do not establish electrical correctness.

This is a source preview, not a signed production firmware release. Follow-up
board reports should state exact revision, wiring, toolchain and test scope.

## Community and licence

See [Contributing](CONTRIBUTING.md) and
[Discussions](https://github.com/SedonaPro/.github/discussions).
Our port code uses Apache-2.0. Framework/runtime and third-party components retain
their licences and attribution: [LICENSE](LICENSE), [NOTICE](NOTICE.md).
