#ifndef PSCS_UTILS_H
#define PSCS_UTILS_H

#include <winscard.h> // PC/SC API
#include <windows.h> // Para Sleep()
#include <stdio.h>

#define REG_MB_CTRL_DYN 0x0D
#define REG_EH_CTRL_DYN 0x02
#define ST25_SEND_PWD 0x07
#define ST25_UPDATE_FIRMWARE 0x04
#define TRANSCEIVER_MODE 0x95

LONG SearchReaders(SCARDCONTEXT hContext, LPTSTR *pmszReaders, DWORD *dwReaders);

LONG ConnectReaderAndTag(SCARDCONTEXT hContext, char *mszReaders, SCARDHANDLE *hCard);

int SendPassword(SCARDHANDLE hCard);

void DisconnectReader(SCARDCONTEXT hContext, LPTSTR mszReaders);

int StartTransparentExchange(SCARDHANDLE hCard);

int FinishTransparentExchange(SCARDHANDLE hCard);

int EnableFTM(SCARDHANDLE hCard);

int DisableFTM(SCARDHANDLE hCard);

void ReadControlDynamicConfig(SCARDHANDLE hCard);

int WriteMessageToMailbox(SCARDHANDLE hCard, BYTE *message, DWORD messageLength);

int ReadMessageFromMailbox(SCARDHANDLE hCard);

void WaitMailboxFree(SCARDHANDLE hCard);

#endif // PSCS_UTILS_H