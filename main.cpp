#include "usb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void exibirAjuda(const char* prog) {
    std::printf(
        "uso: %s <comando> [args]\n"
        "\n"
        "comandos:\n"
        "  listar\n"
        "  resetar        <indice>\n"
        "  descritor      <indice> <tipo_hex> [indice_hex]\n"
        "  controle       <indice> <bmRT> <bReq> <wVal> <wIdx> <hex_dados> <r|w>\n"
        "  bulk           <indice> <endpoint> <hex_dados> <r|w>\n"
        "  ler-setor      <indice> <lba> <qtd>\n"
        "  escrever-setor <indice> <lba> <hex_dados>\n"
        "  bootloader     <indice>\n",
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

static void imprimirDispositivo(int idx, const DispositivoUsb& dev) {
    std::printf("[%d] %04x:%04x  %s %s  serial: %s\n",
                idx, dev.idVendor, dev.idProduct,
                dev.fabricante.c_str(), dev.produto.c_str(),
                dev.serial.empty() ? "-" : dev.serial.c_str());
    std::printf("     %s\n", dev.caminho.c_str());
}

static int executarComando(const std::string& cmd, int argc, char* argv[],
                           intptr_t handle) {
    if (cmd == "resetar") {
        if (!resetarDispositivo(handle)) {
            std::fprintf(stderr, "falha ao resetar\n");
            return 1;
        }
        std::printf("ok\n");
        return 0;
    }

    if (cmd == "descritor") {
        uint8_t tipo = argc > 3 ? static_cast<uint8_t>(std::strtoul(argv[3], nullptr, 16)) : 0x01;
        uint8_t ind  = argc > 4 ? static_cast<uint8_t>(std::strtoul(argv[4], nullptr, 16)) : 0x00;
        auto dados = lerDescritor(handle, tipo, ind);
        if (dados.empty()) { std::fprintf(stderr, "falha ao ler descritor\n"); return 1; }
        imprimirBytes(dados);
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
        if (!enviar) imprimirBytes(dados);
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
        if (!enviar) imprimirBytes(dados);
        else std::printf("ok\n");
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
        imprimirBytes(buf);
        return 0;
    }

    if (cmd == "escrever-setor") {
        // ! operacao irreversivel, sobrescreve dados no dispositivo
        if (argc < 5) { exibirAjuda(argv[0]); return 1; }
        uint64_t lba   = std::strtoull(argv[3], nullptr, 10);
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

    if (argc < 3) { exibirAjuda(argv[0]); return 1; }

    auto devs = enumerarDispositivos();
    char* endptr = nullptr;
    long idxLong = std::strtol(argv[2], &endptr, 10);
    if (endptr == argv[2] || *endptr != '\0' || idxLong < 0 || idxLong >= static_cast<long>(devs.size())) {
        std::fprintf(stderr, "indice invalido: %s\n", argv[2]);
        return 1;
    }
    int idx = static_cast<int>(idxLong);

    intptr_t handle = abrirDispositivo(devs[idx]);
    if (handle < 0) {
        std::fprintf(stderr, "falha ao abrir dispositivo (permissoes suficientes?)\n");
        return 1;
    }

    int ret = executarComando(cmd, argc, argv, handle);
    fecharDispositivo(handle);
    return ret;
}
