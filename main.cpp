#include "usb.h"
#include "descritor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>

static volatile bool rodando = true;

static void tratarSinal(int) { rodando = false; }

static void exibirAjuda(const char* prog) {
    std::printf(
        "uso: %s <comando> [args]\n"
        "\n"
        "inspecao:\n"
        "  listar\n"
        "  info           <indice>\n"
        "  decodificar    <indice>           -- todos os descritores estruturados\n"
        "  dump-hid       <indice>           -- hid report descriptor decodificado\n"
        "  scan-ep        <indice>           -- todos os endpoints e atributos\n"
        "  strings        <indice>           -- todos os string descriptors\n"
        "\n"
        "transferencias:\n"
        "  resetar        <indice>\n"
        "  descritor      <indice> <tipo_hex> [indice_hex]\n"
        "  controle       <indice> <bmRT> <bReq> <wVal> <wIdx> <hex_dados> <r|w>\n"
        "  bulk           <indice> <endpoint> <hex_dados> <r|w>\n"
        "  hid-ler        <indice> <endpoint> [tam_buf] [timeout_ms]\n"
        "\n"
        "storage:\n"
        "  ler-setor      <indice> <lba> <qtd>\n"
        "  escrever-setor <indice> <lba> <hex_dados>\n"
        "\n"
        "outros:\n"
        "  bootloader     <indice>\n"
        "  monitorar      [bus] [addr] [max_pacotes]  -- captura via usbmon\n",
        prog ? prog : "usbctl");
}

static std::vector<uint8_t> hexParaBytes(const char* hex) {
    std::vector<uint8_t> bytes;
    size_t len = std::strlen(hex);
    for (size_t i = 0; i + 1 < len; i += 2) {
        char buf[3] = { hex[i], hex[i + 1], '\0' };
        bytes.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return bytes;
}

static void imprimirBytes(const std::vector<uint8_t>& dados) {
    for (size_t i = 0; i < dados.size(); ++i) {
        if (i && i % 16 == 0) std::putchar('\n');
        std::printf("%02x ", dados[i]);
    }
    std::putchar('\n');
}

static const char* nomeclasse(uint8_t cls) {
    switch (cls) {
        case 0x00: return "definida por interface";
        case 0x02: return "comunicacao";
        case 0x03: return "hid";
        case 0x06: return "imagem";
        case 0x07: return "impressora";
        case 0x08: return "armazenamento em massa";
        case 0x09: return "hub";
        case 0x0e: return "video";
        case 0xe0: return "controlador wireless";
        case 0xff: return "especifico de fabricante";
        default:   return "desconhecida";
    }
}

static void imprimirDispositivo(int idx, const DispositivoUsb& dev) {
    std::printf("[%d] %04x:%04x  %s %s  serial: %s\n",
                idx, dev.idVendor, dev.idProduct,
                dev.fabricante.c_str(), dev.produto.c_str(),
                dev.serial.empty() ? "-" : dev.serial.c_str());
    std::printf("     classe: %s (%02x)  velocidade: %s Mbit/s\n",
                nomeclasse(dev.classe), dev.classe,
                dev.velocidade.empty() ? "?" : dev.velocidade.c_str());
    std::printf("     %s\n", dev.caminho.c_str());
}

static void imprimirInfo(int idx, const DispositivoUsb& dev) {
    std::printf("dispositivo [%d]\n", idx);
    std::printf("  vendor id  : %04x\n", dev.idVendor);
    std::printf("  product id : %04x\n", dev.idProduct);
    std::printf("  fabricante : %s\n", dev.fabricante.empty() ? "-" : dev.fabricante.c_str());
    std::printf("  produto    : %s\n", dev.produto.empty() ? "-" : dev.produto.c_str());
    std::printf("  serial     : %s\n", dev.serial.empty() ? "-" : dev.serial.c_str());
    std::printf("  classe     : %s (%02x/%02x/%02x)\n",
                nomeclasse(dev.classe), dev.classe, dev.subclasse, dev.protocolo);
    std::printf("  velocidade : %s Mbit/s\n",
                dev.velocidade.empty() ? "?" : dev.velocidade.c_str());
    std::printf("  barramento : %d  endereco: %d\n", dev.barramento, dev.endereco);
    std::printf("  caminho    : %s\n", dev.caminho.c_str());
}

static int executarComando(const std::string& cmd, int argc, char* argv[],
                           intptr_t handle, int idx) {
    if (cmd == "resetar") {
        if (!resetarDispositivo(handle)) {
            std::fprintf(stderr, "falha ao resetar\n");
            return 1;
        }
        std::printf("ok\n");
        return 0;
    }

    if (cmd == "decodificar") {
        std::printf("=== device descriptor ===\n");
        auto dev = lerDescritor(handle, 0x01, 0);
        if (!dev.empty()) {
            imprimirDescritorDispositivo(dev);
            std::printf("\n  raw:\n");
            hexdump(dev.data(), dev.size());
        }

        std::printf("\n=== configuration descriptor ===\n");
        auto cfg = lerConfiguracaoCompleta(handle);
        if (!cfg.empty()) {
            auto parsed = parsearConfiguracao(cfg);
            imprimirConfiguracao(parsed);
            std::printf("\n  raw (%zu bytes):\n", cfg.size());
            hexdump(cfg.data(), cfg.size());
        }

        std::printf("\n=== string descriptors ===\n");
        auto strings = lerStrings(handle);
        for (size_t i = 0; i < strings.size(); ++i)
            std::printf("  [%zu] %s\n", i + 1, strings[i].c_str());

        return 0;
    }

    if (cmd == "dump-hid") {
        auto blob = lerRelatorioHid(handle);
        if (blob.empty()) {
            std::fprintf(stderr, "falha ao ler hid report descriptor (dispositivo e hid?)\n");
            return 1;
        }
        imprimirRelatorioHid(blob);
        return 0;
    }

    if (cmd == "scan-ep") {
        auto blob = lerConfiguracaoCompleta(handle);
        if (blob.empty()) {
            std::fprintf(stderr, "falha ao ler configuracao\n");
            return 1;
        }
        auto cfg = parsearConfiguracao(blob);
        std::printf("dispositivo %d: %zu endpoint(s) encontrado(s)\n\n",
                    idx, cfg.endpoints.size());

        for (const auto& ep : cfg.endpoints) {
            std::string tipo = tipoTransferencia(ep.atributos);
            std::string dir  = direcaoEndpoint(ep.endereco);
            std::printf("  ep 0x%02x  iface=%d  %-12s %-4s  max=%4d bytes  intervalo=%d\n",
                        ep.endereco, ep.iface,
                        tipo.c_str(), dir.c_str(),
                        ep.maxPacote, ep.intervalo);
        }
        return 0;
    }

    if (cmd == "strings") {
        auto strings = lerStrings(handle);
        if (strings.empty()) {
            std::printf("nenhuma string encontrada\n");
            return 0;
        }
        for (size_t i = 0; i < strings.size(); ++i)
            std::printf("[%zu] %s\n", i + 1, strings[i].c_str());
        return 0;
    }

    if (cmd == "descritor") {
        uint8_t tipo = argc > 3 ? static_cast<uint8_t>(std::strtoul(argv[3], nullptr, 16)) : 0x01;
        uint8_t ind  = argc > 4 ? static_cast<uint8_t>(std::strtoul(argv[4], nullptr, 16)) : 0x00;
        auto dados = lerDescritor(handle, tipo, ind);
        if (dados.empty()) { std::fprintf(stderr, "falha ao ler descritor\n"); return 1; }
        hexdump(dados.data(), dados.size());
        return 0;
    }

    if (cmd == "controle") {
        if (argc < 9) { exibirAjuda(argv[0]); return 1; }
        uint8_t  bmRT   = static_cast<uint8_t> (std::strtoul(argv[3], nullptr, 16));
        uint8_t  bReq   = static_cast<uint8_t> (std::strtoul(argv[4], nullptr, 16));
        uint16_t wVal   = static_cast<uint16_t>(std::strtoul(argv[5], nullptr, 16));
        uint16_t wIdx   = static_cast<uint16_t>(std::strtoul(argv[6], nullptr, 16));
        bool     enviar = (argv[8][0] == 'w');
        auto dados = hexParaBytes(argv[7]);
        if (!transferirControle(handle, bmRT, bReq, wVal, wIdx, dados, enviar)) {
            std::fprintf(stderr, "falha na transferencia de controle\n");
            return 1;
        }
        if (!enviar) { hexdump(dados.data(), dados.size()); }
        else std::printf("ok\n");
        return 0;
    }

    if (cmd == "bulk") {
        if (argc < 6) { exibirAjuda(argv[0]); return 1; }
        uint8_t ep     = static_cast<uint8_t>(std::strtoul(argv[3], nullptr, 16));
        bool    enviar = (argv[5][0] == 'w');
        auto dados = hexParaBytes(argv[4]);
        if (!transferirBulk(handle, ep, dados, enviar)) {
            std::fprintf(stderr, "falha na transferencia bulk\n");
            return 1;
        }
        if (!enviar) { hexdump(dados.data(), dados.size()); }
        else std::printf("ok\n");
        return 0;
    }

    if (cmd == "hid-ler") {
        if (argc < 4) { exibirAjuda(argv[0]); return 1; }
        uint8_t  ep      = static_cast<uint8_t> (std::strtoul(argv[3], nullptr, 16));
        uint32_t tam     = argc > 4 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 8;
        uint32_t timeout = argc > 5 ? static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10)) : 5000;

        // * desconecta hid do kernel para liberar acesso via usbfs
        desconectarDriver(handle, 0);
        if (!reivindicarInterface(handle, 0)) {
            std::fprintf(stderr, "falha ao reivindicar interface (dispositivo em uso?)\n");
            return 1;
        }

        std::signal(SIGINT, tratarSinal);
        std::printf("lendo hid reports do endpoint %02x (ctrl+c para parar)\n\n", ep);

        uint64_t pacote = 0;
        while (rodando) {
            std::vector<uint8_t> buf(tam, 0);
            if (!transferirInterrupt(handle, ep, buf, timeout)) {
                if (!rodando) break;
                std::fprintf(stderr, "timeout ou erro na leitura\n");
                continue;
            }
            std::printf("[%4llu] ", static_cast<unsigned long long>(pacote++));
            imprimirBytes(buf);
        }

        liberarInterface(handle, 0);
        return 0;
    }

    if (cmd == "ler-setor") {
        if (argc < 5) { exibirAjuda(argv[0]); return 1; }
        uint64_t lba = std::strtoull(argv[3], nullptr, 10);
        uint32_t qtd = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
        std::vector<uint8_t> buf;
        if (!lerSetor(handle, lba, qtd, buf)) {
            std::fprintf(stderr, "falha na leitura de setor\n");
            return 1;
        }
        hexdump(buf.data(), buf.size());
        return 0;
    }

    if (cmd == "escrever-setor") {
        // ! operacao irreversivel, sobrescreve dados no dispositivo
        if (argc < 5) { exibirAjuda(argv[0]); return 1; }
        uint64_t lba  = std::strtoull(argv[3], nullptr, 10);
        auto dados = hexParaBytes(argv[4]);
        if (dados.empty() || dados.size() % 512 != 0) {
            std::fprintf(stderr, "dados devem ser multiplo de 512 bytes\n");
            return 1;
        }
        uint32_t qtd = static_cast<uint32_t>(dados.size() / 512);
        if (!escreverSetor(handle, lba, qtd, dados)) {
            std::fprintf(stderr, "falha na escrita de setor\n");
            return 1;
        }
        std::printf("ok\n");
        return 0;
    }

    if (cmd == "bootloader") {
        if (!entrarModoBootloader(handle)) {
            std::fprintf(stderr, "falha (dispositivo suporta dfu?)\n");
            return 1;
        }
        std::printf("dfu detach enviado\n");
        return 0;
    }

    std::fprintf(stderr, "comando desconhecido: %s\n", cmd.c_str());
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) { exibirAjuda(argv[0]); return 1; }

    const std::string cmd = argv[1];

    if (cmd == "listar") {
        auto devs = enumerarDispositivos();
        if (devs.empty()) { std::printf("nenhum dispositivo encontrado\n"); return 0; }
        for (int i = 0; i < static_cast<int>(devs.size()); ++i) imprimirDispositivo(i, devs[i]);
        return 0;
    }

    // * monitorar nao precisa de indice de dispositivo
    if (cmd == "monitorar") {
        int      bus  = argc > 2 ? static_cast<int>(std::strtol(argv[2], nullptr, 10)) : -1;
        uint8_t  addr = argc > 3 ? static_cast<uint8_t>(std::strtoul(argv[3], nullptr, 10)) : 0;
        uint32_t maxP = argc > 4 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 0;
        monitorar(bus, addr, maxP);
        return 0;
    }

    if (argc < 3) { exibirAjuda(argv[0]); return 1; }

    auto devs = enumerarDispositivos();
    char* endptr = nullptr;
    long idxLong = std::strtol(argv[2], &endptr, 10);
    if (endptr == argv[2] || *endptr != '\0' || idxLong < 0 || idxLong >= static_cast<long>(devs.size())) {
        std::fprintf(stderr, "indice invalido: %s\n", argv[2]);
        return 1;
    }
    int idx = static_cast<int>(idxLong);

    if (cmd == "info") {
        imprimirInfo(idx, devs[idx]);
        return 0;
    }

    intptr_t handle = abrirDispositivo(devs[idx]);
    if (handle < 0) {
        std::fprintf(stderr, "falha ao abrir dispositivo (permissoes suficientes?)\n");
        return 1;
    }

    int ret = executarComando(cmd, argc, argv, handle, idx);
    fecharDispositivo(handle);
    return ret;
}
