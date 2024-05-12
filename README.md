# ESP32WiFiScan

An EPS32 program that displays a list of Wi-Fi networks in the terminal.

Can use with android Serial USB terminal such UsbTerminal (or with a computer with any terminal app such as putty)
115200 8 N 1

1. Flash to any esp32 board with a hardware serial
2. Open terminal or connect to the mobile phone using OTA cable

VT-100 support must be autodetected

### v2
more data: losses, network encryption, mesh detection etc
rssi graphs added
control via terminal added
commands:
`*` : toggle mode
`/` : toggle xterm mode on/off (only if xterm supported)
`+` / `-` : change scan speed
type network # and press enter: view rssi realtime graph (return: press enter)
esc : back to default mode
`r` : reset data



Released into the public domain.

Created by Denis, 2023. Thanks for https://github.com/LieBtrau
