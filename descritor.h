#pragma once

#include <cstdint>
#include <string>
#include <vector>

// * representa um endpoint descoberto via parsing de descritor de configuracao
struct EndpointInfo {
    uint8_t  endereco;
    uint8_t  atributos;
    uint16_t maxPacote;
    uint8_t  intervalo;
    uint8_t  iface;
};

// * item decodificado do hid report descriptor
struct ItemHid {
    std::string tag;
    std::string valor;
};

// * resultado do parse completo de configuracao
struct ConfiguracaoUsb {
    uint8_t  numInterfaces;
    uint8_t  valorConfig;
    uint16_t totalBytes;
    std::vector<EndpointInfo> endpoints;
};

// parseia o blob de configuracao completa e retorna estrutura decodificada
ConfiguracaoUsb parsearConfiguracao(const std::vector<uint8_t>& blob);

// imprime configuracao completa de forma legivel
void imprimirConfiguracao(const ConfiguracaoUsb& cfg);

// parseia e imprime o device descriptor
void imprimirDescritorDispositivo(const std::vector<uint8_t>& blob);

// parseia e imprime o hid report descriptor como itens estruturados
void imprimirRelatorioHid(const std::vector<uint8_t>& blob);

// parseia string descriptor utf16le e retorna string utf8
std::string parsearStringDescriptor(const std::vector<uint8_t>& blob);

// decodifica atributos de endpoint como texto
std::string tipoTransferencia(uint8_t bmAttr);
std::string direcaoEndpoint(uint8_t addr);

// imprime dump hexadecimal com offset e ascii
void hexdump(const uint8_t* dados, size_t tam, size_t baseOffset = 0);
