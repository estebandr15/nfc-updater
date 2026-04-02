#include <stdint.h>
#include <string.h>

#define CRC_POLY  0x04C11DB7
#define CRC_INIT  0xFFFFFFFF
#define CRC_XOROT 0x00000000

// --- Estructura de Contexto (Necesaria para acumular) ---
typedef struct {
    uint32_t current_crc;
    uint8_t  buffer[4];     // Guarda bytes "sobrantes" hasta juntar 4
    uint8_t  buffer_len;    // Cuántos bytes hay en el buffer (0..3)
} ST25_CRC_Ctx;

static uint32_t cmNext(uint32_t crc, uint8_t byteToDo);

static void ProcessBlock(ST25_CRC_Ctx *ctx, const uint8_t *block);

void ST25_CRC_Init(ST25_CRC_Ctx *ctx);

void ST25_CRC_Update(ST25_CRC_Ctx *ctx, const uint8_t *data, size_t len);

uint32_t ST25_CRC_Final(ST25_CRC_Ctx *ctx);