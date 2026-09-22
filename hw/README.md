# Hardware (Vivado)

Vivado 2025.2, part `xc7z020clg400-1`. Only sources and scripts are committed; generated
project folders are ignored (see `.gitignore`).

```
hw/bd/        block design exported as Tcl:   write_bd_tcl -force hw/bd/satlink_bd.tcl
hw/rtl/       custom Verilog: i2s_codec_if, channel_emu, payload_ctrl, ccsds_frame_accel, spec_tap
hw/sim/       testbenches
hw/xdc/       constraints
hw/coe/       FIR coefficients (.coe) and the Hann window (.mem), generated from MATLAB
hw/scripts/   project re-creation script: vivado -mode batch -source hw/scripts/create_project.tcl
```

Baselines are tagged `hw-v1`, `hw-v2`, ...; the exported XSA is attached to the matching GitHub
release. After each export, compare the Address Editor with `icd/address_map.yaml` and set the
confirmed entries to `status: frozen`.

## External wiring

| Connector | Pins | Function |
|---|---|---|
| USB-UART (J12) | on board | Linux console (PS UART1, ttyPS0) |
| Pmod JE | JE1 (V12), JE2 (W16) | FreeRTOS console, PS UART0 via EMIO, to USB-UART adapter #1 |
| Pmod JE | JE7 (V13), JE8 (U17) | MicroBlaze V console (hkc_uart), to USB-UART adapter #2 |
| Pmod JF | JF1-4 = MIO13/10/11/12, JF7 = MIO0 | PmodCAN #1 on PS SPI1 (CS/MOSI/MISO/SCK), INT on MIO0 |
| Pmod JB | V8, W8, U7, V7, INT Y7 | PmodCAN #2 on MicroBlaze V AXI Quad SPI |
| Audio | HPH OUT → LINE IN | 3.5 mm loopback cable: the "RF" link |
| CAN | PmodCAN #1 ↔ PmodCAN #2 | twisted pair, 120 Ω at both ends |
