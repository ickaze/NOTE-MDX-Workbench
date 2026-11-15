// Minimal serial test double; used only by ndsif-transport-tests.
#pragma once
#include "ndsif_wire.h"
#include <cstring>
using DWORD=uint32_t; using HANDLE=int;
constexpr int INVALID_HANDLE_VALUE=-1,GENERIC_READ=1,GENERIC_WRITE=2,OPEN_EXISTING=3,NOPARITY=0,ONESTOPBIT=0,DTR_CONTROL_ENABLE=1,RTS_CONTROL_DISABLE=0,PURGE_RXCLEAR=1,PURGE_TXCLEAR=2;
constexpr DWORD MAXDWORD=UINT32_MAX;
struct DCB {int DCBlength,BaudRate,ByteSize,Parity,StopBits,fBinary,fParity,fOutX,fInX,fOutxCtsFlow,fOutxDsrFlow,fDsrSensitivity,fDtrControl,fRtsControl;};
struct COMMTIMEOUTS {DWORD a,b,c,d,e;};
inline HANDLE CreateFileW(const wchar_t*,int,int,void*,int,int,void*){return 1;}
inline int GetCommState(int,DCB*){return 1;} inline int SetCommState(int,DCB*){return 1;}
inline int SetCommTimeouts(int,COMMTIMEOUTS*){return 1;} inline void CloseHandle(int){}
inline void PurgeComm(int,int){} inline void Sleep(int){}
int WriteFile(int,const void*,DWORD,DWORD*,void*);
int ReadFile(int,void*,DWORD,DWORD*,void*);

