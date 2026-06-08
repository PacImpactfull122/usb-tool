#ifdef _WIN32

#include "usb.h"
#include <windows.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <usb.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <cstdio>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

// * WideCharToMultiByte com CP_UTF8 garante suporte a caminhos com caracteres nao-ascii
static std::string wstrParaStr(const std::wstring& ws) {
    if (ws.empty()) return "";
    int tam = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (tam <= 0) return "";
    std::string s(tam - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), tam, nullptr, nullptr);
    return s;
}

// * MultiByteToWideChar necessario para caminhos com caracteres fora do ascii basico
static std::wstring strParaWstr(const std::string& s) {
    if (s.empty()) return {};
    int tam = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (tam <= 0) return {};
    std::wstring ws(tam - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), tam);
    return ws;
}

std::vector<DispositivoUsb> enumerarDispositivos() {
    std::vector<DispositivoUsb> lista;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return lista;

    SP_DEVINFO_DATA devInfoData{};
    devInfoData.cbSize = sizeof(devInfoData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        DispositivoUsb dev{};

        wchar_t hwId[512]{};
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData,
                SPDRP_HARDWAREID, nullptr, (PBYTE)hwId, sizeof(hwId), nullptr)) {
            std::wstring hw(hwId);
            auto posVid = hw.find(L"VID_");
            auto posPid = hw.find(L"PID_");
            if (posVid != std::wstring::npos)
                dev.idVendor  = (uint16_t)std::stoul(hw.substr(posVid + 4, 4), nullptr, 16);
            if (posPid != std::wstring::npos)
                dev.idProduct = (uint16_t)std::stoul(hw.substr(posPid + 4, 4), nullptr, 16);
        }

        wchar_t desc[256]{};
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData,
                SPDRP_DEVICEDESC, nullptr, (PBYTE)desc, sizeof(desc), nullptr))
            dev.produto = wstrParaStr(desc);

        SP_DEVICE_INTERFACE_DATA ifData{};
        ifData.cbSize = sizeof(ifData);
        if (SetupDiEnumDeviceInterfaces(hDevInfo, &devInfoData,
                &GUID_DEVINTERFACE_USB_DEVICE, 0, &ifData)) {
            DWORD tamReq = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, nullptr, 0, &tamReq, nullptr);
            std::vector<BYTE> buf(tamReq);
            auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData,
                    detail, tamReq, nullptr, nullptr))
                dev.caminho = wstrParaStr(detail->DevicePath);
        }

        if (dev.idVendor || dev.idProduct) lista.push_back(dev);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return lista;
}

intptr_t abrirDispositivo(const DispositivoUsb& dev) {
    std::wstring ws = strParaWstr(dev.caminho);
    HANDLE h = CreateFileW(ws.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileW(ws.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return (intptr_t)-1;
    return (intptr_t)h;
}

void fecharDispositivo(intptr_t handle) {
    if (handle < 0) return;
    HANDLE h = (HANDLE)handle;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

bool resetarDispositivo(intptr_t handle) {
    DWORD bytes = 0;
    return DeviceIoControl((HANDLE)handle, IOCTL_USB_RESET_PORT,
                           nullptr, 0, nullptr, 0, &bytes, nullptr) != 0;
}

std::vector<uint8_t> lerDescritor(intptr_t handle, uint8_t tipo, uint8_t indice) {
    struct {
        USB_DESCRIPTOR_REQUEST req;
        uint8_t dados[255];
    } buf{};

    buf.req.ConnectionIndex       = 0;
    buf.req.SetupPacket.bmRequest = 0x80;
    buf.req.SetupPacket.bRequest  = 0x06;
    buf.req.SetupPacket.wValue    = (uint16_t)((tipo << 8) | indice);
    buf.req.SetupPacket.wIndex    = 0;
    buf.req.SetupPacket.wLength   = 255;

    DWORD retornado = 0;
    if (!DeviceIoControl((HANDLE)handle,
                         IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                         &buf, sizeof(buf), &buf, sizeof(buf), &retornado, nullptr))
        return {};

    uint32_t tam = retornado > sizeof(USB_DESCRIPTOR_REQUEST)
                   ? retornado - (uint32_t)sizeof(USB_DESCRIPTOR_REQUEST) : 0;
    return std::vector<uint8_t>(buf.dados, buf.dados + tam);
}

bool transferirControle(intptr_t handle,
                        uint8_t bmRequestType, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex,
                        std::vector<uint8_t>& dados, bool enviar) {
    // ! requer winusb ou driver de kernel customizado instalado para o dispositivo
    (void)handle; (void)bmRequestType; (void)bRequest;
    (void)wValue; (void)wIndex; (void)dados; (void)enviar;
    std::fprintf(stderr, "transferencia de controle requer winusb instalado para o dispositivo\n");
    return false;
}

bool transferirBulk(intptr_t handle, uint8_t endpoint,
                    std::vector<uint8_t>& dados, bool enviar) {
    // ! requer winusb ou driver de kernel customizado instalado para o dispositivo
    (void)handle; (void)endpoint; (void)dados; (void)enviar;
    std::fprintf(stderr, "transferencia bulk requer winusb instalado para o dispositivo\n");
    return false;
}

bool lerSetor(intptr_t handle, uint64_t lba, uint32_t qtd, std::vector<uint8_t>& buf) {
    const uint32_t tamSetor = 512;
    buf.resize((size_t)(qtd * tamSetor));

    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)(lba * tamSetor);

    OVERLAPPED ov{};
    ov.Offset     = offset.LowPart;
    ov.OffsetHigh = (DWORD)offset.HighPart;
    ov.hEvent     = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    DWORD lido = 0;
    BOOL ok = ReadFile((HANDLE)handle, buf.data(), (DWORD)buf.size(), &lido, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult((HANDLE)handle, &ov, &lido, TRUE);

    CloseHandle(ov.hEvent);
    if (!ok) return false;
    buf.resize(lido);
    return true;
}

bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                   const std::vector<uint8_t>& dados) {
    (void)qtd;
    const uint32_t tamSetor = 512;
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)(lba * tamSetor);

    OVERLAPPED ov{};
    ov.Offset     = offset.LowPart;
    ov.OffsetHigh = (DWORD)offset.HighPart;
    ov.hEvent     = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    DWORD escrito = 0;
    BOOL ok = WriteFile((HANDLE)handle, dados.data(), (DWORD)dados.size(), &escrito, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult((HANDLE)handle, &ov, &escrito, TRUE);

    CloseHandle(ov.hEvent);
    return ok != 0;
}

bool entrarModoBootloader(intptr_t handle) {
    // ! requer winusb ou driver dfu instalado para o dispositivo
    (void)handle;
    std::fprintf(stderr, "modo bootloader requer winusb ou driver dfu instalado\n");
    return false;
}

#endif // _WIN32
