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

struct InterfaceInfo {
    uint8_t  numero;
    uint8_t  alternativo;
    uint8_t  classe;
    uint8_t  subclasse;
    uint8_t  protocolo;
    uint8_t  iString;
    std::vector<EndpointInfo> endpoints;
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
    std::vector<InterfaceInfo>  interfaces;
    std::vector<EndpointInfo>   endpoints; // * todos os endpoints achatados para acesso rapido
};

// parseia o blob de configuracao completa e retorna estrutura decodificada
ConfiguracaoUsb parsearConfiguracao(const std::vector<uint8_t>& blob);

// imprime configuracao resumida (lista de endpoints)
void imprimirConfiguracao(const ConfiguracaoUsb& cfg);

// imprime configuracao completa com interfaces estruturadas
void imprimirConfiguracaoCompleta(const ConfiguracaoUsb& cfg);

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
