#include "descritor.h"
#include <cstdio>
#include <cstring>
#include <cctype>

// * tabela de usage pages hid mais comuns
static const char* usagePage(uint16_t pg) {
    switch (pg) {
        case 0x01: return "generic desktop";
        case 0x02: return "simulation";
        case 0x03: return "vr";
        case 0x04: return "sport";
        case 0x05: return "game";
        case 0x06: return "generic device";
        case 0x07: return "keyboard";
        case 0x08: return "leds";
        case 0x09: return "button";
        case 0x0c: return "consumer";
        case 0x0d: return "digitizer";
        case 0x0f: return "haptics";
        case 0x40: return "medical";
        case 0x84: return "power";
        case 0xff00: return "vendor especifico";
        default:   return "desconhecido";
    }
}

// * usages do generic desktop page mais usados
static const char* usageGenericDesktop(uint16_t u) {
    switch (u) {
        case 0x01: return "pointer";
        case 0x02: return "mouse";
        case 0x04: return "joystick";
        case 0x05: return "gamepad";
        case 0x06: return "keyboard";
        case 0x07: return "keypad";
        case 0x08: return "multi-axis";
        case 0x30: return "x";
        case 0x31: return "y";
        case 0x32: return "z";
        case 0x33: return "rx";
        case 0x34: return "ry";
        case 0x35: return "rz";
        case 0x36: return "slider";
        case 0x37: return "dial";
        case 0x38: return "wheel";
        case 0x39: return "hat switch";
        case 0x3a: return "counted buffer";
        case 0x3b: return "byte count";
        case 0x3c: return "motion wakeup";
        case 0x40: return "vx";
        case 0x41: return "vy";
        case 0x42: return "vz";
        case 0x43: return "vbrx";
        case 0x44: return "vbry";
        case 0x45: return "vbrz";
        case 0x46: return "vno";
        case 0x80: return "system control";
        case 0x81: return "system power down";
        case 0x82: return "system sleep";
        case 0x83: return "system wake up";
        default:   return nullptr;
    }
}

static int32_t lerSinalado(const uint8_t* p, int tam) {
    int32_t v = 0;
    for (int i = 0; i < tam; ++i) v |= (p[i] << (8 * i));
    if (tam == 1 && (v & 0x80))  v |= 0xffffff00;
    if (tam == 2 && (v & 0x8000)) v |= 0xffff0000;
    return v;
}

static uint32_t lerUnsigned(const uint8_t* p, int tam) {
    uint32_t v = 0;
    for (int i = 0; i < tam; ++i) v |= (static_cast<uint32_t>(p[i]) << (8 * i));
    return v;
}

void imprimirRelatorioHid(const std::vector<uint8_t>& blob) {
    if (blob.empty()) { std::printf("  descritor vazio\n"); return; }

    std::printf("  hid report descriptor (%zu bytes)\n\n", blob.size());

    // * exibe hex bruto primeiro para referencia
    std::printf("  hex: ");
    for (size_t i = 0; i < blob.size(); ++i) {
        if (i && i % 16 == 0) std::printf("\n       ");
        std::printf("%02x ", blob[i]);
    }
    std::printf("\n\n  itens decodificados:\n\n");

    size_t i = 0;
    int indent = 0;
    uint16_t paginaUsageAtual = 0;

    while (i < blob.size()) {
        uint8_t prefixo = blob[i++];

        // * item longo: prefixo 0xfe, tamanho no proximo byte
        if (prefixo == 0xfe) {
            if (i >= blob.size()) break;
            uint8_t tam = blob[i++];
            std::printf("%*s[item longo %d bytes]\n", indent * 2 + 4, "", tam);
            i += tam;
            continue;
        }

        int    tamDado = prefixo & 0x03;
        if (tamDado == 3) tamDado = 4;
        uint8_t tipo   = (prefixo >> 2) & 0x03;  // 0=main 1=global 2=local
        uint8_t tag    = (prefixo >> 4) & 0x0f;

        if (i + tamDado > blob.size()) break;

        int32_t  valorS = tamDado > 0 ? lerSinalado (&blob[i], tamDado) : 0;
        uint32_t valorU = tamDado > 0 ? lerUnsigned (&blob[i], tamDado) : 0;
        i += tamDado;

        // * itens main afetam o indentacao de colecoes
        if (tipo == 0) {
            switch (tag) {
                case 0x08: { // input
                    std::printf("%*sInput(", indent * 2 + 4, "");
                    if (valorU & 0x01) std::printf("Const,"); else std::printf("Data,");
                    if (valorU & 0x02) std::printf("Var,");   else std::printf("Array,");
                    if (valorU & 0x04) std::printf("Rel");    else std::printf("Abs");
                    if (valorU & 0x08) std::printf(",Wrap");
                    if (valorU & 0x10) std::printf(",NonLinear");
                    if (valorU & 0x20) std::printf(",NoPref");
                    if (valorU & 0x40) std::printf(",Null");
                    if (valorU & 0x80) std::printf(",Buf");
                    std::printf(")  [0x%02x]\n", valorU & 0xff);
                    break;
                }
                case 0x09: { // output
                    std::printf("%*sOutput(0x%02x)\n", indent * 2 + 4, "", valorU & 0xff);
                    break;
                }
                case 0x0b: { // feature
                    std::printf("%*sFeature(0x%02x)\n", indent * 2 + 4, "", valorU & 0xff);
                    break;
                }
                case 0x0a: { // collection
                    const char* tc = "unknown";
                    switch (valorU) {
                        case 0x00: tc = "Physical"; break;
                        case 0x01: tc = "Application"; break;
                        case 0x02: tc = "Logical"; break;
                        case 0x03: tc = "Report"; break;
                        case 0x04: tc = "Named Array"; break;
                        case 0x05: tc = "Usage Switch"; break;
                    }
                    std::printf("%*sCollection(%s)\n", indent * 2 + 4, "", tc);
                    indent++;
                    break;
                }
                case 0x0c: // end collection
                    if (indent > 0) indent--;
                    std::printf("%*sEnd Collection\n", indent * 2 + 4, "");
                    break;
                default:
                    std::printf("%*s[main tag=0x%x val=0x%x]\n", indent * 2 + 4, "", tag, valorU);
            }
        } else if (tipo == 1) { // global
            switch (tag) {
                case 0x00:
                    paginaUsageAtual = static_cast<uint16_t>(valorU);
                    std::printf("%*sUsage Page(%s)  [0x%04x]\n",
                                indent * 2 + 4, "", usagePage(paginaUsageAtual), paginaUsageAtual);
                    break;
                case 0x01:
                    std::printf("%*sLogical Min(%d)\n", indent * 2 + 4, "", valorS);
                    break;
                case 0x02:
                    std::printf("%*sLogical Max(%d)\n", indent * 2 + 4, "", valorS);
                    break;
                case 0x03:
                    std::printf("%*sPhysical Min(%d)\n", indent * 2 + 4, "", valorS);
                    break;
                case 0x04:
                    std::printf("%*sPhysical Max(%d)\n", indent * 2 + 4, "", valorS);
                    break;
                case 0x05:
                    std::printf("%*sUnit Exp(%d)\n", indent * 2 + 4, "", valorS);
                    break;
                case 0x06:
                    std::printf("%*sUnit(0x%x)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x07:
                    std::printf("%*sReport Size(%u bits)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x08:
                    std::printf("%*sReport ID(0x%02x)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x09:
                    std::printf("%*sReport Count(%u)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x0a:
                    std::printf("%*sPush\n", indent * 2 + 4, "");
                    break;
                case 0x0b:
                    std::printf("%*sPop\n", indent * 2 + 4, "");
                    break;
                default:
                    std::printf("%*s[global tag=0x%x val=0x%x]\n", indent * 2 + 4, "", tag, valorU);
            }
        } else { // local
            switch (tag) {
                case 0x00: {
                    const char* nome = nullptr;
                    if (paginaUsageAtual == 0x01) nome = usageGenericDesktop(static_cast<uint16_t>(valorU));
                    if (nome)
                        std::printf("%*sUsage(%s)  [0x%04x]\n", indent * 2 + 4, "", nome, valorU);
                    else
                        std::printf("%*sUsage(0x%04x)\n", indent * 2 + 4, "", valorU);
                    break;
                }
                case 0x01:
                    std::printf("%*sUsage Min(0x%04x)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x02:
                    std::printf("%*sUsage Max(0x%04x)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x03:
                    std::printf("%*sDesignator Index(0x%x)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x07:
                    std::printf("%*sString Index(%u)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x08:
                    std::printf("%*sString Min(%u)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x09:
                    std::printf("%*sString Max(%u)\n", indent * 2 + 4, "", valorU);
                    break;
                case 0x0a:
                    std::printf("%*sDelimiter(%u)\n", indent * 2 + 4, "", valorU);
                    break;
                default:
                    std::printf("%*s[local tag=0x%x val=0x%x]\n", indent * 2 + 4, "", tag, valorU);
            }
        }
    }
}

std::string tipoTransferencia(uint8_t bmAttr) {
    switch (bmAttr & 0x03) {
        case 0x00: return "control";
        case 0x01: return "isochronous";
        case 0x02: return "bulk";
        case 0x03: return "interrupt";
        default:   return "?";
    }
}

std::string direcaoEndpoint(uint8_t addr) {
    return (addr & 0x80) ? "IN" : "OUT";
}

static const char* nomeClasseIface(uint8_t cls) {
    switch (cls) {
        case 0x01: return "audio";
        case 0x02: return "cdc control";
        case 0x03: return "hid";
        case 0x05: return "fisico";
        case 0x06: return "imagem";
        case 0x07: return "impressora";
        case 0x08: return "mass storage";
        case 0x0a: return "cdc dados";
        case 0x0b: return "cartao inteligente";
        case 0x0e: return "video";
        case 0x0f: return "saude pessoal";
        case 0xe0: return "wireless";
        case 0xfe: return "aplicacao especifica";
        case 0xff: return "fabricante";
        default:   return "desconhecida";
    }
}

ConfiguracaoUsb parsearConfiguracao(const std::vector<uint8_t>& blob) {
    ConfiguracaoUsb cfg{};
    if (blob.size() < 9) return cfg;

    cfg.totalBytes    = static_cast<uint16_t>(blob[2] | (blob[3] << 8));
    cfg.numInterfaces = blob[4];
    cfg.valorConfig   = blob[5];

    size_t pos = 0;
    InterfaceInfo* ifaceAtual = nullptr;

    while (pos + 2 <= blob.size()) {
        uint8_t tam  = blob[pos];
        uint8_t tipo = blob[pos + 1];
        if (tam < 2 || pos + tam > blob.size()) break;

        if (tipo == 0x04 && tam >= 9) {
            InterfaceInfo iface{};
            iface.numero      = blob[pos + 2];
            iface.alternativo = blob[pos + 3];
            iface.classe      = blob[pos + 5];
            iface.subclasse   = blob[pos + 6];
            iface.protocolo   = blob[pos + 7];
            iface.iString     = blob[pos + 8];
            cfg.interfaces.push_back(iface);
            ifaceAtual = &cfg.interfaces.back();
        } else if (tipo == 0x05 && tam >= 7) {
            EndpointInfo ep{};
            ep.endereco  = blob[pos + 2];
            ep.atributos = blob[pos + 3];
            ep.maxPacote = static_cast<uint16_t>(blob[pos + 4] | (blob[pos + 5] << 8));
            ep.intervalo = blob[pos + 6];
            ep.iface     = ifaceAtual ? ifaceAtual->numero : 0;
            if (ifaceAtual) ifaceAtual->endpoints.push_back(ep);
            cfg.endpoints.push_back(ep);
        }
        pos += tam;
    }
    return cfg;
}

void imprimirConfiguracao(const ConfiguracaoUsb& cfg) {
    std::printf("  configuracao: %d interface(s)  total: %d bytes\n\n",
                cfg.numInterfaces, cfg.totalBytes);

    for (const auto& ep : cfg.endpoints) {
        std::printf("  endpoint 0x%02x  iface=%d  %-12s %-4s  max=%4d bytes  intervalo=%d\n",
                    ep.endereco, ep.iface,
                    tipoTransferencia(ep.atributos).c_str(),
                    direcaoEndpoint(ep.endereco).c_str(),
                    ep.maxPacote, ep.intervalo);
    }
}

void imprimirConfiguracaoCompleta(const ConfiguracaoUsb& cfg) {
    std::printf("  configuracao %d  |  %d interface(s)  |  %d bytes total\n\n",
                cfg.valorConfig, cfg.numInterfaces, cfg.totalBytes);

    for (const auto& iface : cfg.interfaces) {
        std::printf("  interface %d (alt=%d)\n", iface.numero, iface.alternativo);
        std::printf("    classe    : 0x%02x (%s)\n", iface.classe, nomeClasseIface(iface.classe));
        std::printf("    subclasse : 0x%02x\n", iface.subclasse);
        std::printf("    protocolo : 0x%02x\n", iface.protocolo);
        std::printf("    endpoints : %zu\n", iface.endpoints.size());

        for (const auto& ep : iface.endpoints) {
            std::printf("      ep 0x%02x  %-12s %-4s  max=%4d  intervalo=%d\n",
                        ep.endereco,
                        tipoTransferencia(ep.atributos).c_str(),
                        direcaoEndpoint(ep.endereco).c_str(),
                        ep.maxPacote, ep.intervalo);
        }
        std::putchar('\n');
    }
}

void imprimirDescritorDispositivo(const std::vector<uint8_t>& blob) {
    if (blob.size() < 18) { std::printf("  descritor incompleto (%zu bytes)\n", blob.size()); return; }

    uint16_t bcd    = static_cast<uint16_t>(blob[2]  | (blob[3]  << 8));
    uint16_t vendor = static_cast<uint16_t>(blob[8]  | (blob[9]  << 8));
    uint16_t product= static_cast<uint16_t>(blob[10] | (blob[11] << 8));
    uint16_t bcdDev = static_cast<uint16_t>(blob[12] | (blob[13] << 8));

    std::printf("  bcdUSB           : %x.%02x\n",   bcd >> 8, bcd & 0xff);
    std::printf("  bDeviceClass     : 0x%02x (%s)\n", blob[4], nomeClasseIface(blob[4]));
    std::printf("  bDeviceSubClass  : 0x%02x\n",      blob[5]);
    std::printf("  bDeviceProtocol  : 0x%02x\n",      blob[6]);
    std::printf("  bMaxPacketSize0  : %d bytes\n",    blob[7]);
    std::printf("  idVendor         : %04x\n",        vendor);
    std::printf("  idProduct        : %04x\n",        product);
    std::printf("  bcdDevice        : %x.%02x\n",    bcdDev >> 8, bcdDev & 0xff);
    std::printf("  iManufacturer    : string[%d]\n",  blob[14]);
    std::printf("  iProduct         : string[%d]\n",  blob[15]);
    std::printf("  iSerialNumber    : string[%d]\n",  blob[16]);
    std::printf("  bNumConfigurations: %d\n",         blob[17]);
}

std::string parsearStringDescriptor(const std::vector<uint8_t>& blob) {
    // * string descriptor: byte0=len byte1=0x03 seguido de utf16le
    if (blob.size() < 2 || blob[1] != 0x03) return "";
    std::string s;
    for (size_t i = 2; i + 1 < blob.size(); i += 2) {
        uint16_t cp = static_cast<uint16_t>(blob[i] | (blob[i+1] << 8));
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xc0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            s += static_cast<char>(0xe0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            s += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }
    return s;
}

void hexdump(const uint8_t* dados, size_t tam, size_t baseOffset) {
    for (size_t i = 0; i < tam; i += 16) {
        std::printf("  %04zx  ", baseOffset + i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < tam) std::printf("%02x ", dados[i + j]);
            else             std::printf("   ");
            if (j == 7) std::printf(" ");
        }
        std::printf(" |");
        for (size_t j = 0; j < 16 && i + j < tam; ++j) {
            uint8_t c = dados[i + j];
            std::printf("%c", (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '.');
        }
        std::printf("|\n");
    }
}
