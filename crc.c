#include "crc.h"

// Función auxiliar interna para calcular el siguiente CRC con 1 byte 
static uint32_t cmNext(uint32_t crc, uint8_t byteToDo) {
    // XOR del byte actual con el byte más alto del CRC
    crc ^= ((uint32_t)byteToDo << 24);
    // Procesar cada bit del byte
    for (int i = 0; i < 8; i++) {
        if (crc & 0x80000000) {
            crc = (crc << 1) ^ CRC_POLY;
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

// Función auxiliar interna para procesar 1 bloque de 4 bytes
static void ProcessBlock(ST25_CRC_Ctx *ctx, const uint8_t *block) {
    uint32_t wordToDo = 0;
    
    // Construcción de wordToDo
    for (int k = 0; k < 4; ++k) {
        wordToDo |= ((uint32_t)block[k] << (8 * k));
    }

    // Proceso de bytes (Extrae byte alto y desplaza)
    for (int i = 0; i < 4; ++i) {
        uint8_t byteToDo;
        byteToDo = (uint8_t)((wordToDo & 0xFF000000) >> 24);
        wordToDo <<= 8;
        ctx->current_crc = cmNext(ctx->current_crc, byteToDo);
    }
}

// ==========================================
//    NUEVAS FUNCIONES DE ACUMULACIÓN
// ==========================================

// 1. INICIALIZAR
void ST25_CRC_Init(ST25_CRC_Ctx *ctx) {
    ctx->current_crc = CRC_INIT;
    ctx->buffer_len = 0;
    memset(ctx->buffer, 0, 4);
}

// 2. ACTUALIZAR (Acumula datos)
void ST25_CRC_Update(ST25_CRC_Ctx *ctx, const uint8_t *data, size_t len) {
    size_t i = 0;

    // Si hay datos pendientes en el buffer, intentamos llenarlo
    if (ctx->buffer_len > 0) {
        while (i < len && ctx->buffer_len < 4) {
            ctx->buffer[ctx->buffer_len++] = data[i++];
        }
        
        // Si completamos los 4 bytes, procesamos ese bloque
        if (ctx->buffer_len == 4) {
            ProcessBlock(ctx, ctx->buffer);
            ctx->buffer_len = 0; // Buffer vacío
        }
    }

    // Procesar bloques completos que vienen en 'data'
    while ((len - i) >= 4) {
        ProcessBlock(ctx, &data[i]);
        i += 4;
    }

    // Guardar los bytes sobrantes (1, 2 o 3) para la próxima llamada
    while (i < len) {
        ctx->buffer[ctx->buffer_len++] = data[i++];
    }
}

// 3. FINALIZAR (Aplica Padding y Formato Salida)
uint32_t ST25_CRC_Final(ST25_CRC_Ctx *ctx) {
    
    // Aplicar Padding si quedaron bytes sueltos
    if (ctx->buffer_len > 0) {
        while (ctx->buffer_len < 4) {
            ctx->buffer[ctx->buffer_len++] = 0; // Relleno con ceros
        }
        ProcessBlock(ctx, ctx->buffer);
    }

    // XOR Final
    uint32_t crcValue = ctx->current_crc ^ CRC_XOROT;

    // Formatear salida como byte array Big Endian
    // uint8_t crcBytes[4];
    // crcBytes[0] = (uint8_t)((crcValue >> 24) & 0xFF);
    // crcBytes[1] = (uint8_t)((crcValue >> 16) & 0xFF);
    // crcBytes[2] = (uint8_t)((crcValue >> 8)  & 0xFF);
    // crcBytes[3] = (uint8_t)((crcValue >> 0)  & 0xFF);

    // return (uint32_t)(crcBytes[0] << 24 | crcBytes[1] << 16 | crcBytes[2] << 8 | crcBytes[3]);
    return crcValue;
}