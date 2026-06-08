# usb-tool

Ferramenta de linha de comando em C++ puro para interacao direta com hardware USB no Linux e Windows, sem dependencias de terceiros.

## Funcionalidades

- Enumeracao de todos os dispositivos USB conectados
- Reset/ejecao de dispositivo especifico
- Leitura de descritores USB (device, configuration, string, etc.)
- Transferencia de controle e bulk raw
- Leitura e escrita de setores raw (dispositivos de armazenamento)
- Envio de DFU DETACH para modo bootloader

## Requisitos

### Linux
- Kernel com suporte a usbfs (`/dev/bus/usb`)
- `g++` com suporte a C++17
- Root ou membership no grupo `plugdev` com udev rules adequadas

### Windows
- MinGW-w64 ou MSVC com suporte a C++17
- Privilegios de administrador
- Windows DDK headers (inclusas no Windows SDK)

## Compilacao

### Linux

```bash
make
```

### Windows (MinGW)

```bat
mingw32-make -f Makefile.win
```

### Windows (MSVC)

```bat
cl /std:c++17 /O2 main.cpp usb_windows.cpp /link setupapi.lib cfgmgr32.lib /out:usb-tool.exe
```

## Uso

### Listar dispositivos

```bash
sudo ./usb-tool listar
```

Saida exemplo:
```
[0] 0951:1666  Kingston DataTraveler (serial: 001234)
     caminho: /sys/bus/usb/devices/1-1/
[1] 046d:c52b  Logitech Unifying Receiver (serial: -)
     caminho: /sys/bus/usb/devices/1-2/
```

### Resetar dispositivo

```bash
sudo ./usb-tool resetar 0
```

### Ler descritor

```bash
# descritor de dispositivo (tipo 01)
sudo ./usb-tool descritor 0 01

# descritor de configuracao (tipo 02)
sudo ./usb-tool descritor 0 02

# descritor de string indice 1 (tipo 03)
sudo ./usb-tool descritor 0 03 01
```

### Transferencia de controle

```bash
# usb-tool controle <indice> <bmRequestType> <bRequest> <wValue> <wIndex> <hex_dados> <r|w>

# leitura (r): GET_DESCRIPTOR device
sudo ./usb-tool controle 0 80 06 0100 0000 12000000 r

# escrita (w): SET_CONFIGURATION
sudo ./usb-tool controle 0 00 09 0100 0000 "" w
```

### Transferencia bulk

```bash
# usb-tool bulk <indice> <endpoint> <hex_dados> <r|w>

# envio para endpoint 0x01 (bulk OUT)
sudo ./usb-tool bulk 0 01 deadbeef w

# leitura do endpoint 0x81 (bulk IN), buffer de 64 bytes
sudo ./usb-tool bulk 0 81 0000000000000000000000000000000000000000000000000000000000000000 r
```

### Leitura de setor raw (storage)

```bash
# le 1 setor a partir do LBA 0 (MBR)
sudo ./usb-tool ler-setor 0 0 1

# le 4 setores a partir do LBA 2048
sudo ./usb-tool ler-setor 0 2048 4
```

### Escrita de setor raw

```bash
# ! operacao irreversivel, sobrescreve dados permanentemente
# hex_dados deve ter exatamente N*512 bytes (N setores)
sudo ./usb-tool escrever-setor 0 0 <512_bytes_em_hex>
```

### Modo bootloader (DFU)

```bash
# envia DFU_DETACH para o dispositivo (requer suporte a USB DFU class)
sudo ./usb-tool bootloader 0
```

## Estrutura do codigo

```
usb-tool/
  usb.h            interface comum entre plataformas
  usb_linux.cpp    implementacao Linux (usbfs, ioctl, sysfs)
  usb_windows.cpp  implementacao Windows (SetupAPI, DeviceIoControl)
  main.cpp         CLI e logica de entrada
  Makefile         build para Linux
  Makefile.win     build para Windows (MinGW)
```

## APIs utilizadas

### Linux
- `/sys/bus/usb/devices/` - enumeracao via sysfs
- `/dev/bus/usb/BBB/DDD` - acesso direto via usbfs
- `USBDEVFS_RESET` - reset de dispositivo
- `USBDEVFS_CONTROL` - transferencias de controle
- `USBDEVFS_BULK` - transferencias bulk
- `USBDEVFS_GET_DESCRIPTOR` - leitura de descritores

### Windows
- `SetupDiGetClassDevs` / `SetupDiEnumDeviceInfo` - enumeracao
- `CreateFile` - abertura de handle
- `IOCTL_USB_RESET_PORT` - reset
- `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` - descritores
- `ReadFile` / `WriteFile` com OVERLAPPED - I/O em disco

## Limitacoes

- **Transferencias de controle/bulk no Windows** requerem WinUSB ou driver de kernel customizado instalado para o dispositivo alvo. Sem ele, essas operacoes retornam erro.
- **Leitura/escrita de setor** funciona apenas em dispositivos de armazenamento em massa. No Windows, abrir o dispositivo como `\\.\PhysicalDriveN` em vez do caminho do hub USB.
- **Modo bootloader (DFU)** funciona apenas se o firmware do dispositivo implementar USB DFU class (ex: STM32, ATmega32u4, ESP32-S2 em modo DFU).
- **Acesso a memoria interna** de dispositivos nao-storage (HID, audio, etc.) nao e possivel via APIs padrao sem firmware cooperativo.
- Operacoes de baixo nivel requerem root no Linux e administrador no Windows.

## Permissoes no Linux (sem root)

Criar udev rule em `/etc/udev/rules.d/99-usb-tool.rules`:

```
SUBSYSTEM=="usb", MODE="0666", GROUP="plugdev"
```

Recarregar:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Adicionar usuario ao grupo:

```bash
sudo usermod -aG plugdev $USER
```
