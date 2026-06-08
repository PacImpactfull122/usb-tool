# usbctl

Ferramenta de linha de comando em C++17 para interacao direta com hardware USB no Linux e Windows, sem dependencias de terceiros.

## Funcionalidades

- Enumeracao de dispositivos USB com classe, velocidade e protocolo via sysfs (Linux) ou SetupAPI (Windows)
- Informacoes detalhadas de um dispositivo especifico
- Decodificacao completa de todos os descritores: device, configuration, interface, endpoint, string
- Parser de HID report descriptor com decodificacao de usage pages, collections, Input/Output/Feature
- Scan de todos os endpoints (endereco, tipo de transferencia, direcao, max packet size, intervalo)
- Leitura de todos os string descriptors com conversao UTF-16LE para UTF-8
- Captura de trafego USB raw via usbmon com decodificacao de pacotes (requer modulo usbmon)
- Reset de dispositivo via usbfs / IOCTL
- Transferencias de controle, bulk e interrupt raw com hexdump estruturado
- Leitura de HID reports em loop via endpoint interrupt (teclados, mouses, gamepads)
- Leitura e escrita de setores raw via protocolo USB MSC BOT (Linux)
- Leitura e escrita de setores raw via ReadFile/WriteFile OVERLAPPED (Windows)
- Envio de DFU DETACH para modo bootloader

## Requisitos

### Linux
- Kernel com suporte a usbfs (`/dev/bus/usb`) e sysfs (`/sys/bus/usb/devices`)
- `g++` com suporte a C++17
- Root ou membership no grupo `plugdev` com udev rules configuradas

### Windows
- MinGW-w64 ou MSVC com suporte a C++17
- Windows SDK (inclui `setupapi.h`, `usbioctl.h`, `usb.h`)
- Privilegios de administrador

## Compilacao

### Linux

```bash
make
```

### Linux (debug com sanitizers)

```bash
make debug
```

### Linux (instalar em /usr/local/bin)

```bash
sudo make install
```

### Windows (MinGW)

```bat
mingw32-make -f Makefile.win
```

### Windows (MSVC)

```bat
cl /std:c++17 /O2 main.cpp usb_windows.cpp /link setupapi.lib cfgmgr32.lib /out:usbctl.exe
```

## Uso

### Listar dispositivos

```bash
sudo ./usbctl listar
```

Saida exemplo:
```
[0] 0951:1666  Kingston DataTraveler  serial: 001234
     classe: armazenamento em massa (08)  velocidade: 480 Mbit/s
     /sys/bus/usb/devices/1-1/
[1] 046d:c52b  Logitech Unifying Receiver  serial: -
     classe: hid (03)  velocidade: 12 Mbit/s
     /sys/bus/usb/devices/1-2/
```

### Decodificacao completa de descritores

Parseia e exibe de forma estruturada o device descriptor, configuration descriptor (com todas as interfaces e endpoints) e todos os string descriptors:

```bash
sudo ./usbctl decodificar 0
```

Saida exemplo (teclado HID):
```
=== device descriptor ===
  bcdUSB           : 2.00
  bDeviceClass     : 0x00 (definida por interface)
  bMaxPacketSize0  : 8 bytes
  idVendor         : 04d9
  idProduct        : a01c
  bNumConfigurations: 1

  raw:
  0000  12 01 00 02 00 00 00 08  d9 04 1c a0 10 01 01 02  |................|
  0010  03 01                                             |..|

=== configuration descriptor ===
  configuracao: 2 interface(s)  total: 59 bytes

  endpoint 0x81  iface=0  interrupt     IN   max=   8 bytes  intervalo=10
  endpoint 0x82  iface=1  interrupt     IN   max=   3 bytes  intervalo=10

=== string descriptors ===
  [1] SINO WEALTH ELECTRONIC LTD.
  [2] USB Keyboard
  [3] SN0000000001
```

### Scan de endpoints

Lista todos os endpoints do dispositivo com atributos completos:

```bash
sudo ./usbctl scan-ep 0
```

Saida exemplo:
```
dispositivo 0: 2 endpoint(s) encontrado(s)

  ep 0x81  iface=0  interrupt     IN    max=   8 bytes  intervalo=10
  ep 0x82  iface=1  interrupt     IN    max=   3 bytes  intervalo=10
```

### HID report descriptor decodificado

Parseia o HID report descriptor e exibe os itens estruturados com nomes de usage pages e usages conhecidos:

```bash
sudo ./usbctl dump-hid 0
```

Saida exemplo (teclado):
```
  hid report descriptor (63 bytes)

  hex: 05 01 09 06 a1 01 05 07 ...

  itens decodificados:

    Usage Page(generic desktop)  [0x0001]
    Usage(keyboard)  [0x0006]
    Collection(Application)
      Usage Page(keyboard)  [0x0007]
      Usage Min(0x00e0)
      Usage Max(0x00e7)
      Logical Min(0)
      Logical Max(1)
      Report Size(1 bits)
      Report Count(8)
      Input(Data,Var,Abs)  [0x02]
      Report Size(8 bits)
      Report Count(1)
      Input(Const,Array,Abs)  [0x01]
      Report Size(8 bits)
      Report Count(6)
      Usage Min(0x0000)
      Usage Max(0x00ff)
      Logical Min(0)
      Logical Max(255)
      Input(Data,Array,Abs)  [0x00]
    End Collection
```

### String descriptors

```bash
sudo ./usbctl strings 0
```

### Monitoramento de trafego USB (usbmon)

Captura e decodifica todos os pacotes USB em tempo real. Requer o modulo `usbmon` carregado:

```bash
# carregar o modulo (uma vez por boot)
sudo modprobe usbmon

# monitorar todos os dispositivos em todos os barramentos
sudo ./usbctl monitorar

# monitorar barramento 1
sudo ./usbctl monitorar 1

# monitorar barramento 1, apenas dispositivo com endereco 3
sudo ./usbctl monitorar 1 3

# capturar apenas os primeiros 100 pacotes
sudo ./usbctl monitorar 1 3 100
```

Saida exemplo:
```
monitorando /dev/usbmon1  (addr=3, ctrl+c para parar)

timestamp          tipo xfer  dir  ep   dev  status  len  dados
------------------  ---- ----- ---- ---- ---- ------- ----
  1718123456.001234  S    int   IN   81   3    0       8    00 00 04 00 00 00 00 00
  1718123456.012345  C    int   IN   81   3    0       8    00 00 04 00 00 00 00 00
  1718123456.023456  S    int   IN   81   3    0       8    00 00 00 00 00 00 00 00
```



```bash
sudo ./usbctl info 0
```

Saida exemplo:
```
dispositivo [0]
  vendor id  : 046d
  product id : c52b
  fabricante : Logitech
  produto    : USB Receiver
  serial     : -
  classe     : hid (03/00/00)
  velocidade : 12 Mbit/s
  barramento : 1  endereco: 3
  caminho    : /sys/bus/usb/devices/1-2/
```

### Resetar dispositivo

```bash
sudo ./usbctl resetar 0
```

### Ler descritor

```bash
# descritor de dispositivo (tipo 01)
sudo ./usbctl descritor 0 01

# descritor de configuracao (tipo 02)
sudo ./usbctl descritor 0 02

# descritor de string indice 1 (tipo 03)
sudo ./usbctl descritor 0 03 01

# HID report descriptor (tipo 22)
sudo ./usbctl descritor 0 22
```

### Leitura de HID reports (teclado, mouse, gamepad)

Leitura continua via endpoint interrupt. Mostra os bytes raw de cada report enviado pelo dispositivo.

```bash
# leitura do endpoint interrupt 0x81, buffer de 8 bytes (padrao hid teclado)
sudo ./usbctl hid-ler 0 81

# especificar tamanho do buffer e timeout em ms
sudo ./usbctl hid-ler 0 81 8 3000

# mouse (report de 4 bytes: botoes, dx, dy, scroll)
sudo ./usbctl hid-ler 1 81 4
```

Saida exemplo ao pressionar tecla A em um teclado:
```
lendo hid reports do endpoint 81 (ctrl+c para parar)

[   0] 00 00 04 00 00 00 00 00
[   1] 00 00 00 00 00 00 00 00
```

Formato do report HID de teclado (8 bytes):
```
byte 0  modificadores (bit0=ctrl, bit1=shift, bit2=alt, bit3=meta)
byte 1  reservado
bytes 2-7  ate 6 keycodes simultaneos (0x04=a, 0x05=b, 0x28=enter...)
```

### Transferencia de controle

```bash
# usbctl controle <indice> <bmRequestType> <bRequest> <wValue> <wIndex> <hex_dados> <r|w>

# leitura: GET_DESCRIPTOR device
sudo ./usbctl controle 0 80 06 0100 0000 12000000 r

# escrita: SET_CONFIGURATION
sudo ./usbctl controle 0 00 09 0100 0000 "" w
```

### Transferencia bulk

```bash
# usbctl bulk <indice> <endpoint> <hex_dados> <r|w>

# envio para endpoint 0x01 (bulk OUT)
sudo ./usbctl bulk 0 01 deadbeef w

# leitura do endpoint 0x81 (bulk IN), buffer de 64 bytes
sudo ./usbctl bulk 0 81 0000000000000000000000000000000000000000000000000000000000000000 r
```

### Leitura de setor raw (storage)

```bash
# le 1 setor a partir do LBA 0 (MBR)
sudo ./usbctl ler-setor 0 0 1

# le 4 setores a partir do LBA 2048
sudo ./usbctl ler-setor 0 2048 4
```

### Escrita de setor raw

```bash
# operacao irreversivel, sobrescreve dados permanentemente
# hex_dados deve ter exatamente N*512 bytes (N setores)
sudo ./usbctl escrever-setor 0 0 <512_bytes_em_hex>
```

### Modo bootloader (DFU)

```bash
# envia DFU_DETACH (requer suporte a USB DFU class no firmware)
sudo ./usbctl bootloader 0
```

## Estrutura

```
usbctl/
  usb.h            interface comum entre plataformas
  usb_linux.cpp    implementacao Linux (usbfs, ioctl, sysfs, protocolo BOT, usbmon)
  usb_windows.cpp  implementacao Windows (SetupAPI, DeviceIoControl, OVERLAPPED I/O)
  descritor.h      interface do parser de descritores e HID report decoder
  descritor.cpp    parser completo: device/config/interface/endpoint/string/HID report
  main.cpp         CLI e despacho de comandos
  Makefile         build para Linux
  Makefile.win     build para Windows (MinGW)
```

## Protocolo USB MSC BOT (Linux)

Leitura e escrita de setores seguem o protocolo Bulk-Only Transport definido em USB MSC:

1. CBW (Command Block Wrapper) enviado via bulk OUT com o CDB SCSI dentro
2. Transferencia de dados via bulk IN (leitura) ou bulk OUT (escrita)
3. CSW (Command Status Wrapper) recebido via bulk IN para confirmar sucesso

Endpoints assumidos: bulk OUT `0x01`, bulk IN `0x81`. Dispositivos com endpoints diferentes precisam que o codigo seja ajustado ou que os endpoints sejam descobertos a partir dos descritores de interface.

## APIs utilizadas

### Linux
- `/sys/bus/usb/devices/` - enumeracao e metadados via sysfs
- `/dev/bus/usb/BBB/DDD` - acesso direto via usbfs
- `USBDEVFS_RESET` - reset de dispositivo
- `USBDEVFS_CONTROL` - transferencias de controle e DFU DETACH
- `USBDEVFS_BULK` - transferencias bulk e protocolo BOT
- `USBDEVFS_INTERRUPT` - leitura de endpoints interrupt (HID)
- `USBDEVFS_CLAIMINTERFACE` / `USBDEVFS_RELEASEINTERFACE` - controle exclusivo de interface
- `USBDEVFS_DISCONNECT` - desconexao do driver do kernel

### Windows
- `SetupDiGetClassDevs` / `SetupDiEnumDeviceInfo` - enumeracao
- `CreateFile` - abertura de handle
- `IOCTL_USB_RESET_PORT` - reset
- `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` - descritores
- `ReadFile` / `WriteFile` com `OVERLAPPED` - I/O assincrono em disco

## Limitacoes

- Transferencias de controle e bulk no Windows requerem WinUSB ou driver de kernel customizado instalado para o dispositivo.
- Leitura HID via `hid-ler` desconecta temporariamente o driver do kernel (usbhid). O dispositivo volta ao normal apos liberar a interface ou reiniciar.
- Leitura e escrita de setor funcionam apenas em dispositivos de armazenamento em massa. No Windows, o dispositivo deve ser aberto como `\\.\PhysicalDriveN`.
- Modo bootloader (DFU) funciona apenas se o firmware implementar USB DFU class (STM32, ATmega32u4, ESP32-S2, etc).
- Os endpoints de bulk OUT (`0x01`) e bulk IN (`0x81`) sao assumidos como fixos. Dispositivos com configuracao diferente requerem ajuste.
- Operacoes de baixo nivel requerem root no Linux e administrador no Windows.

## Permissoes no Linux (sem root)

Criar udev rule em `/etc/udev/rules.d/99-usbctl.rules`:

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
