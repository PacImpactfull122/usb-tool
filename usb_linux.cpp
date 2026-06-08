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

static std::string lerSysfs(const std::string& caminho) {
    std::ifstream arq(caminho);
    if (!arq) return "";
    std::string val;
    std::getline(arq, val);
    return val;
}

static uint16_t hexParaU16(const std::string& s) {
    if (s.empty()) return 0;
    return (uint16_t)std::stoul(s, nullptr, 16);
}

std::vector<DispositivoUsb> enumerarDispositivos() {
    std::vector<DispositivoUsb> lista;
    const std::string base = "/sys/bus/usb/devices/";
    DIR* dir = opendir(base.c_str());
    if (!dir) return lista;

    struct dirent* ent;
    while ((ent = readdir(dir))) {
        std::string nome = ent->d_name;
        // * nos com dois pontos sao interfaces, nao dispositivos
        if (nome.find(':') != std::string::npos) continue;
        if (nome == "." || nome == "..") continue;

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

        std::string busStr = lerSysfs(no + "busnum");
        std::string devStr = lerSysfs(no + "devnum");
        dev.barramento = busStr.empty() ? 0 : (uint8_t)std::stoi(busStr);
        dev.endereco   = devStr.empty() ? 0 : (uint8_t)std::stoi(devStr);

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
    if (handle > 0) close((int)handle);
}

bool resetarDispositivo(intptr_t handle) {
    return ioctl((int)handle, USBDEVFS_RESET, nullptr) == 0;
}

std::vector<uint8_t> lerDescritor(intptr_t handle, uint8_t tipo, uint8_t indice) {
    std::vector<uint8_t> buf(255, 0);
    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    ct.bRequest     = USB_REQ_GET_DESCRIPTOR;
    ct.wValue       = (uint16_t)((tipo << 8) | indice);
    ct.wIndex       = 0;
    ct.wLength      = (uint16_t)buf.size();
    ct.timeout      = 1000;
    ct.data         = buf.data();
    int ret = ioctl((int)handle, USBDEVFS_CONTROL, &ct);
    if (ret < 0) return {};
    buf.resize(ret);
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
    ct.wLength      = (uint16_t)dados.size();
    ct.timeout      = 2000;
    ct.data         = dados.empty() ? nullptr : dados.data();
    int ret = ioctl((int)handle, USBDEVFS_CONTROL, &ct);
    if (ret < 0) return false;
    if (!enviar) dados.resize(ret);
    return true;
}

bool transferirBulk(intptr_t handle, uint8_t endpoint,
                    std::vector<uint8_t>& dados, bool enviar) {
    struct usbdevfs_bulktransfer bt{};
    bt.ep      = endpoint;
    bt.len     = (uint32_t)dados.size();
    bt.timeout = 2000;
    bt.data    = dados.data();
    int ret = ioctl((int)handle, USBDEVFS_BULK, &bt);
    if (ret < 0) return false;
    if (!enviar) dados.resize(ret);
    return true;
}

bool lerSetor(intptr_t handle, uint64_t lba, uint32_t qtd, std::vector<uint8_t>& buf) {
    const uint32_t tamSetor = 512;
    buf.resize(qtd * tamSetor, 0);

    // * cdb scsi read(10)
    uint8_t cdb[10] = {
        0x28, 0x00,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0x00,
        (uint8_t)(qtd >> 8),  (uint8_t)(qtd),
        0x00
    };

    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = 0x21;
    ct.bRequest     = 0x00;
    ct.wValue = ct.wIndex = 0;
    ct.wLength = sizeof(cdb);
    ct.timeout = 5000;
    ct.data    = cdb;
    if (ioctl((int)handle, USBDEVFS_CONTROL, &ct) < 0) return false;

    struct usbdevfs_bulktransfer bt{};
    bt.ep      = 0x81;
    bt.len     = (uint32_t)buf.size();
    bt.timeout = 5000;
    bt.data    = buf.data();
    return ioctl((int)handle, USBDEVFS_BULK, &bt) >= 0;
}

bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                   const std::vector<uint8_t>& dados) {
    // * cdb scsi write(10)
    uint8_t cdb[10] = {
        0x2a, 0x00,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0x00,
        (uint8_t)(qtd >> 8),  (uint8_t)(qtd),
        0x00
    };

    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = 0x21;
    ct.bRequest     = 0x00;
    ct.wValue = ct.wIndex = 0;
    ct.wLength = sizeof(cdb);
    ct.timeout = 5000;
    ct.data    = cdb;
    if (ioctl((int)handle, USBDEVFS_CONTROL, &ct) < 0) return false;

    struct usbdevfs_bulktransfer bt{};
    bt.ep      = 0x01;
    bt.len     = (uint32_t)dados.size();
    bt.timeout = 5000;
    bt.data    = const_cast<uint8_t*>(dados.data());
    return ioctl((int)handle, USBDEVFS_BULK, &bt) >= 0;
}

bool entrarModoBootloader(intptr_t handle) {
    // * dfu detach: bmRequestType=0x21, bRequest=0x00
    struct usbdevfs_ctrltransfer ct{};
    ct.bRequestType = 0x21;
    ct.bRequest     = 0x00;
    ct.wValue       = 5000;
    ct.wIndex       = 0;
    ct.wLength      = 0;
    ct.timeout      = 2000;
    ct.data         = nullptr;
    return ioctl((int)handle, USBDEVFS_CONTROL, &ct) >= 0;
}

#endif // __linux__
