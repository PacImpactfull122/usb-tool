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

[[nodiscard]] bool lerSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                             std::vector<uint8_t>& buf);

// ! operacao destrutiva e irreversivel
[[nodiscard]] bool escreverSetor(intptr_t handle, uint64_t lba, uint32_t qtd,
                                  const std::vector<uint8_t>& dados);

[[nodiscard]] bool entrarModoBootloader(intptr_t handle);
