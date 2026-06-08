#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DispositivoUsb {
    uint16_t    idVendor;
    uint16_t    idProduct;
    std::string fabricante;
    std::string produto;
    std::string serial;
    std::string caminho;
    uint8_t     barramento;
    uint8_t     endereco;
    uint8_t     classe;
    uint8_t     subclasse;
    uint8_t     protocolo;
    std::string velocidade;
    uint8_t     versaoUsb;
};

[[nodiscard]] std::vector<DispositivoUsb> enumerarDispositivos();

[[nodiscard]] intptr_t abrirDispositivo(const DispositivoUsb& dev);
void fecharDispositivo(intptr_t handle);

[[nodiscard]] bool resetarDispositivo(intptr_t handle);

[[nodiscard]] std::vector<uint8_t> lerDescritor(intptr_t handle, uint8_t tipo, uint8_t indice);

[[nodiscard]] bool transferirControle(intptr_t handle,
                                      uint8_t bmRequestType, uint8_t bRequest,
                                      uint16_t wValue, uint16_t wIndex,
                                      std::vector<uint8_t>& dados, bool enviar);

[[nodiscard]] bool transferirBulk(intptr_t handle, uint8_t endpoint,
                                   std::vector<uint8_t>& dados, bool enviar);

// * leitura de endpoint interrupt, usado para hid e outros dispositivos de polling
[[nodiscard]] bool transferirInterrupt(intptr_t handle, uint8_t endpoint,
                                       std::vector<uint8_t>& dados, uint32_t timeout);

[[nodiscard]] bool lerSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                             std::vector<uint8_t>& buf);

// ! operacao destrutiva e irreversivel
[[nodiscard]] bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                                  const std::vector<uint8_t>& dados);

[[nodiscard]] bool entrarModoBootloader(intptr_t handle);

// * reivindicar e liberar interface para acesso exclusivo
[[nodiscard]] bool reivindicarInterface(intptr_t handle, uint32_t iface);
void liberarInterface(intptr_t handle, uint32_t iface);

// * desconectar driver do kernel antes de reivindicar interface
bool desconectarDriver(intptr_t handle, uint32_t iface);
