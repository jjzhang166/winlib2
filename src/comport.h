/*0===========================================================================
**
** File:       comport.hpp
**
** Purpose:    Windows serial COM port class definitions.
**
**
**
**
** Date:       Dec. 10, 2007
**
** Author:     Alan D. Graves
**
**============================================================================
*/
#if !defined COMPORT_H
#define COMPORT_H
#include "active.h"
#include "circbuf.hpp"              /* Circular Buffer class i/f */

class ComPort : public ActiveObject
{
public:
   ComPort(int num,DWORD baud=CBR_9600,BYTE parity=NOPARITY,BYTE stop=ONESTOPBIT);
   ~ComPort();

   bool Start();
   bool Stop();
   int  Read( LPSTR lpszBlock, int nMaxLength );
   bool Write( LPSTR lpByte , DWORD dwBytesToWrite);
   int  Write( BYTE c );

   virtual void Process(void);

   int  TxWrite( int len, char *ptr );
   int  TxPutch( int c );
   int  TxGetch( void );
   long TxCount( void );
   int  RxWrite( int len, char *ptr );
   int  RxGetch( void );
   long RxCount( void );

   bool IsConnected(void) { return fConnected; }

private:
   void InitThread() {}
   void Run();
   void FlushThread();

   Mutex _mutex;
   Event _event;

   circbuf ctxbuf;
   circbuf crxbuf;
   char     szPort[ 15 ];
#ifdef _CONSOLE
   WCHAR	swPort[ 15 ];
#else
   char		swPort[15];
#endif
   bool     fConnected;
   HANDLE   id;
   BYTE     bPort;
   bool     fXonXoff;
   bool     fLocalEcho;
   bool     fNewLine; 
   bool     fAutoWrap;
   bool     fDisplayErrors;
   BYTE     bByteSize;
   BYTE     bParity;
   BYTE     bStopBits;
   DWORD    dwBaudRate;
   OVERLAPPED  osWrite, osRead;
};

#endif
