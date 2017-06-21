// winlib.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "windows.h"
#include "src\LOG.H"		// Capture putchar & printf output to a log file
#include "src\TMR.H"
#include "src\comport.h"

#define RUN_TESTING
//#define COM_TESTING
//#define TMR_TESTING

ComPort com(3, CBR_115200);

int rxpacket_len = 0;
int rxpacket_cnt = 0;
byte rxpacket[80];

extern void MEM_Dump(unsigned char *data, int len, long base);

/*
** ComPort virtual thread processing function.
*/
void ComPort::Process(void)
{
	/* Rx serial processing - rx data block received is put into circular buffer. */
	int n = Read((LPSTR)rxpacket, sizeof(rxpacket) - 1);
	if (n > 0)
	{
		RxWrite(n, (char *)rxpacket);
	}

	/* Tx serial processing - tx data is sent immediately by app, so nothing to do. */

}

/*
** Functions required by slip.cpp module to interact with the hardware.
*/
int recv_char(void)
{
	return com.RxGetch();
}

int send_char(byte c)
{
	com.Write(c);
	return 0;
}

#ifdef RUN_TESTING
/*
** A simple SCRIPTing language based thread for processing serial output and input data.
*/
#include "src\BASEFILE.HPP"
#include "src\SCRIPT.HPP"
#include "src\TMR.H"

/* TxScript Rx initial timeout maximum value. */
#define TMR_TIMEOUT_MAX	32000

/* Used in defining the Read block size. */
#define BUFSIZE			128

/* TxScript return result codes. */
#define TXSCRIPT_PROCESSING       1
#define TXSCRIPT_SUCCESS          0
#define TXSCRIPT_EXIT			 -1
#define TXSCRIPT_FILE_NOT_FOUND  -2
#define TXSCRIPT_ERROR           -3
#define TXSCRIPT_TIMEOUT         -4
#define TXSCRIPT_LABEL_NOT_FOUND -5
#define TXSCRIPT_VAR_NOT_FOUND   -6
#define TXSCRIPT_VAL_UNDEFINED   -7
#define TXSCRIPT_ABORTED         -8
#define TXSCRIPT_OTHER           -9

class TxScript : public ActiveObject
{
public:
	SCRIPT *pScript;
	ComPort *com;
	int Result;
	Event e;

	TxScript(ComPort *comptr)
		: com(comptr)
	{ }

	char *getname() { return "TxScript"; }

	int RunScript(SCRIPT *script)
	{
		pScript = script;
		Result = TXSCRIPT_PROCESSING;
		/* Run thread. */
		Resume();
		/* Wait for result from script processing. */
		return WaitResult();
	}

	int GetResult() { return Result; }
	int WaitResult() { e.Wait(); return GetResult(); }

private:
	void InitThread() { }
	void Run();
	void FlushThread() { e.Release(); }

};

void TxScript::Run()
{
	SCRIPT_REC rec;
	SCRIPT_RECDATA *pRecData;
	BOOL bSuccess = true;

	/* Define a processing timer. */
	Tmr tmr = TMR_New();
	int Timeout = TMR_TIMEOUT_MAX;
	TMR_Start(tmr, Timeout);

	TRACE("%s: Running...\n", getname());
	pScript->SetRecord(0);

	while (bSuccess && pScript->GetNextRec(&rec))
	{
		if (log)
		{
			log->Flush();
		}

		if (_isDying)
		{
			printf("%s: Aborted!\n", getname());
			Result = TXSCRIPT_ABORTED;
			break;
		}

		printf("> %03d: ", rec.iNum);
		switch (rec.iType)
		{
		case SCRIPT_TXCHAR:
		{
			int c;
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("tx %d ", rec.iLen, pRecData->cArray);
			for (int i = 0; i < rec.iLen; i++)
			{
				c = pRecData->cArray[i] & 0xFF;
				if (isprint(c))
				{
					putchar(c);
				}
				else
				{
					printf("\\x%02X", c);
				}
			}
			putchar('\n');
			com->Write(pRecData->cArray, rec.iLen);
		}
		break;

		case SCRIPT_TXVAR:
		{
			int c;
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("txvar %d ", rec.iNext);
			int n = rec.iLen / sizeof(int);
			int j;
			j = pScript->FindTypeRecord(SCRIPT_VAR, rec.iNext);
			if (j >= 0)
			{
				if (n == 0)
				{
					// Default val if not explicitly specified.
					n = 1;
				}
				int m = pScript->GetRec(j)->iLen;
				if (m == 0)
				{
					// Default val if not explicitly specified.
					m = sizeof(int);
				}
				m /= sizeof(int);
				for (int k = 0; k < n; k++)
				{
					int i = pRecData->iArray[k];
					if (i >= m)
					{
						if (k != 0)
						{
							putchar('\n');
						}
						printf("\n%s: [%d] Val_Undefined!\n", getname(), k);
						Result = TXSCRIPT_VAL_UNDEFINED;
						bSuccess = false;
						break;
					}
					int val;
					pScript->GetVar(j, &val, i);
					//ToDo: ADG! Only handles a char value!
					printf("\\x%02X", val & 0xFF);
					char c = val & 0xFF;
					com->Write(&c, 1);
				}
				putchar('\n');
			}
			else
			{
				printf("\n%s: [%d] Var_Not_Found!\n", getname(), rec.iNext);
				Result = TXSCRIPT_VAR_NOT_FOUND;
				bSuccess = false;
				break;
			}
		}
		break;

		case SCRIPT_TXFILE:
		{
			BASEFILE bf;
			char *filename;
			long size;
			BYTE buf[BUFSIZE];
			int buflen;
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			filename = pRecData->cArray;	/* the image filename */
			bool bDone = false;
			printf("txfile %s ", filename);
			/* Determine the size of the image file. */
			bf.InitReadFile(filename);
			if (!bf.IsFile())
			{
				printf("\n%s: File_Not_Found!\n", getname());
				bSuccess = false;
				Result = TXSCRIPT_FILE_NOT_FOUND;
				break;
			}
			size = bf.Filesize();
			printf("size=%08x bytes\n", size);
			while (size)
			{
				if (size > BUFSIZE)
				{
					bf.ReadfromFile(BUFSIZE, buf);
					com->Write((char *)&buf[0], BUFSIZE);
					size -= BUFSIZE;
				}
				else
				{
					bf.ReadfromFile(size, buf);
					com->Write((char *)&buf[0], size);
					break;
				}
				if (_isDying)
				{
					printf("%s: Aborted!\n", getname());
					Result = TXSCRIPT_ABORTED;
					bSuccess = false;
					break;
				}
			}
		}
		break;

		case SCRIPT_RXCHAR:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);

			TMR_Stop(tmr);
			if (Timeout > 0)
			{
				TMR_Start(tmr, Timeout);
			}
			int c;
			int Cnt = 0;
			int MaxCnt = rec.iLen;
			bool bFound = false;
			printf("rx %d ", MaxCnt);

			while (!bFound)
			{
				if (_isDying)
				{
					printf("\n%s: Aborted!\n", getname());
					Result = TXSCRIPT_ABORTED;
					bSuccess = false;
					break;
				}
				if (TMR_IsTimeout(tmr))
				{
					// if label value not set
					if (rec.iNext == -1)
					{
						// then do Timeout error.
						printf("\n%s: Timeout!\n", getname());
						Result = TXSCRIPT_TIMEOUT;
						bSuccess = false;
						break;
					}
					// else goto label record specified.
					int i = pScript->FindTypeRecord(SCRIPT_LABEL, rec.iNext);
					if (i >= 0)
					{
						TRACE("\tgoto record %d\n", i);
						pScript->SetRecord(i);
					}
					else
					{
						printf("\n%s: [%d] Label_Not_Found!\n", getname(), rec.iNext);
						Result = TXSCRIPT_LABEL_NOT_FOUND;
						bSuccess = false;
						break;
					}
					bFound = true;
					break;
				}
				while ((c = recv_char()) != -1)
				{
					if (c == (pRecData->cArray[Cnt] & 0xFF))
					{
						if (isprint(c))
							putchar(c);
						else
							printf("\\x%02X", c);
						Cnt++;
					}
					else
					{
						putchar('[');
						if (isprint(c))
							putchar(c);
						else
							printf("%02X", c);
						putchar(']');
						while (Cnt > 0)
						{
							Cnt--;
						}
					}
					if (Cnt >= MaxCnt)
					{
						putchar('\n');
						bFound = true;
						break;
					}
				}
				Sleep(5);
			}
		}
		break;

		case SCRIPT_SKIP:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);

			TMR_Stop(tmr);
			if (Timeout > 0)
			{
				TMR_Start(tmr, Timeout);
			}
			int c;
			int Cnt = 0;
			int MaxCnt = pRecData->iArray[0];
			BOOL bFound = false;
			printf("skip %d ", MaxCnt);
			int j;
			int var = -1;
			int idx = 0;
			if (rec.iLen > 4)
			{
				var = pRecData->iArray[1];
				idx = var >> 16;
				var &= 0xFFFF;
			}
			if (var != -1)
			{
				j = pScript->FindTypeRecord(SCRIPT_VAR, var);
				if (j >= 0)
				{
					TRACE("var %d@%d:", var, idx);
				}
			}
			while (bSuccess && !bFound)
			{
				if (_isDying)
				{
					printf("\n%s: Aborted!\n", getname());
					Result = TXSCRIPT_ABORTED;
					bSuccess = false;
					break;
				}
				if (TMR_IsTimeout(tmr))
				{
					// if label value not set
					if (rec.iNext == -1)
					{
						// then do Timeout error.
						printf("\n%s: Timeout!\n", getname());
						Result = TXSCRIPT_TIMEOUT;
						bSuccess = false;
						break;
					}
					// else goto label record specified.
					int i = pScript->FindTypeRecord(SCRIPT_LABEL, rec.iNext);
					if (i >= 0)
					{
						TRACE("\tgoto record %d\n", i);
						pScript->SetRecord(i);
					}
					else
					{
						printf("\n%s: [%d] Label_Not_Found!\n", getname(), rec.iNext);
						Result = TXSCRIPT_LABEL_NOT_FOUND;
						bSuccess = false;
						break;
					}
					bFound = true;
					break;
				}
				while ((c = recv_char()) != -1)
				{
					if (isprint(c))
						putchar(c);
					else
						printf("\\x%02X", c);
					if (var != -1)
					{
						// Save char to variable array.
						pScript->SetVar(j, c, idx + Cnt);
					}
					Cnt++;
					if (Cnt >= MaxCnt)
					{
						bFound = true;
						break;
					}
				}
				Sleep(5);
			}
			putchar('\n');
		}
		break;

		case SCRIPT_DELAY:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("delay %d", pRecData->iValue);
			int i;
			for (i = 0; i < pRecData->iValue; i++)
			{
				if (_isDying)
				{
					printf("\n%s: Aborted!\n", getname());
					Result = TXSCRIPT_ABORTED;
					break;
				}
				Sleep(100L);
				putchar('.');
			}
			putchar('\n');
		}
		break;

		case SCRIPT_TIMEOUT:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			Timeout = pRecData->iValue;
			printf("timeout %d\n", Timeout);
			TMR_Stop(tmr);
			if (Timeout > 0)
			{
				TMR_Start(tmr, Timeout);
			}
		}
		break;

		case SCRIPT_EXIT:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			if (rec.iLen == 0)
			{
				Result = TXSCRIPT_EXIT;
			}
			else
			{
				Result = pRecData->iValue;
			}
			printf("exit %d\n", Result);
			pScript->SetRecord(pScript->GetRecCount());
		}
		break;

		case SCRIPT_LABEL:
		{
			printf("label %d\n", rec.iNext);
		}
		break;

		case SCRIPT_VAR:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("var %d:", rec.iNext);
			int i;
			int n = rec.iLen / sizeof(int);
			for (i = 0; i < n; i++)
			{
				printf("%d", pRecData->iArray[i]);
				putchar(((i + 1) < n) ? ',' : ' ');
			}
			putchar('\n');
		}
		break;

		case SCRIPT_GOTO:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("goto %d,", rec.iNext);
			int i;
			int n = rec.iLen / sizeof(int);
			for (i = 0; i < n; i++)
			{
				if (i > 0)
				{
					printf("%d", pRecData->iArray[i]);
				}
				else
				{
					printf("%d@%d", pRecData->iArray[i] & 0xFFFF, pRecData->iArray[i] >> 16);
				}
				putchar(((i + 1) < n) ? ',' : ' ');
			}

			// goto label record specified if variable meets condition
			i = pScript->FindTypeRecord(SCRIPT_LABEL, rec.iNext);
			if (i >= 0)
			{
				if (rec.iLen >= 4)
				{
					int var = -1;
					int val = 0;
					int inc = 0;
					int j;
					int idx;
					var = pRecData->iArray[0];
					idx = var >> 16;
					var &= 0xFFFF;
					j = pScript->FindTypeRecord(SCRIPT_VAR, var);
					if (j >= 0)
					{
						if (rec.iLen >= 8)
						{
							val = pRecData->iArray[1];
							if (rec.iLen >= 12)
							{
								inc = pRecData->iArray[2];
							}
						}
						TRACE("\t%d@%d=", var, idx);
						pScript->GetVar(j, &var, idx);
						TRACE("(%d", var);
						if (var >= val)
						{
							TRACE(">=%d) continue\n", val);
						}
						else
						{
							TRACE("< %d) inc=%d, new val=%d,  goto record %d\n", val, inc, var + inc, i);
							pScript->SetVar(j, var + inc, idx);
							pScript->SetRecord(i);		// Goto to record
						}
					}
					else
					{
						printf("%s: [%d] Var_Not_Found!\n", getname(), var);
						Result = TXSCRIPT_VAR_NOT_FOUND;
						bSuccess = false;
						break;
					}
				}
				else
				{
					TRACE("\tgoto record %d\n", i);
					pScript->SetRecord(i);		// Goto to record
				}
			}
			else
			{
				TRACE("\n%s: [%d] Label_Not_Found!\n", getname(), rec.iNext);
				Result = TXSCRIPT_LABEL_NOT_FOUND;
				bSuccess = false;
				break;
			}
			break;
		}
		case SCRIPT_TEST:
		{
			pRecData = (SCRIPT_RECDATA*)(rec.pData);
			printf("test %d,", rec.iNext);
			int i;
			int n = rec.iLen / sizeof(int);
			for (i = 0; i < n; i++)
			{
				if (i > 0)
				{
					printf("%d", pRecData->iArray[i]);
				}
				else
				{
					printf("%d@%d", pRecData->iArray[i]&0xFFFF, pRecData->iArray[i]>>16);
				}
				putchar(((i + 1) < n) ? ',' : ' ');
			}

			// test and skip over records if variable meets condition
			int var = -1;
			int val = 0;
			int inc = 0;
			int j;
			if (rec.iLen >= 4)
			{
				int idx;
				var = pRecData->iArray[0];
				idx = var >> 16;
				var &= 0xFFFF;
				j = pScript->FindTypeRecord(SCRIPT_VAR, var);
				if (j >= 0)
				{
					if (rec.iLen >= 8)
					{
						val = pRecData->iArray[1];
						if (rec.iLen >= 12)
						{
							inc = pRecData->iArray[2];
						}
					}
					TRACE("\t%d@%d=", var, idx);
					pScript->GetVar(j, &var, idx);
					TRACE("(%d", var);
					if (var == val)
					{
						TRACE("==%d) continue\n", val);
					}
					else
					{
						TRACE("!=%d), ", val);
						if (rec.iLen >= 12)
						{
							TRACE("new val=%d, ", inc);
							pScript->SetVar(j, inc, idx);
						}
						TRACE("skip %d records\n", rec.iNext);
						pScript->SetRecord(pScript->GetRecord() + rec.iNext);	// Skip over records
					}
				}
				else
				{
					printf("\n%s: [%d] Var_Not_Found!\n", var);
					Result = TXSCRIPT_VAR_NOT_FOUND;
					bSuccess = false;
					break;
				}
			}
		}
		break;

		default:
			printf("%s: [%d] Type Error!\n", getname(), rec.iType);
			Result = TXSCRIPT_ERROR;
			bSuccess = FALSE;
		}
	}
	if (bSuccess)
	{
		printf("%s: Success\n", getname());
		Result = TXSCRIPT_SUCCESS;
	}
	else
	{
		printf("%s: Failed\n", getname());
	}
	TMR_Stop(tmr);
	TMR_Delete(tmr);
	e.Release();
}
#endif

#ifdef TMR_TESTING
class ATmr : CTimerFunc
{
	void Func(void)
	{
		putchar('.');
	}
};

ATmr atmr;
#endif

int main()
{
	// Open log file to capture output from putchar and printf macros.
	LOG_Init("C:\\winlib.txt");

	printf("winlib\n");

	TMR_Init(100);	// 100ms timebase

	/* Serial port testing. */
	if (!com.Start())
	{
		exit(-1);
	}
	com.Resume();

#ifdef RUN_TESTING
	SCRIPT *sp = new SCRIPT();
	if (sp)
	{
		/* Load and parse the SCRIPT messages from a file. */
		int result = sp->Load("c:\\temp\\test.spt");
		if (result == 0)
		{
			/* Initialize the TxScript processing. */
			TxScript *txscript = new TxScript(&com);

			result = txscript->RunScript(sp);

			/* Terminate the TxScript thread. */
			delete txscript;
			SCRIPT_REC *r = sp->GetVarRecPtr(2);
			if (r != NULL)
			{
				SCRIPT_RECDATA *d = (SCRIPT_RECDATA *)r->pData;
				//MEM_Dump((unsigned char *)d->iArray, r->iLen, 0L);
				printf("var 2:");
				//for (int i = 0; i < 5; i++)
				//{
				//	printf("\\x%08X,", d->iArray[i]);
				//}
				//printf(" as char:");
				for (int i = 0; i < 5; i++)
				{
					printf("%c,", (char)d->iArray[i]);
				}
				printf("\n");
			}
		}
		sp->Dump();
		delete sp;
		printf("result=%d\n", result);
	}
#endif

#ifdef COM_TESTING
	unsigned char buf[80];
	com.Write("AT\r", 3);
	com.Sleep(2000);
	int n = com.Read((LPSTR)buf, sizeof(buf));
	if (n > 0)
	{
		for (int i = 0; i < n; i++)
		{
			printf("%c", buf[i]);
		}
		putchar('\n');
	}
#endif

#ifdef TMR_TESTING
	/* Timer testing. */
	printf("\nset 1 second timer ");
	Tmr t = TMR_New();
	TMR_Start(t, 10);
	while (!TMR_IsTimeout(t))
	{
		TMR_Delay(1);
		putchar('.');
	}
	printf("done\n");

	printf("periodic heartbeat ");
	TMR_Event(5, (CTimerEvent *)&atmr, PERIODIC);
	TMR_Delay(20);
	printf("2 seconds ");
	TMR_Delay(50);
	printf("5 seconds ");
	TMR_Event(0, (CTimerEvent *)&atmr, PERIODIC);
	putchar('\n');
#endif

	com.Stop();
	TMR_Term();

	printf("exit\n\n");

	// Close capture log file.
	LOG_Term();
	
	return 0;
}

