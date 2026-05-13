# zmk-input-notifier

ZMK feature module that streams **pointer deltas** (e.g. PMW3610 trackball) and
**rotary encoder ticks** over the existing
[zmk-raw-hid](https://github.com/zzeneg/zmk-raw-hid) channel
(usagePage `0xFF60` / usage `0x61`), so a host-side WebHID / native app
can visualize input in real time without modifying host OS state.

Use alongside [srwi/zmk-keypeek-layer-notifier](https://github.com/srwi/zmk-keypeek-layer-notifier)
for key positions + layer mask.

## Packet format (32 bytes, little-endian)

### Pointer delta — marker `0xF2`

```
[0]    = 0xF2
[1]    = length (=9)
[2..3] = int16 dx
[4..5] = int16 dy
[6..7] = int16 wheel
[8..9] = int16 hwheel
[10]   = uint8 buttons (bit0=L, bit1=R, bit2=M)
[11..] = 0
```

Deltas are accumulated and flushed once per `CONFIG_ZMK_INPUT_NOTIFIER_MOUSE_BURST_MS`
(default 8 ms) to keep BLE bandwidth predictable.

### Encoder tick — marker `0xF3`

```
[0]   = 0xF3
[1]   = length (=2)
[2]   = uint8 sensor_index
[3]   = int8  delta  (clamped to -127..127)
[4..] = 0
```

## Usage

1. Add to your `zmk-config/config/west.yml`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: zzeneg
      url-base: https://github.com/zzeneg
    - name: yangentai181-wq
      url-base: https://github.com/yangentai181-wq
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-raw-hid
      remote: zzeneg
      revision: main
    - name: zmk-input-notifier
      remote: yangentai181-wq
      revision: main
```

2. Append the `raw_hid_adapter` shield to the central side of your build.

3. Make sure `CONFIG_RAW_HID=y` is set; this module also picks it up via
   `select RAW_HID` in its Kconfig, so usually nothing extra is needed.

## License

MIT
