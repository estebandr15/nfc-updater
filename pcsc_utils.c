#include "pcsc_utils.h"
#include <winscard.h>
#include <stdio.h>
#include <windows.h>

// APDUs para iniciar y finalizar el modo transparente
const BYTE HEADER_APDU_START_TRANSPARENT_EXCHANGE[] = {0xFF, 0xC2, 0x00, 0x00, 0x02, 0x81, 0x00};
const BYTE HEADER_APDU_END_TRANSPARENT_EXCHANGE[] = {0xFF, 0xC2, 0x00, 0x00, 0x02, 0x82, 0x00};

// APDUs para escribir y leer la configuración dinámica de control [CLS INS P1 P2 Lc 0x95 Le Datos]
// 0x95 Le , modo trasnceiver
const BYTE HEADER_APDU_CONFIGURE_FTM[] = {0xFF, 0xC2, 0x00, 0x01, 0x07, TRANSCEIVER_MODE, 0x05, 0x02, 0xA1, 0x02, REG_MB_CTRL_DYN, 0x0F};
const BYTE HEADER_APDU_ENABLE_FTM[] = {0xFF, 0xC2, 0x00, 0x01, 0x07, TRANSCEIVER_MODE, 0x05, 0x02, 0xAE, 0x02, REG_MB_CTRL_DYN, 0x01};
const BYTE HEADER_APDU_DISABLE_FTM[] = {0xFF, 0xC2, 0x00, 0x01, 0x07, TRANSCEIVER_MODE, 0x05, 0x02, 0xAE, 0x02, REG_MB_CTRL_DYN, 0x00};
const BYTE HEADER_APDU_READ_DYN_CONFIG[] = {0xFF, 0xC2, 0x00, 0x01, 0x06, TRANSCEIVER_MODE, 0x04, 0x02, 0xAD, 0x02, REG_MB_CTRL_DYN, 0x00};
const BYTE HEADER_APDU_SEND_PASSWORD[] = {0xFF, 0xC2, 0x00, 0x01, 0x11, TRANSCEIVER_MODE, 0x0F, 0x02, 0xAA, 0x02, 0x0A, 0x71, 0x09, ST25_SEND_PWD, 0x12, 0x34, 0x56, 0x78, 0x0D, 0xBD, 0xDC, 0x47};
const BYTE HEADER_APDU_READ_MAILBOX[] = {0xFF, 0xC2, 0x00, 0x01, 0x07, TRANSCEIVER_MODE, 0x05, 0x02, 0xAC, 0x02, 0x00, 0x00};

// Función para desconectar el lector y liberar el contexto
void DisconnectReader(SCARDCONTEXT hContext, LPTSTR mszReaders)
{
    LONG lResult;

    lResult = SCardFreeMemory(hContext, mszReaders);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al desconectar la tarjeta: 0x%08lX\n", lResult);
    }

    lResult = SCardReleaseContext(hContext);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al liberar el contexto: 0x%08lX\n", lResult);
    }
}

// Función para buscar lectores disponibles
LONG SearchReaders(SCARDCONTEXT hContext, LPTSTR *pmszReaders, DWORD *dwReaders)
{
    LONG lResult;

    lResult = SCardListReaders(hContext, NULL, (LPTSTR)pmszReaders, dwReaders);
    if (lResult == SCARD_E_NO_READERS_AVAILABLE)
    {
        fprintf(stderr, "Error: No se ha encontrado ningun lector NFC.\n");
        return lResult;
    }
    return lResult;
}

// Función para conectar con el lector y la tarjeta/tag
LONG ConnectReaderAndTag(SCARDCONTEXT hContext, char *mszReaders, SCARDHANDLE *hCard)
{
    LONG lResult;
    DWORD dwActiveProtocol;

    lResult = SCardConnect(hContext, mszReaders, SCARD_SHARE_SHARED,
                           SCARD_PROTOCOL_T1, hCard, &dwActiveProtocol);
    return lResult;
}

// Función para iniciar el modo transparente
int StartTransparentExchange(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_START_TRANSPARENT_EXCHANGE);

    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);

    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_START_TRANSPARENT_EXCHANGE,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al iniciar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        int uidLength = dwRecvLength;
        // printf("(Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        // printf("\n");
        printf("\nModo transparente iniciado correctamente.\n");
        return 0;
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

// Función para finalizar el modo transparente
int FinishTransparentExchange(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_END_TRANSPARENT_EXCHANGE);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_END_TRANSPARENT_EXCHANGE,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        int uidLength = dwRecvLength;
        // printf("(Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        // printf("\n");
        printf("\nModo transparente finalizado correctamente.\n");
        return 0;
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

// Función para escribir la configuración dinámica de control (Activa FTM)
int EnableFTM(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_ENABLE_FTM);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_ENABLE_FTM,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        // int uidLength = dwRecvLength;
        // printf("Write Dyn (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        if (pbRecvBuffer[dwRecvLength - 4] == 0x67 && pbRecvBuffer[dwRecvLength - 3] == 0x00)
        {
            printf("Error: Tamaño de mensaje incorrecto (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 4] == 0x64 && pbRecvBuffer[dwRecvLength - 3] == 0x01)
        {
            printf("Error en el proceso de envio (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x02 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Comando no reconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x03 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Opcion/es de comando/s no soportado/s (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x0F && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Error desconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else
        {
            printf("\n");
            printf("FTM habilitado correctamente.\n");
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

int DisableFTM(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_DISABLE_FTM);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_DISABLE_FTM,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        // int uidLength = dwRecvLength;
        // printf("Write Dyn (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        if (pbRecvBuffer[dwRecvLength - 4] == 0x67 && pbRecvBuffer[dwRecvLength - 3] == 0x00)
        {
            printf("Error: Tamaño de mensaje incorrecto (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 4] == 0x64 && pbRecvBuffer[dwRecvLength - 3] == 0x01)
        {
            printf("Error en el proceso de envio (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x02 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Comando no reconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x03 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Opcion/es de comando/s no soportado/s (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x0F && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Error desconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else
        {
            printf("\n");
            printf("FTM deshabilitado correctamente.\n");
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

// Función para leer la configuración dinámica de control
void ReadControlDynamicConfig(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_READ_DYN_CONFIG);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_READ_DYN_CONFIG,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);

    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        // int uidLength = dwRecvLength;
        // printf("Read Dyn (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        printf("\n");
        if (pbRecvBuffer[dwRecvLength - 3] & 0x01)
        {
            printf("FTM Mode is ENABLED\n\n");
        }
        else
        {
            printf("FTM Mode is DISABLED\n\n");
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
    }
}

int WriteMessageToMailbox(SCARDHANDLE hCard, BYTE *message, DWORD messageLength)
{
    LONG lResult;
    DWORD dwSendLength = messageLength;
    BYTE *pbSendBuffer = message;
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        pbSendBuffer,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);
    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error en Write Mailbox: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        int uidLength = dwRecvLength;
        // printf("Write Mailbox (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 4], pbRecvBuffer[dwRecvLength - 3]);
        printf("\n");

        if (pbRecvBuffer[dwRecvLength - 4] == 0x67 && pbRecvBuffer[dwRecvLength - 3] == 0x00)
        {
            printf("Error: Tamaño de mensaje incorrecto (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 4] == 0x64 && pbRecvBuffer[dwRecvLength - 3] == 0x01)
        {
            printf("Error en el proceso de envio (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x02 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Comando no reconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x03 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Opcion/es de comando/s no soportado/s (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x0F && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Error desconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

int ReadMessageFromMailbox(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_READ_MAILBOX);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    int busy = 1;

    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_READ_MAILBOX,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);

    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        // int uidLength = dwRecvLength;
        // printf("Read Mailbox(Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        // printf("\n");
        if (pbRecvBuffer[dwRecvLength - 4] == 0x67 && pbRecvBuffer[dwRecvLength - 3] == 0x00)
        {
            printf("Error: Tamaño de mensaje incorrecto (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 4] == 0x64 && pbRecvBuffer[dwRecvLength - 3] == 0x01)
        {
            printf("Error en el proceso de envio (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x02 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Comando no reconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x03 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Opcion/es de comando/s no soportado/s (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x0F && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Error desconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x81 && pbRecvBuffer[dwRecvLength - 4] == 0x00)
        {
            printf("Error: Error CRC (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return 81;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}

void WaitMailboxFree(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_READ_DYN_CONFIG), dwSendLength2 = sizeof(HEADER_APDU_READ_MAILBOX);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer), dwRecvLength2 = sizeof(pbRecvBuffer);
    int busy = 1;

    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_READ_DYN_CONFIG,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);

    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        int uidLength = dwRecvLength;
        // printf("WaitMailbox (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        // printf("\n");
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
    }
    while (busy)
    {
        // Comprueba si sigue estando el mensaje que se mandó por RF
        if ((pbRecvBuffer[dwRecvLength - 3] & 0x04) != 0) // || (pbRecvBuffer[dwRecvLength - 3] & 0x80) != 0)//BIT 2 MB_CTRL_Dyn
        {
            Sleep(1);
            lResult = SCardTransmit(
                hCard,
                SCARD_PCI_T1,
                HEADER_APDU_READ_DYN_CONFIG,
                dwSendLength,
                NULL,
                pbRecvBuffer,
                &dwRecvLength);
            //printf(".");
            // ReadMessageFromMailbox(hCard);
        }
        else
        {
            //printf("\rMailbox is FREE   ");
            //fflush(stdout);
            busy = 0;
        }
    }
}

int SendPassword(SCARDHANDLE hCard)
{
    LONG lResult;
    DWORD dwSendLength = sizeof(HEADER_APDU_SEND_PASSWORD);
    BYTE pbRecvBuffer[258];
    DWORD dwRecvLength = sizeof(pbRecvBuffer);
    int busy = 1;

    lResult = SCardTransmit(
        hCard,
        SCARD_PCI_T1,
        HEADER_APDU_SEND_PASSWORD,
        dwSendLength,
        NULL,
        pbRecvBuffer,
        &dwRecvLength);

    if (lResult != SCARD_S_SUCCESS)
    {
        fprintf(stderr, "Error al finalizar el modo transparente: 0x%08lX\n", lResult);
    }

    if (dwRecvLength >= 2 &&
        pbRecvBuffer[dwRecvLength - 2] == 0x90 &&
        pbRecvBuffer[dwRecvLength - 1] == 0x00)
    {
        // int uidLength = dwRecvLength;
        // printf("Send Password (Hex): ");
        // for (int i = 0; i < uidLength; i++)
        // {
        //     printf("%02X ", pbRecvBuffer[i]);
        // }

        // printf("\n0x%02X%02X \n", pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        if (pbRecvBuffer[dwRecvLength - 4] == 0x67 && pbRecvBuffer[dwRecvLength - 3] == 0x00)
        {
            printf("Error: Tamaño de mensaje incorrecto (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 4] == 0x64 && pbRecvBuffer[dwRecvLength - 3] == 0x01)
        {
            printf("Error en el proceso de envio (0x%02X).\n", pbRecvBuffer[dwRecvLength - 4]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x02 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Comando no reconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x03 && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Opcion/es de comando/s no soportado/s (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else if (pbRecvBuffer[dwRecvLength - 3] == 0x0F && pbRecvBuffer[dwRecvLength - 4] == 0x01)
        {
            printf("Error: Error desconocido (0x%02X).\n", pbRecvBuffer[dwRecvLength - 3]);
            return -1;
        }
        else
        {
            printf("Password enviada correctamente.");
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Error: El tag respondio con un codigo de estado (SW1/SW2) : 0x%02X%02X.\n",
                pbRecvBuffer[dwRecvLength - 2], pbRecvBuffer[dwRecvLength - 1]);
        return -1;
    }
}