/**
 ******************************************************************************
 * @file    main.c
 * @author  [Esteban Domínguez Ramos]
 * @version V1.0.0
 * @date    09-Febrero-2026
 * @brief   Herramienta de actualización de firmware vía NFC (PC/SC) para STM32.
 *
 * @details Esta aplicación se conecta a un lector NFC mediante WinSCARD,
 * detecta un tag ST25DV y transfiere un archivo binario utilizando
 * el protocolo Fast Transfer Mode (FTM) con validación CRC.
 *
 * Dependencias:
 * - winscard.h (Windows PC/SC API)
 * - crc.c / crc.h (Cálculo CRC32 MPEG-2)
 * - file_utils.c / file_utils.h (Búsqueda de archivos .bin)
 * - pcsc_utils.c / pcsc_utils.h (Funciones de comunicación PC/SC y FTM)
 *
 * Compilación:
 * gcc -o updater.exe main.c pcsc_file.c crc.c file_utils.c -lwinscard
 *******************************************************************************
 */

#include "file_utils.h"
#include "crc.h"
#include "pcsc_utils.h"
#include <time.h>

#define MAILBOX_SIZE 256
#define CHUNK_SIZE 227
#define PAQUETES_POR_SEGMENTO 20
#define CMD_WRITE_DYN_CONFIG 0xAE
#define CMD_READ_DYN_CONFIG 0xAD
#define CMD_WRITE_MESSAGE 0xAA

#define REG_MB_CTRL_DYN 0x0D
#define ST25_SEND_PWD 0x07
#define ST25_UPDATE_FIRMWARE 0x04

const BYTE HEADER_SEND_APDU[] = {0xFF, 0xC2, 0x00, 0x01};
const BYTE TRANSCEIVER_HAEDER[] = {0x95, 0x81};
BYTE message[] = {0xFF, 0xC2, 0x00, 0x01, 0x07, 0x95, 0x05, 0x02, CMD_WRITE_MESSAGE, 0x02, 0x00, 0x80}; // Mensaje para enviar ACK
BYTE message2[] = {0xFF, 0xC2, 0x00, 0x00, 0x02, 0x84, 0x00};

SCARDCONTEXT hContext;
char *mszReaders = NULL;

// Función para imprimir errores de PC/SC
void check_error(LONG lResult, const char *message)
{
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (lResult != SCARD_S_SUCCESS)
    {
        SetConsoleTextAttribute(hStderr, FOREGROUND_RED | FOREGROUND_INTENSITY); // Rojo brillante
        fprintf(stderr, "Error en: %s\n", message);
        fprintf(stderr, "Codigo de error PC/SC (Win): 0x%08lX\n", lResult);
        SetConsoleTextAttribute(hStderr, 7); // Reset a blanco
        exit(1);
    }
}

// Función simple para unificar la pausa
void pause_ms(int ms)
{
    Sleep(ms);
}

// Función manejadora para Ctrl+C
BOOL WINAPI CtrlCHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
        if (mszReaders)
            SCardFreeMemory(hContext, mszReaders); // Liberar memoria
        if (hContext)
            SCardReleaseContext(hContext); // Liberar contexto
        printf("\nPrograma terminado por el usuario (Ctrl+C).\n");
        return FALSE; // Permite que el proceso termine
    default:
        return FALSE;
    }
}

int main()
{
    LONG lResult;
    SCARDHANDLE hCard;
    DWORD dwActiveProtocol;
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    FILE *f;                             // Puntero al fichero
    unsigned char *firmware_buffer;      // Puntero para el buffer
    long f_size;                         // Tamaño del fichero
    char *bin_file = find_bin_file();    // Buscar en el directorio actual
    unsigned char bytes_leidos = 0;
    long total_bytes = 0;
    int contador_bloques = 0;
    clock_t start_time, end_time;
    double elapsed_time;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    ST25_CRC_Ctx crc_ctx; // Contexto CRC

    // Abrir el fichero
    f = fopen(bin_file, "rb"); // Modo lectura binaria
    if (f == NULL)             // Error al abrir
    {
        perror("Error abriendo fichero"); // Mensaje de error
        return -1;
    }

    // Buscar el final del fichero
    fseek(f, 0, SEEK_END);

    // Obtener el tamaño
    f_size = ftell(f);

    // Volver al principio
    fseek(f, 0, SEEK_SET);

    total_bytes = f_size + 1;
    // Asignar la memoria exacta
    firmware_buffer = malloc(CHUNK_SIZE); // Reservar memoria
    if (firmware_buffer == NULL)          // Error al asignar memoria
    {
        fprintf(stderr, "Error al asignar memoria\n");
        fclose(f); // Cerrar fichero
        return -1;
    }

    // Establecer contexto PC/SC
    lResult = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
    check_error(lResult, "SCardEstablishContext");

    // Registrar nuestro manejador de Ctrl+C
    if (!SetConsoleCtrlHandler(CtrlCHandler, TRUE))
    {
        fprintf(stderr, "Error: No se pudo registrar el manejador de Ctrl-C.\n");
        return 1;
    }

    // Buscar lectores disponibles
    DWORD dwReaders = SCARD_AUTOALLOCATE;

    lResult = SearchReaders(hContext, &mszReaders, &dwReaders);
    check_error(lResult, "SearchReaders");

    printf("Lectores encontrados:\n%s\n", mszReaders);
    printf("Por favor, acerca un tag NFC al lector...\n");

    while (1)
    {

        // Conectar con el lector y el tag ---
        lResult = ConnectReaderAndTag(hContext, mszReaders, &hCard);

        if (lResult == SCARD_E_NO_SMARTCARD || lResult == SCARD_W_REMOVED_CARD)
        {
            // No hay tag, o se acaba de retirar. Esperamos y continuamos.
            pause_ms(200);
            continue;
        }

        if (lResult == SCARD_E_READER_UNAVAILABLE)
        {
            fprintf(stderr, "Error: Lector desconectado. Saliendo.\n");
            break;
        }

        check_error(lResult, "SCardConnect (esperando tag)");
        printf("Tag detectado!\n");

        if(StartTransparentExchange(hCard) == -1)
        {
            fprintf(stderr, "\nError iniciando modo transparente. Abortando actualizacion.\n");
            break;
        }

        if(EnableFTM(hCard) == -1)
        {
            fprintf(stderr, "\nError habilitando FTM. Abortando actualizacion.\n");
            break;
        }

        ReadControlDynamicConfig(hCard);

        WaitMailboxFree(hCard);

        if (SendPassword(hCard) == -1)
        {
            fprintf(stderr, "\n\nError enviando password. Abortando actualizacion.\n");
            break;
        }

        WaitMailboxFree(hCard);
        int result = ReadMessageFromMailbox(hCard);

        if (result == -1)
        {
            fprintf(stderr, "\n\nError leyendo mensaje del mailbox. Abortando actualizacion.\n");
            break;
        }
        else if(result != 81)
        {
            if (WriteMessageToMailbox(hCard, message, sizeof(message)) == -1)
            {
                fprintf(stderr, "\n\nError enviando ACK al mailbox. Abortando actualizacion.\n");
                break;
            }
            else
            {
                printf("\nACK enviado correctamente.\n");
            }
        }
        else
        {
            fprintf(stderr, "\n\nError de CRC detectado en el tag. Abortando actualizacion.\n");
            break;
        }


        WaitMailboxFree(hCard);

        // pause_ms(1200);

        int primera_vez = 1;

        unsigned char apdu[MAILBOX_SIZE];
        unsigned char flag_actual = 0x00;

        memset(firmware_buffer, 0, sizeof(firmware_buffer));
        memset(apdu, 0, sizeof(apdu));

        int pack = 0;
        int seg = 0;
        int cont = 0;

        printf("\nIniciando actualizacion de firmware...");
        printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

        start_time = clock(); // Iniciar temporizador
        while ((bytes_leidos = fread(firmware_buffer, 1, CHUNK_SIZE, f)) > 0)
        {
            //Codigo para mostrar el tiempo de actualización en una sola linea en la consola
            COORD pos;
            pos.X = 0;
            pos.Y = 0;
            SetConsoleCursorPosition(hConsole, pos);

            cont += bytes_leidos;
            // printf("Bytes leidos: %d, Total leidos: %d, Porcentaje: %.2f%%   \n", bytes_leidos, cont, (cont / (float)total_bytes) * 100);
            pack++;
            int ciclo = seg % 2;

            long bytes_restantes = total_bytes - cont;
            int islast = (bytes_restantes == 0);

            if (pack == 1)
            {
                ST25_CRC_Init(&crc_ctx); // Reiniciar CRC al inicio de cada segmento
                if (seg == 0)
                {
                    flag_actual = 0x15;
                }
                else
                {
                    switch (ciclo)
                    {
                    case 0:
                        flag_actual = 0x59;
                        break;
                    case 1:
                        flag_actual = 0x5B;
                        break;
                    }
                }
            }
            else if (pack < PAQUETES_POR_SEGMENTO)
            {
                switch (ciclo)
                {
                case 0:
                    flag_actual = 0x49;
                    break;
                case 1:
                    flag_actual = 0x4B;
                    break;
                }
            }
            else if (pack == PAQUETES_POR_SEGMENTO)
            {
                switch (ciclo)
                {
                case 0:
                    flag_actual = 0x69;
                    break;
                case 1:
                    flag_actual = 0x6B;
                    break;
                }
            }

            if (bytes_leidos < CHUNK_SIZE)
            {
                if (pack == 1)
                {
                    ST25_CRC_Init(&crc_ctx);
                    if (flag_actual == 0x6B)
                    {
                        flag_actual = 0x71;
                    }
                    else
                    {
                        flag_actual = 0x72;
                    }
                }
                else
                {
                    if (flag_actual == 0x49)
                    {
                        flag_actual = 0x6D;
                    }
                    else if (flag_actual == 0x4B)
                    {
                        flag_actual = 0x6F;
                    }
                }
            }

            int idx = 0;

            //  --- CONSTRUIR CABECERA APDU ---
            apdu[idx++] = HEADER_SEND_APDU[0];
            apdu[idx++] = HEADER_SEND_APDU[1];
            apdu[idx++] = HEADER_SEND_APDU[2];
            apdu[idx++] = HEADER_SEND_APDU[3];

            // Guardamos la posición de Lc para rellenarla luego
            int pos_lc = idx++;

            // --- CONSTRUIR PAYLOAD ---
            apdu[idx++] = TRANSCEIVER_HAEDER[0]; // Flags ISO
            apdu[idx++] = TRANSCEIVER_HAEDER[1]; // Flag del modo transceiver para poder mandar 128 bytes o más
            int pos_le = idx++;
            apdu[idx++] = 0x02;
            apdu[idx++] = CMD_WRITE_MESSAGE; // Cmd Write Message 0xAA
            apdu[idx++] = 0x02;
            int pos_ldata = idx++;

            // --- LÓGICA FTM  ---
            if (primera_vez)
            {

                apdu[idx++] = flag_actual;
                apdu[idx++] = (total_bytes >> 24) & 0xFF;
                apdu[idx++] = (total_bytes >> 16) & 0xFF;
                apdu[idx++] = (total_bytes >> 8) & 0xFF;
                apdu[idx++] = (total_bytes >> 0) & 0xFF;
                apdu[idx++] = ST25_UPDATE_FIRMWARE; // CMD: START UPDATE FIRMWARE
                uint8_t cmd_fw_update = ST25_UPDATE_FIRMWARE;

                ST25_CRC_Update(&crc_ctx, &cmd_fw_update, 1); // Actualizar CRC con el comando

                if (idx + bytes_leidos <= sizeof(apdu))
                {
                    memcpy(&apdu[idx], firmware_buffer, bytes_leidos);
                    ST25_CRC_Update(&crc_ctx, firmware_buffer, bytes_leidos); // Actualizar CRC con los datos del firmware
                }
                else
                {
                    printf("Desbordamiento de buffer detectado. DATOS: %d\n", idx + bytes_leidos);
                    exit(-1);
                }

                idx += bytes_leidos;

                apdu[pos_lc] = (unsigned char)(idx - (pos_lc + 1));
                apdu[pos_le] = (unsigned char)(idx - (pos_le + 1));
                apdu[pos_ldata] = (unsigned char)(idx - (pos_ldata + 2));

                // printf("apdu[pos_lc]=%d, apdu[pos_le]=%d, apdu[pos_ldata]=%d\n", apdu[pos_lc], apdu[pos_le], apdu[pos_ldata]);

                primera_vez = 0;
            }
            else
            {
                apdu[idx++] = flag_actual;
                if (bytes_leidos == CHUNK_SIZE)
                {
                    if (pack == PAQUETES_POR_SEGMENTO)
                    {
                        apdu[idx++] = bytes_leidos + 4;
                        if (idx + bytes_leidos <= sizeof(apdu))
                        {
                            memcpy(&apdu[idx], firmware_buffer, bytes_leidos);
                            ST25_CRC_Update(&crc_ctx, firmware_buffer, bytes_leidos); // Actualizar CRC con los datos del firmware
                        }
                        else
                        {
                            printf("Desbordamiento de buffer detectado. DATOS: %d\n", idx + bytes_leidos);
                            exit(-1);
                        }

                        idx += bytes_leidos;
                        uint32_t crc_final = ST25_CRC_Final(&crc_ctx); // Obtener CRC final
                        apdu[idx++] = (crc_final >> 24) & 0xFF;
                        apdu[idx++] = (crc_final >> 16) & 0xFF;
                        apdu[idx++] = (crc_final >> 8) & 0xFF;
                        apdu[idx++] = (crc_final >> 0) & 0xFF;

                        apdu[pos_lc] = (unsigned char)(idx - (pos_lc + 1));
                        apdu[pos_le] = (unsigned char)(idx - (pos_le + 1));
                        apdu[pos_ldata] = (unsigned char)(idx - (pos_ldata + 2));
                    }
                    else
                    {
                        if (feof(f)) // Si es el último bloque del archivo, añadimos el CRC aunque no se hayan leído 227 bytes
                        {
                            apdu[idx++] = bytes_leidos + 4;
                            if (idx + bytes_leidos <= sizeof(apdu))
                            {
                                memcpy(&apdu[idx], firmware_buffer, bytes_leidos);
                                ST25_CRC_Update(&crc_ctx, firmware_buffer, bytes_leidos); // Actualizar CRC con los datos del firmware
                            }
                            else
                            {
                                printf("Desbordamiento de buffer detectado. DATOS: %d\n", idx + bytes_leidos);
                                exit(-1);
                            }
                            idx += bytes_leidos;

                            uint32_t crc_final = ST25_CRC_Final(&crc_ctx); // Obtener CRC final
                            apdu[idx++] = (crc_final >> 24) & 0xFF;
                            apdu[idx++] = (crc_final >> 16) & 0xFF;
                            apdu[idx++] = (crc_final >> 8) & 0xFF;
                            apdu[idx++] = (crc_final >> 0) & 0xFF;

                            apdu[pos_lc] = (unsigned char)(idx - (pos_lc + 1));
                            apdu[pos_le] = (unsigned char)(idx - (pos_le + 1));
                            apdu[pos_ldata] = (unsigned char)(idx - (pos_ldata + 2));
                        }
                        else // Si no es el último bloque, no añadimos el CRC (aunque se hayan leído 227 bytes)
                        {
                            apdu[idx++] = bytes_leidos;
                            if (idx + bytes_leidos <= sizeof(apdu))
                            {
                                memcpy(&apdu[idx], firmware_buffer, bytes_leidos);
                                ST25_CRC_Update(&crc_ctx, firmware_buffer, bytes_leidos); // Actualizar CRC con los datos del firmware
                            }
                            else
                            {
                                printf("Desbordamiento de buffer detectado. DATOS: %d\n", idx + bytes_leidos);
                                exit(-1);
                            }
                            idx += bytes_leidos;

                            apdu[pos_lc] = (unsigned char)(idx - (pos_lc + 1));
                            apdu[pos_le] = (unsigned char)(idx - (pos_le + 1));
                            apdu[pos_ldata] = (unsigned char)(idx - (pos_ldata + 2));
                        }
                    }
                }
                else // Si se han leído menos de 227 bytes y es el último paquete, añadimos el CRC
                {
                    apdu[idx++] = bytes_leidos + 4;

                    if (idx + bytes_leidos < sizeof(apdu))
                    {
                        memcpy(&apdu[idx], firmware_buffer, bytes_leidos);
                        ST25_CRC_Update(&crc_ctx, firmware_buffer, bytes_leidos); // Actualizar CRC con los datos del firmware
                    }
                    else
                    {
                        printf("Desbordamiento de buffer detectado. DATOS: %d\n", idx + bytes_leidos);
                        exit(-1);
                    }

                    idx += bytes_leidos;

                    uint32_t crc_final = ST25_CRC_Final(&crc_ctx); // Obtener CRC final
                    apdu[idx++] = (crc_final >> 24) & 0xFF;
                    apdu[idx++] = (crc_final >> 16) & 0xFF;
                    apdu[idx++] = (crc_final >> 8) & 0xFF;
                    apdu[idx++] = (crc_final >> 0) & 0xFF;

                    apdu[pos_lc] = (unsigned char)(idx - (pos_lc + 1));
                    apdu[pos_le] = (unsigned char)(idx - (pos_le + 1));
                    apdu[pos_ldata] = (unsigned char)(idx - (pos_ldata + 2));
                }
            }

            // --- ENVIAR ---
            // printf("Enviando %ld bytes, Paquete: %d, Segmento: %d  \n", bytes_leidos, pack, seg);
            WaitMailboxFree(hCard);

            if (WriteMessageToMailbox(hCard, apdu, idx) == -1)
            {
                fprintf(stderr, "Error enviando paquete al mailbox. Abortando actualizacion.\n");
                break;
            }
            // else
            // {
            //     printf("Paquete enviado correctamente.  \n");
            // }

            memset(firmware_buffer, 0, sizeof(firmware_buffer));
            memset(apdu, 0, sizeof(apdu));

            if ((flag_actual & 0x20) == 0x20 || (flag_actual & 0x0C) == 0x0C) // Último paquete del segmento
            {
                pack = 0; // Reiniciamos cuenta
                seg++;    // Pasamos al siguiente segmento
                // pause_ms(1);
                WaitMailboxFree(hCard);
                result = ReadMessageFromMailbox(hCard);
                if (result == -1)
                {
                    fprintf(stderr, "\n\nError leyendo mensaje del mailbox. Abortando actualizacion.\n");
                    break;
                }
                else if(result == 81)
                {
                    fprintf(stderr, "\n\nError de CRC detectado. Abortando actualizacion.\n");
                    break;
                }
            }

            end_time = clock(); // Finalizar temporizador
            elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
            printf("Tiempo total de actualizacion: %.3f segundos\n", elapsed_time);
        }
        // WaitMailboxFree(hCard);
        pause_ms(1);
        if (ReadMessageFromMailbox(hCard) == -1)
        {
            fprintf(stderr, "\n\nError leyendo mensaje del mailbox. Abortando actualizacion.\n");
            break;
        }
        // Enviar ACK para indicar al microcontrolador que ha finalizado la transmisión y salte al nuevo firmware.
        if (WriteMessageToMailbox(hCard, message, sizeof(message)) == -1)
        {
            fprintf(stderr, "\n\nError enviando ACK al mailbox. Abortando actualizacion.\n");
        }
        else
        {
            printf("ACK enviado correctamente.\n\n");
        }
        WaitMailboxFree(hCard);
        if (DisableFTM(hCard) == -1)
        {
            fprintf(stderr, "\n\nError deshabilitando FTM. Abortando actualizacion.\n");
            break;
        }
        ReadControlDynamicConfig(hCard);

        free(firmware_buffer); // Liberar memoria
        fclose(f);             // Cerrar fichero

        if(FinishTransparentExchange(hCard) == -1)
        {
            fprintf(stderr, "\nError finalizando modo transparente.\n");
            break;
        }

        lResult = SCardDisconnect(hCard, SCARD_LEAVE_CARD);
        check_error(lResult, "SCardDisconnect");

        // Esperar a que el tag sea retirado
        printf("Por favor, retira el tag del lector...\n");

        lResult = SCARD_S_SUCCESS; // Asumimos que el tag sigue ahí

        // Bucle mientras sigamos conectando con éxito
        while (lResult == SCARD_S_SUCCESS)
        {
            // Intentamos conectar de nuevo.
            lResult = SCardConnect(hContext, mszReaders, SCARD_SHARE_SHARED,
                                   SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol);

            if (lResult == SCARD_S_SUCCESS)
            {
                // El tag sigue ahí. Nos desconectamos y esperamos.
                SCardDisconnect(hCard, SCARD_LEAVE_CARD);
                pause_ms(200);
            }
            // Si lResult es SCARD_E_NO_SMARTCARD o SCARD_W_REMOVED_CARD,
            // el bucle 'while' terminará.
        }

        // Si el error es NO_SMARTCARD (0x0C) o REMOVED_CARD (0x69), es normal.
        // Si es cualquier otro error, es un problema.
        if (lResult != SCARD_E_NO_SMARTCARD && lResult != SCARD_W_REMOVED_CARD)
        {
            // Un error real que no es "tag retirado"
            check_error(lResult, "SCardConnect (esperando retirada)");
        }

        printf("Tag retirado. Esperando el proximo tag...\n\n");
    }

    DisconnectReader(hContext, mszReaders); // Liberar memoria y contexto PC/SC

    return 0;
}
