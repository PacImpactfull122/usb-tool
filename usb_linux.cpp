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
#include <csignal>

// =============================================================================
// constantes
// =============================================================================

static constexpr uint32_t TAMANHO_SETOR       = 512u;
static constexpr uint32_t ASSINATURA_CBW      = 0x43425355u;
static constexpr uint32_t ASSINATURA_CSW      = 0x53425355u;
static constexpr uint8_t  FLAG_CBW_ENTRADA    = 0x80u;
static constexpr uint8_t  FLAG_CBW_SAIDA      = 0x00u;
static constexpr uint8_t  ENDPOINT_BULK_OUT   = 0x01u;
static constexpr uint8_t  ENDPOINT_BULK_IN    = 0x81u;
static constexpr uint32_t TIMEOUT_CONTROLE_MS = 2000u;
static constexpr uint32_t TIMEOUT_BULK_MS     = 2000u;
static constexpr uint32_t TIMEOUT_CBW_MS      = 5000u;
static constexpr uint32_t TIMEOUT_DADOS_MS    = 10000u;
static constexpr uint16_t IDIOMA_EN_US        = 0x0409u;
static constexpr uint8_t  CLASSE_HID          = 0x03u;
static constexpr uint8_t  TIPO_HID_REPORT     = 0x22u;
static constexpr size_t   TAMANHO_MAX_CONFIG   = 4096u;

// =============================================================================
// estruturas do protocolo bulk-only transport
// =============================================================================

// bloco de comando enviado pelo host antes de cada operacao scsi
struct __attribute__((packed)) BlocoComandoBulk {
    uint32_t assinatura;
    uint32_t tag;
    uint32_t tamanhoTransferencia;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  tamanhoCdb;
    uint8_t  cdb[16];
};

// bloco de status retornado pelo dispositivo apos a operacao
struct __attribute__((packed)) BlocoStatusBulk {
    uint32_t assinatura;
    uint32_t tag;
    uint32_t residuo;
    uint8_t  status;
};

// =============================================================================
// utilitarios de leitura do sysfs
// =============================================================================

static std::string lerArquivoSysfs(const std::string& caminho)
{
    std::ifstream arquivo(caminho);
    if (!arquivo) return {};

    std::string valor;
    std::getline(arquivo, valor);
    return valor;
}

static uint16_t converterHexParaU16(const std::string& texto)
{
    if (texto.empty()) return 0u;
    try   { return static_cast<uint16_t>(std::stoul(texto, nullptr, 16)); }
    catch (...) { return 0u; }
}

static int lerInteiroSysfs(const std::string& caminho)
{
    const std::string valor = lerArquivoSysfs(caminho);
    if (valor.empty()) return 0;
    try   { return std::stoi(valor); }
    catch (...) { return 0; }
}

static uint8_t lerHexadecimalU8Sysfs(const std::string& caminho)
{
    const std::string valor = lerArquivoSysfs(caminho);
    if (valor.empty()) return 0u;
    try   { return static_cast<uint8_t>(std::stoul(valor, nullptr, 16)); }
    catch (...) { return 0u; }
}

// =============================================================================
// enumeracao de dispositivos
// =============================================================================

std::vector<DispositivoUsb> enumerarDispositivos()
{
    std::vector<DispositivoUsb> lista;

    const std::string diretorioBase = "/sys/bus/usb/devices/";
    DIR* diretorio = opendir(diretorioBase.c_str());
    if (!diretorio) return lista;

    struct dirent* entrada;
    while ((entrada = readdir(diretorio)) != nullptr) {
        const std::string nome = entrada->d_name;

        // entradas de ponto e interfaces (com dois pontos) sao ignoradas
        if (nome == "." || nome == "..") continue;
        if (nome.find(':') != std::string::npos) continue;

        const std::string prefixoNo = diretorioBase + nome + "/";

        const std::string vendorId  = lerArquivoSysfs(prefixoNo + "idVendor");
        const std::string productId = lerArquivoSysfs(prefixoNo + "idProduct");
        if (vendorId.empty() || productId.empty()) continue;

        DispositivoUsb dispositivo{};
        dispositivo.idVendor   = converterHexParaU16(vendorId);
        dispositivo.idProduct  = converterHexParaU16(productId);
        dispositivo.fabricante = lerArquivoSysfs(prefixoNo + "manufacturer");
        dispositivo.produto    = lerArquivoSysfs(prefixoNo + "product");
        dispositivo.serial     = lerArquivoSysfs(prefixoNo + "serial");
        dispositivo.caminho    = prefixoNo;
        dispositivo.barramento = static_cast<uint8_t>(lerInteiroSysfs(prefixoNo + "busnum"));
        dispositivo.endereco   = static_cast<uint8_t>(lerInteiroSysfs(prefixoNo + "devnum"));
        dispositivo.classe     = lerHexadecimalU8Sysfs(prefixoNo + "bDeviceClass");
        dispositivo.subclasse  = lerHexadecimalU8Sysfs(prefixoNo + "bDeviceSubClass");
        dispositivo.protocolo  = lerHexadecimalU8Sysfs(prefixoNo + "bDeviceProtocol");
        dispositivo.versaoUsb  = static_cast<uint8_t>(lerInteiroSysfs(prefixoNo + "bcdUSB"));
        dispositivo.velocidade = lerArquivoSysfs(prefixoNo + "speed");

        lista.push_back(dispositivo);
    }

    closedir(diretorio);
    return lista;
}

// =============================================================================
// ciclo de vida do handle
// =============================================================================

intptr_t abrirDispositivo(const DispositivoUsb& dispositivo)
{
    char caminho[64];
    std::snprintf(caminho, sizeof(caminho),
                  "/dev/bus/usb/%03d/%03d",
                  dispositivo.barramento,
                  dispositivo.endereco);

    // tenta leitura e escrita, cai para somente leitura se necessario
    int descritor = open(caminho, O_RDWR);
    if (descritor < 0) descritor = open(caminho, O_RDONLY);
    return static_cast<intptr_t>(descritor);
}

void fecharDispositivo(intptr_t handle)
{
    if (handle >= 0) close(static_cast<int>(handle));
}

bool resetarDispositivo(intptr_t handle)
{
    return ioctl(static_cast<int>(handle), USBDEVFS_RESET, nullptr) == 0;
}

// =============================================================================
// transferencias de controle
// =============================================================================

std::vector<uint8_t> lerDescritor(intptr_t handle, uint8_t tipo, uint8_t indice)
{
    std::vector<uint8_t> buffer(255u, 0u);

    usbdevfs_ctrltransfer transferencia{};
    transferencia.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    transferencia.bRequest     = USB_REQ_GET_DESCRIPTOR;
    transferencia.wValue       = static_cast<uint16_t>((tipo << 8) | indice);
    transferencia.wIndex       = 0u;
    transferencia.wLength      = static_cast<uint16_t>(buffer.size());
    transferencia.timeout      = 1000u;
    transferencia.data         = buffer.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia);
    if (resultado < 0) return {};

    buffer.resize(static_cast<size_t>(resultado));
    return buffer;
}

bool transferirControle(intptr_t handle,
                        uint8_t  bmRequestType,
                        uint8_t  bRequest,
                        uint16_t wValue,
                        uint16_t wIndex,
                        std::vector<uint8_t>& dados,
                        bool enviar)
{
    usbdevfs_ctrltransfer transferencia{};
    transferencia.bRequestType = bmRequestType;
    transferencia.bRequest     = bRequest;
    transferencia.wValue       = wValue;
    transferencia.wIndex       = wIndex;
    transferencia.wLength      = static_cast<uint16_t>(dados.size());
    transferencia.timeout      = TIMEOUT_CONTROLE_MS;
    transferencia.data         = dados.empty() ? nullptr : dados.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia);
    if (resultado < 0) return false;

    if (!enviar) dados.resize(static_cast<size_t>(resultado));
    return true;
}

// =============================================================================
// transferencias bulk e interrupt
// =============================================================================

bool transferirBulk(intptr_t handle,
                    uint8_t endpoint,
                    std::vector<uint8_t>& dados,
                    bool enviar)
{
    usbdevfs_bulktransfer transferencia{};
    transferencia.ep      = endpoint;
    transferencia.len     = static_cast<uint32_t>(dados.size());
    transferencia.timeout = TIMEOUT_BULK_MS;
    transferencia.data    = dados.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_BULK, &transferencia);
    if (resultado < 0) return false;

    if (!enviar) dados.resize(static_cast<size_t>(resultado));
    return true;
}

bool transferirInterrupt(intptr_t handle,
                         uint8_t endpoint,
                         std::vector<uint8_t>& dados,
                         uint32_t timeoutMs)
{
    // kernels recentes unificaram interrupt com bulk no ioctl usbdevfs_bulk
    usbdevfs_bulktransfer transferencia{};
    transferencia.ep      = endpoint;
    transferencia.len     = static_cast<uint32_t>(dados.size());
    transferencia.timeout = timeoutMs;
    transferencia.data    = dados.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_BULK, &transferencia);
    if (resultado < 0) return false;

    dados.resize(static_cast<size_t>(resultado));
    return true;
}

// =============================================================================
// gerenciamento de interface
// =============================================================================

bool reivindicarInterface(intptr_t handle, uint32_t numeroInterface)
{
    unsigned int n = numeroInterface;
    return ioctl(static_cast<int>(handle), USBDEVFS_CLAIMINTERFACE, &n) == 0;
}

void liberarInterface(intptr_t handle, uint32_t numeroInterface)
{
    unsigned int n = numeroInterface;
    ioctl(static_cast<int>(handle), USBDEVFS_RELEASEINTERFACE, &n);
}

bool desconectarDriver(intptr_t handle, uint32_t numeroInterface)
{
    usbdevfs_ioctl comando{};
    comando.ifno       = static_cast<int>(numeroInterface);
    comando.ioctl_code = USBDEVFS_DISCONNECT;
    comando.data       = nullptr;

    // retorno negativo pode indicar ausencia de driver, nao e erro fatal
    ioctl(static_cast<int>(handle), USBDEVFS_IOCTL, &comando);
    return true;
}

// =============================================================================
// protocolo bulk-only transport (bot)
// =============================================================================

static bool executarComandoBulk(int descritor,
                                uint8_t endpointSaida,
                                uint8_t endpointEntrada,
                                const uint8_t* cdb,
                                uint8_t tamanhoCdb,
                                uint8_t* dados,
                                uint32_t tamanhosDados,
                                bool leitura)
{
    static uint32_t sequenciaTag = 1u;

    // monta o bloco de comando
    BlocoComandoBulk comando{};
    comando.assinatura          = ASSINATURA_CBW;
    comando.tag                 = sequenciaTag++;
    comando.tamanhoTransferencia = tamanhosDados;
    comando.flags               = leitura ? FLAG_CBW_ENTRADA : FLAG_CBW_SAIDA;
    comando.lun                 = 0u;
    comando.tamanhoCdb          = tamanhoCdb;
    std::memcpy(comando.cdb, cdb, tamanhoCdb);

    usbdevfs_bulktransfer transferencia{};

    // fase 1, envio do bloco de comando
    transferencia.ep      = endpointSaida;
    transferencia.len     = static_cast<uint32_t>(sizeof(BlocoComandoBulk));
    transferencia.timeout = TIMEOUT_CBW_MS;
    transferencia.data    = &comando;
    if (ioctl(descritor, USBDEVFS_BULK, &transferencia) < 0) return false;

    // fase 2, transferencia dos dados quando houver payload
    if (tamanhosDados > 0u) {
        transferencia.ep      = leitura ? endpointEntrada : endpointSaida;
        transferencia.len     = tamanhosDados;
        transferencia.timeout = TIMEOUT_DADOS_MS;
        transferencia.data    = dados;
        if (ioctl(descritor, USBDEVFS_BULK, &transferencia) < 0) return false;
    }

    // fase 3, leitura e validacao do bloco de status
    BlocoStatusBulk status{};
    transferencia.ep      = endpointEntrada;
    transferencia.len     = static_cast<uint32_t>(sizeof(BlocoStatusBulk));
    transferencia.timeout = TIMEOUT_CBW_MS;
    transferencia.data    = &status;
    if (ioctl(descritor, USBDEVFS_BULK, &transferencia) < 0) return false;

    return status.assinatura == ASSINATURA_CSW && status.status == 0u;
}

// =============================================================================
// operacoes de setor (scsi read10 / write10)
// =============================================================================

bool lerSetor(intptr_t handle, uint64_t lba, uint32_t quantidade, std::vector<uint8_t>& buffer)
{
    buffer.assign(static_cast<size_t>(quantidade * TAMANHO_SETOR), 0u);

    // cdb scsi read(10), opcode 0x28
    const uint8_t cdb[10] = {
        0x28u, 0x00u,
        static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
        static_cast<uint8_t>(lba >> 8),  static_cast<uint8_t>(lba),
        0x00u,
        static_cast<uint8_t>(quantidade >> 8), static_cast<uint8_t>(quantidade),
        0x00u
    };

    return executarComandoBulk(static_cast<int>(handle),
                               ENDPOINT_BULK_OUT, ENDPOINT_BULK_IN,
                               cdb, sizeof(cdb),
                               buffer.data(),
                               static_cast<uint32_t>(buffer.size()),
                               true);
}

bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t quantidade,
                   const std::vector<uint8_t>& dados)
{
    // cdb scsi write(10), opcode 0x2a
    const uint8_t cdb[10] = {
        0x2au, 0x00u,
        static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
        static_cast<uint8_t>(lba >> 8),  static_cast<uint8_t>(lba),
        0x00u,
        static_cast<uint8_t>(quantidade >> 8), static_cast<uint8_t>(quantidade),
        0x00u
    };

    // const_cast necessario pois o ioctl nao aceita ponteiro const, mas escrita nao modifica o buffer
    return executarComandoBulk(static_cast<int>(handle),
                               ENDPOINT_BULK_OUT, ENDPOINT_BULK_IN,
                               cdb, sizeof(cdb),
                               const_cast<uint8_t*>(dados.data()),
                               static_cast<uint32_t>(dados.size()),
                               false);
}

// =============================================================================
// bootloader e descritores avancados
// =============================================================================

bool entrarModoBootloader(intptr_t handle)
{
    // dfu detach, classe dfu, bRequest 0x00, wValue e o timeout em milissegundos
    usbdevfs_ctrltransfer transferencia{};
    transferencia.bRequestType = 0x21u;
    transferencia.bRequest     = 0x00u;
    transferencia.wValue       = 1000u;
    transferencia.wIndex       = 0u;
    transferencia.wLength      = 0u;
    transferencia.timeout      = TIMEOUT_CONTROLE_MS;
    transferencia.data         = nullptr;

    return ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia) >= 0;
}

std::vector<uint8_t> lerConfiguracaoCompleta(intptr_t handle)
{
    // primeira leitura de 9 bytes para descobrir o tamanho total do descritor
    std::vector<uint8_t> cabecalho(9u, 0u);

    usbdevfs_ctrltransfer transferencia{};
    transferencia.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    transferencia.bRequest     = USB_REQ_GET_DESCRIPTOR;
    transferencia.wValue       = static_cast<uint16_t>(USB_DT_CONFIG << 8);
    transferencia.wIndex       = 0u;
    transferencia.wLength      = 9u;
    transferencia.timeout      = 1000u;
    transferencia.data         = cabecalho.data();

    if (ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia) < 0) return {};

    const uint16_t tamanhoTotal = static_cast<uint16_t>(cabecalho[2] | (cabecalho[3] << 8));
    if (tamanhoTotal < 9u || tamanhoTotal > TAMANHO_MAX_CONFIG) return cabecalho;

    // segunda leitura para obter o blob completo com todas as interfaces e endpoints
    std::vector<uint8_t> descricaoCompleta(tamanhoTotal, 0u);
    transferencia.wLength = tamanhoTotal;
    transferencia.data    = descricaoCompleta.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia);
    if (resultado < 0) return cabecalho;

    descricaoCompleta.resize(static_cast<size_t>(resultado));
    return descricaoCompleta;
}

std::vector<uint8_t> lerRelatorioHid(intptr_t handle)
{
    // hid report descriptor, tipo 0x22, solicitado via interface com wIndex igual a zero
    std::vector<uint8_t> buffer(512u, 0u);

    usbdevfs_ctrltransfer transferencia{};
    transferencia.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE;
    transferencia.bRequest     = USB_REQ_GET_DESCRIPTOR;
    transferencia.wValue       = static_cast<uint16_t>(TIPO_HID_REPORT << 8);
    transferencia.wIndex       = 0u;
    transferencia.wLength      = static_cast<uint16_t>(buffer.size());
    transferencia.timeout      = 1000u;
    transferencia.data         = buffer.data();

    const int resultado = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia);
    if (resultado < 0) return {};

    buffer.resize(static_cast<size_t>(resultado));
    return buffer;
}

uint8_t detectarEndpointHid(intptr_t handle)
{
    const auto descricao = lerConfiguracaoCompleta(handle);
    if (descricao.size() < 9u) return 0u;

    size_t posicao      = 0u;
    bool emInterfaceHid = false;

    while (posicao + 2u <= descricao.size()) {
        const uint8_t tamanho = descricao[posicao];
        const uint8_t tipo    = descricao[posicao + 1u];

        if (tamanho < 2u || posicao + tamanho > descricao.size()) break;

        if (tipo == USB_DT_INTERFACE && tamanho >= 9u) {
            // bit de classe da interface, 0x03 identifica hid
            emInterfaceHid = (descricao[posicao + 5u] == CLASSE_HID);

        } else if (tipo == USB_DT_ENDPOINT && tamanho >= 7u && emInterfaceHid) {
            const uint8_t enderecoEp  = descricao[posicao + 2u];
            const uint8_t atributosEp = descricao[posicao + 3u];

            // interrupt in, bit7 indica direcao entrada, bits1:0 iguais a 11 indicam interrupt
            if ((enderecoEp & 0x80u) != 0u && (atributosEp & 0x03u) == 0x03u) {
                return enderecoEp;
            }
        }

        posicao += tamanho;
    }

    return 0u;
}

// =============================================================================
// strings de descricao
// =============================================================================

std::vector<std::string> lerStrings(intptr_t handle)
{
    std::vector<std::string> resultado;

    // indice 0 retorna a lista de idiomas, indices 1 a 127 contem as strings do dispositivo
    for (uint8_t indice = 1u; indice < 128u; ++indice) {
        std::vector<uint8_t> buffer(255u, 0u);

        usbdevfs_ctrltransfer transferencia{};
        transferencia.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        transferencia.bRequest     = USB_REQ_GET_DESCRIPTOR;
        transferencia.wValue       = static_cast<uint16_t>((USB_DT_STRING << 8) | indice);
        transferencia.wIndex       = IDIOMA_EN_US;
        transferencia.wLength      = static_cast<uint16_t>(buffer.size());
        transferencia.timeout      = 500u;
        transferencia.data         = buffer.data();

        const int resultado_ioctl = ioctl(static_cast<int>(handle), USBDEVFS_CONTROL, &transferencia);
        if (resultado_ioctl < 2) break;

        buffer.resize(static_cast<size_t>(resultado_ioctl));

        // decodifica utf-16le descartando codepoints acima de 0x7f
        std::string texto;
        for (size_t i = 2u; i + 1u < buffer.size(); i += 2u) {
            const uint16_t codepoint = static_cast<uint16_t>(buffer[i] | (buffer[i + 1u] << 8));
            if (codepoint == 0u) break;
            texto += (codepoint < 0x80u) ? static_cast<char>(codepoint) : '?';
        }

        if (texto.empty()) break;
        resultado.push_back(std::move(texto));
    }

    return resultado;
}

// =============================================================================
// monitoramento via usbmon
// =============================================================================

// estrutura de pacote do usbmon conforme documentacao do kernel linux
struct __attribute__((packed)) PacoteUsbmon {
    uint64_t id;
    uint8_t  tipoEvento;
    uint8_t  tipoTransferencia;
    uint8_t  numeroEndpoint;
    uint8_t  numeroDispositivo;
    uint16_t numeroBarramento;
    int8_t   flagSetup;
    int8_t   flagDados;
    int64_t  timestampSegundos;
    int32_t  timestampMicros;
    int32_t  status;
    uint32_t comprimento;
    uint32_t capturado;
    uint8_t  setup[8];
    int32_t  intervalo;
    int32_t  quadroInicio;
    uint32_t flagsTransferencia;
    uint32_t numeroDescritores;
};

static volatile bool monitoramentoAtivo = true;

static void tratarSinalInterrupcao(int) { monitoramentoAtivo = false; }

void monitorar(int barramento, uint8_t enderecoFiltro, uint32_t limitePacketes)
{
    char caminhoDispositivo[32];
    // usbmon0 captura todos os barramentos, usbmonN captura apenas o barramento N
    std::snprintf(caminhoDispositivo, sizeof(caminhoDispositivo),
                  "/dev/usbmon%d", barramento < 0 ? 0 : barramento);

    const int descritor = open(caminhoDispositivo, O_RDONLY);
    if (descritor < 0) {
        std::fprintf(stderr,
            "nao foi possivel abrir %s\n"
            "verifique se o modulo usbmon esta carregado,\n"
            "  sudo modprobe usbmon\n"
            "ou monte o debugfs,\n"
            "  sudo mount -t debugfs none /sys/kernel/debug\n",
            caminhoDispositivo);
        return;
    }

    static const char* nomesTransferencia[] = { "iso", "int", "ctrl", "bulk" };

    std::signal(SIGINT, tratarSinalInterrupcao);
    monitoramentoAtivo = true;

    std::printf("monitorando %s  (addr=%d, ctrl+c para parar)\n\n",
                caminhoDispositivo, enderecoFiltro);
    std::printf("%-18s %-4s %-5s %-4s ep   dev  status  len  dados\n",
                "timestamp", "tipo", "xfer", "dir");
    std::printf("%-18s %-4s %-5s %-4s %-4s %-4s %-7s %-4s\n",
                "------------------","----","-----","----","----","----","-------","----");

    // buffer para cabecalho mais payload de ate 64 bytes
    constexpr size_t TAMANHO_BUFFER_MON = sizeof(PacoteUsbmon) + 64u;
    uint8_t buffer[TAMANHO_BUFFER_MON];

    uint32_t totalCapturado = 0u;
    while (monitoramentoAtivo && (limitePacketes == 0u || totalCapturado < limitePacketes)) {
        const ssize_t lido = read(descritor, buffer, TAMANHO_BUFFER_MON);
        if (lido < static_cast<ssize_t>(sizeof(PacoteUsbmon))) {
            if (!monitoramentoAtivo) break;
            continue;
        }

        const auto* pacote = reinterpret_cast<const PacoteUsbmon*>(buffer);

        if (enderecoFiltro != 0u && pacote->numeroDispositivo != enderecoFiltro) continue;

        const uint8_t ep        = pacote->numeroEndpoint & 0x7fu;
        const uint8_t direcao   = (pacote->numeroEndpoint & 0x80u) ? 1u : 0u;
        const uint8_t idxXfer   = pacote->tipoTransferencia < 4u ? pacote->tipoTransferencia : 3u;
        const char    tipoEvt   = (pacote->tipoEvento == 'S') ? 'S'
                                : (pacote->tipoEvento == 'C') ? 'C' : 'E';

        std::printf("%10lld.%06d  %c    %-5s %-4s %02x   %-4d %-7d %-4u ",
                    static_cast<long long>(pacote->timestampSegundos),
                    pacote->timestampMicros,
                    tipoEvt,
                    nomesTransferencia[idxXfer],
                    direcao ? "IN " : "OUT",
                    ep,
                    pacote->numeroDispositivo,
                    pacote->status,
                    pacote->comprimento);

        // exibe os primeiros bytes do payload quando disponivel
        const uint32_t bytesVisiveis = pacote->capturado < 16u ? pacote->capturado : 16u;
        if (bytesVisiveis > 0u && pacote->flagDados == 0) {
            const uint8_t* payload = buffer + sizeof(PacoteUsbmon);
            for (uint32_t i = 0u; i < bytesVisiveis; ++i)
                std::printf("%02x ", payload[i]);

        } else if (pacote->flagSetup == 0 && tipoEvt == 'S') {
            // decodifica o setup packet com os campos em ordem correta
            std::printf("setup: %02x %02x %02x%02x %02x%02x %02x%02x",
                pacote->setup[0], pacote->setup[1],
                pacote->setup[3], pacote->setup[2],
                pacote->setup[5], pacote->setup[4],
                pacote->setup[7], pacote->setup[6]);
        }

        std::putchar('\n');
        ++totalCapturado;
    }

    close(descritor);
    std::printf("\n%u pacotes capturados\n", totalCapturado);
}

#endif // __linux__
