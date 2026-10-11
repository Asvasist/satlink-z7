## SatLink-Z7 hw-v1 pin constraints, Digilent Zybo Z7-20.
## Locations from the Digilent board file (zybo-z7-20/A.0) and, for the codec I2C,
## the Digilent Zybo Z7 master XDC. Wiring: hw/README.md.

## Core 1 (FreeRTOS) console: PS UART0 via EMIO. JE1 = board TX, JE2 = board RX.
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports uart0_txd]
set_property -dict {PACKAGE_PIN W16 IOSTANDARD LVCMOS33} [get_ports uart0_rxd]

## HKC (MicroBlaze V) console: UART Lite. JE7 = board TX, JE8 = board RX.
set_property -dict {PACKAGE_PIN V13 IOSTANDARD LVCMOS33} [get_ports hkc_uart_txd]
set_property -dict {PACKAGE_PIN U17 IOSTANDARD LVCMOS33} [get_ports hkc_uart_rxd]

## SSM2603 codec configuration: PS I2C0 via EMIO.
set_property -dict {PACKAGE_PIN N18 IOSTANDARD LVCMOS33} [get_ports codec_iic_scl_io]
set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports codec_iic_sda_io]

## PmodCAN #2 on Pmod JB (HKC Quad SPI): JB1 CS, JB2 MOSI, JB3 MISO, JB4 SCK, JB7 INT.
set_property -dict {PACKAGE_PIN V8 IOSTANDARD LVCMOS33} [get_ports {can2_spi_ss_io[0]}]
set_property -dict {PACKAGE_PIN W8 IOSTANDARD LVCMOS33} [get_ports can2_spi_io0_io]
set_property -dict {PACKAGE_PIN U7 IOSTANDARD LVCMOS33} [get_ports can2_spi_io1_io]
set_property -dict {PACKAGE_PIN V7 IOSTANDARD LVCMOS33} [get_ports can2_spi_sck_io]
set_property -dict {PACKAGE_PIN Y7 IOSTANDARD LVCMOS33 PULLUP true} [get_ports can2_int_n]

## LD0-3 and BTN0-3: axi_gpio_0 (Core 1).
set_property -dict {PACKAGE_PIN M14 IOSTANDARD LVCMOS33} [get_ports {leds_tri_o[0]}]
set_property -dict {PACKAGE_PIN M15 IOSTANDARD LVCMOS33} [get_ports {leds_tri_o[1]}]
set_property -dict {PACKAGE_PIN G14 IOSTANDARD LVCMOS33} [get_ports {leds_tri_o[2]}]
set_property -dict {PACKAGE_PIN D18 IOSTANDARD LVCMOS33} [get_ports {leds_tri_o[3]}]
set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports {btns_tri_i[0]}]
set_property -dict {PACKAGE_PIN P16 IOSTANDARD LVCMOS33} [get_ports {btns_tri_i[1]}]
set_property -dict {PACKAGE_PIN K19 IOSTANDARD LVCMOS33} [get_ports {btns_tri_i[2]}]
set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports {btns_tri_i[3]}]

## SW0-3 and RGB LED6 (bit 0 red, 1 green, 2 blue): hkc_gpio.
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports {sws_tri_i[0]}]
set_property -dict {PACKAGE_PIN P15 IOSTANDARD LVCMOS33} [get_ports {sws_tri_i[1]}]
set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports {sws_tri_i[2]}]
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports {sws_tri_i[3]}]
set_property -dict {PACKAGE_PIN V16 IOSTANDARD LVCMOS33} [get_ports {rgb_led6_tri_o[0]}]
set_property -dict {PACKAGE_PIN F17 IOSTANDARD LVCMOS33} [get_ports {rgb_led6_tri_o[1]}]
set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports {rgb_led6_tri_o[2]}]
