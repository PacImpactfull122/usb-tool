#ifdef __linux__

#include "usb.h"
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <cstring>
#include <cstdio>

static constexpr uint32_t TAM_SETOR = 512;

// * estruturas do protocolo BOT definidas localmente para evitar dependencia de cabecalhos externos
struct __attribute__((packed)) CBW {
    uint32_t assinatura;
    uint32_t tag;
    uint32_t tamanhoTransferencia;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  tamCDB;
    uint8_t  cdb[16];
};

struct __attribute__((packed)) CSW {
    uint32_t assinatura;
    uint32_t tag;
    uint32_t residuo;
    uint8_t  status;
};

static constexpr uint32_t CBW_ASSINATURA = 0x43425355u;
static constexpr uint32_t CSW_ASSINATURA = 0x53425355u;
static constexpr uint8_t  CBW_FLAG_IN    = 0x80u;
static constexpr uint8_t  CBW_FLAG_OUT   = 0x00u;

static std::string lerSysfs(const std::string& caminho) {
    std::ifstream arq(caminho);
    if (!arq) return "";
    std::string val;
    std::getline(arq, val);
    return val;
}

static uint16_t hexParaU16(const std::string& s) {
    if (s.empty()) return 0;
    try { return static_cast<uint16_t>(std::stoul(s, nullptr, 16)); }
    catch (...) { return 0; }
}

static int lerSysfsInt(const std::string& caminho) {
    std::string val = lerSysfs(caminho);
    if (val.empty()) return 0;
    try { return std::stoi(val); }
    catch (...) { return 0; }
}

static uint8_t lerSysfsHex8(const std::string& caminho) {
    std::string val = lerSysfs(caminho);
    if (val.empty()) return 0;
    try { return static_cast<uint8_t>(std::stoul(val, nullptr, 16)); }
    catch (...) { return 0; }
}

std::vector<DispositivoUsb> enumerarDispositivos() {
    std::vector<DispositivoUsb> lista;
    const std::string base = "/sys/bus/usb/devices/";
    DIR* dir = opendir(base.c_str());
    if (!dir) return lista;

    struct dirent* ent;
    while ((ent = readdir(dir))) {
        std::string nome = ent->d_name;
        if (nome == "." || nome == "..") continue;
        // * nos com dois pontos sao interfaces, nao dispositivos raiz
        if (nome.find(':') != std::string::npos) continue;

        std::string no = base + nome + "/";
        std::string vid = lerSysfs(no + "idVendor");
        std::string pid = lerSysfs(no + "idProduct");
        if (vid.empty() || pid.empty()) continue;

        DispositivoUsb dev{};
        dev.idVendor   = hexParaU16(vid);
        dev.idProduct  = hexParaU16(pid);
        dev.fabricante = lerSysfs(no + "manufacturer");
        dev.produto    = lerSysfs(no + "product");
        dev.serial     = lerSysfs(no + "serial");
        dev.caminho    = no;
        dev.barramento = static_cast<uint8_t>(lerSysfsInt(no + "busnum"));
        dev.endereco   = static_cast<uint8_t>(lerSysfsInt(no + "devnum"));
        dev.classe     = lerSysfsHex8(no + "bDeviceClass");
        dev.subclasse  = lerSysfsHex8(no + "bDeviceSubClass");
        dev.protocolo  = lerSysfsHex8(no + "bDeviceProtocol");
        dev.versaoUsb  = static_cast<uint8_t>(lerSysfsInt(no + "bcdUSB"));
        dev.velocidade = lerSysfs(no + "speed");

        lista.push_back(dev);
    }
    closedir(dir);
    return lista;
}

intptr_t abrirDispositivo(const DispositivoUsb& dev) {
    char caminho[64];
    std::snprintf(caminho, sizeof(caminho),
                  "/dev/bus/usb/%03d/%03d", dev.barramento, dev.endereco);
    int fd = open(caminho, O_RDWR);
    if (fd < 0) fd = open(caminho, O_RDONLY);
    return (intptr_t)fd;
}

void fecharDispositivo(intptr_t handle) {
    if (handle >= 0) close(static_cast<int>(handle));
}

bool resetarDispositivo(intptr_t handle) {
    return ioctl(static_cast<int>(handle), USBDEVFS_RESET, nullptr) == 0;
}

std::vector<uint8_t> lerDescritor(intptr_t handle, uint8_t tipo, uint8_t indice) {
    std::vector<uint8_t> buf(255, 0);
    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    ct.bRequest     = USB_REQ_GET_DESCRIPTOR;
    ct.wValue       = static_cast<uint16_t>((tipo << 8) | indice);
    ct.wIndex       = 0;
    ct.wLength      = static_cast<uint16_t>(buf.size());
    ct.timeout      = 1000;
    ct.data         = buf.data();
    int ret = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &ct);
    if (ret < 0) return {};
    buf.resize(static_cast<size_t>(ret));
    return buf;
}

bool transferirControle(intptr_t handle,
                        uint8_t bmRequestType, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex,
                        std::vector<uint8_t>& dados, bool enviar) {
    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = bmRequestType;
    ct.bRequest     = bRequest;
    ct.wValue       = wValue;
    ct.wIndex       = wIndex;
    ct.wLength      = static_cast<uint16_t>(dados.size());
    ct.timeout      = 2000;
    ct.data         = dados.empty() ? nullptr : dados.data();
    int ret = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &ct);
    if (ret < 0) return false;
    if (!enviar) dados.resize(static_cast<size_t>(ret));
    return true;
}

bool transferirBulk(intptr_t handle, uint8_t endpoint,
                    std::vector<uint8_t>& dados, bool enviar) {
    struct usbdevfs_bulktransfer bt{};
    bt.ep      = endpoint;
    bt.len     = static_cast<uint32_t>(dados.size());
    bt.timeout = 2000;
    bt.data    = dados.data();
    int ret = ioctl(static_cast<int>(handle), USBDEVFS_BULK, &bt);
    if (ret < 0) return false;
    if (!enviar) dados.resize(static_cast<size_t>(ret));
    return true;
}

bool transferirInterrupt(intptr_t handle, uint8_t endpoint,
                         std::vector<uint8_t>& dados, uint32_t timeout) {
    struct usbdevfs_bulktransfer bt{};
    // * usbdevfs_interrupttransfer tem a mesma estrutura que bulktransfer
    bt.ep      = endpoint;
    bt.len     = static_cast<uint32_t>(dados.size());
    bt.timeout = timeout;
    bt.data    = dados.data();
    // * kernels recentes removeram usbdevfs_interrupt, bulk no endpoint interrupt funciona igual
    int ret = ioctl(static_cast<int>(handle), USBDEVFS_BULK, &bt);
    if (ret < 0) return false;
    dados.resize(static_cast<size_t>(ret));
    return true;
}

bool reivindicarInterface(intptr_t handle, uint32_t iface) {
    unsigned int n = iface;
    return ioctl(static_cast<int>(handle), USBDEVFS_CLAIMINTERFACE, &n) == 0;
}

void liberarInterface(intptr_t handle, uint32_t iface) {
    unsigned int n = iface;
    ioctl(static_cast<int>(handle), USBDEVFS_RELEASEINTERFACE, &n);
}

bool desconectarDriver(intptr_t handle, uint32_t iface) {
    struct usbdevfs_ioctl cmd{};
    cmd.ifno    = static_cast<int>(iface);
    cmd.ioctl_code = USBDEVFS_DISCONNECT;
    cmd.data    = nullptr;
    // * retorno negativo pode indicar que nao havia driver, nao e erro fatal
    ioctl(static_cast<int>(handle), USBDEVFS_IOCTL, &cmd);
    return true;
}

// * protocolo BOT: envia cbw via bulk out, transfere dados, valida csw via bulk in
static bool botExec(int fd, uint8_t epOut, uint8_t epIn,
                    const uint8_t* cdb, uint8_t tamCDB,
                    uint8_t* dados, uint32_t tamDados, bool leitura) {
    static uint32_t tagSeq = 1;

    CBW cbw{};
    cbw.assinatura           = CBW_ASSINATURA;
    cbw.tag                  = tagSeq++;
    cbw.tamanhoTransferencia = tamDados;
    cbw.flags                = leitura ? CBW_FLAG_IN : CBW_FLAG_OUT;
    cbw.lun                  = 0;
    cbw.tamCDB               = tamCDB;
    std::memcpy(cbw.cdb, cdb, tamCDB);

    struct usbdevfs_bulktransfer bt{};
    bt.ep      = epOut;
    bt.len     = static_cast<uint32_t>(sizeof(CBW));
    bt.timeout = 5000;
    bt.data    = &cbw;
    if (ioctl(fd, USBDEVFS_BULK, &bt) < 0) return false;

    if (tamDados > 0) {
        bt.ep      = leitura ? epIn : epOut;
        bt.len     = tamDados;
        bt.timeout = 10000;
        bt.data    = dados;
        if (ioctl(fd, USBDEVFS_BULK, &bt) < 0) return false;
    }

    CSW csw{};
    bt.ep      = epIn;
    bt.len     = static_cast<uint32_t>(sizeof(CSW));
    bt.timeout = 5000;
    bt.data    = &csw;
    if (ioctl(fd, USBDEVFS_BULK, &bt) < 0) return false;

    return csw.assinatura == CSW_ASSINATURA && csw.status == 0;
}

bool lerSetor(intptr_t handle, uint64_t lba, uint32_t qtd, std::vector<uint8_t>& buf) {
    buf.assign(static_cast<size_t>(qtd * TAM_SETOR), 0);

    uint8_t cdb[10] = {
        0x28, 0x00,
        static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
        static_cast<uint8_t>(lba >> 8),  static_cast<uint8_t>(lba),
        0x00,
        static_cast<uint8_t>(qtd >> 8),  static_cast<uint8_t>(qtd),
        0x00
    };

    return botExec(static_cast<int>(handle), 0x01, 0x81, cdb, sizeof(cdb),
                   buf.data(), static_cast<uint32_t>(buf.size()), true);
}

bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                   const std::vector<uint8_t>& dados) {
    uint8_t cdb[10] = {
        0x2a, 0x00,
        static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
        static_cast<uint8_t>(lba >> 8),  static_cast<uint8_t>(lba),
        0x00,
        static_cast<uint8_t>(qtd >> 8),  static_cast<uint8_t>(qtd),
        0x00
    };

    // * botExec recebe uint8_t* nao-const pois o ioctl exige, mas escrita nao modifica o buffer
    return botExec(static_cast<int>(handle), 0x01, 0x81, cdb, sizeof(cdb),
                   const_cast<uint8_t*>(dados.data()), static_cast<uint32_t>(dados.size()), false);
}

bool entrarModoBootloader(intptr_t handle) {
    // * dfu detach: classe dfu, bRequest=0x00, wValue=timeout em ms
    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = 0x21;
    ct.bRequest     = 0x00;
    ct.wValue       = 1000;
    ct.wIndex       = 0;
    ct.wLength      = 0;
    ct.timeout      = 2000;
    ct.data         = nullptr;
    return ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &ct) >= 0;
}

#endif // __linux__
