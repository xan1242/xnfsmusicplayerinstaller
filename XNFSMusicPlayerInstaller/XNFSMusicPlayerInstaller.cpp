// XNFSMusicPlayerInstaller - the ingame installer!
// by Xanvier / xan1242

#include "..\includes\IniReader.h"
#include "..\includes\injector\hooking.hpp"
#include <direct.h>
#include <stdlib.h>
#include "XNFSMusicPlayerInstaller.h"

FILE *fin, *fin2, *batchfileout;
struct stat st = { 0 };

bool bInstallerCompleted = 0;
bool bInstallerSuccess = 0;
int ShowFileErrorDialog = 0;
bool bASFScannerFinished = 0;
bool bNodeScannerFinished = 0;
bool bScanForAllFiles = 0;
bool bInteractiveMusicFolderExists = 0;
bool bEnableInteractiveNoding = 1;
bool bDialogShownAtLeastOnce = 0;
bool bBreakThreadLoop = 0;
bool bUseOGGenc = 1;
bool bUseASF = 1;
float OGGEncQuality = 6.0;
int ButtonResult = 0;
int SystemReturnValue = 0;

int UpdateDialogCharBuffer(const char* format, ...)
{
	char TempBuf[255];
	va_list Args;
	va_start(Args, format);
	vfprintf(stdout, format, Args);
	vsprintf(TempBuf, format, Args);
	va_end(Args);

	if (bSecondPrintLine)
		strcpy(PrintfOutputBuffer1, TempBuf);
	else
		strcpy(PrintfOutputBuffer2, TempBuf);
	bSecondPrintLine = !bSecondPrintLine;

	CurrentDialog_FEPrintf(NULL, MESSAGEOBJECTHASH, "%s\n%s", PrintfOutputBuffer1, PrintfOutputBuffer2);
	return 0;
}

void(__thiscall *cFEng_QueueMessage)(void* dis, unsigned int Message, const char* pkg_name, void* FEObject, unsigned int unk) = (void(__thiscall*)(void*, unsigned int, const char*, void*, unsigned int))CFENGQUEUEMESSAGE_ADDRESS;

void __stdcall QueueGameMessageHook(unsigned int Message, const char* pkg_name, unsigned int unk)
{
	ButtonResult = Message;
	cFEng_QueueMessage(*(void**)CFENGINSTANCE_ADDRESS, Message, pkg_name, (void*)0xFFFFFFFF, unk);
}

const char SXBatchScript[] = "for %%f in (\"InteractiveMusic/*.asf\") do scripts\\XNFSInstallerTools\\sx.exe -wave \"InteractiveMusic/%%f\" -=\"InteractiveMusic/%%f.wav\"\n\
@exit /b 69";

const char OGGEncBatchScript[] = "for %%%%f in (\"InteractiveMusic/*.wav\") do scripts\\XNFSInstallerTools\\oggenc2.exe -q %f \"InteractiveMusic/%%%%f\"\n\
@exit /b 68";

const char ASFCleanupScript[] = "del InteractiveMusic\\*.asf /Q\n\
@exit /b 67";

const char WAVCleanupScript[] = "del InteractiveMusic\\*.wav /Q\n\
@exit /b 66";

char* FilenameFormat;

char *BatchScriptBuffer;
char OGGEncMessageBuffer[127];

unsigned int ReadIDFromBank(FILE *fp)
{
	long int OldOffset = 0;
	unsigned int ID = 0;
	OldOffset = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fread(&ID, 4, 1, fp);
	fseek(fp, OldOffset, SEEK_SET);
	return ID;
}

long int SearchForBankIDOffset(FILE *fp, unsigned int IDtoSearch)
{
	long int OldOffset = 0;
	unsigned int CurrentID = 0;
	unsigned int IDOffset = 0;
	OldOffset = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	UpdateDialogCharBuffer("Searching for ID: %x\n", IDtoSearch);
	while (!feof(fp))
	{
		fread(&CurrentID, 4, 1, fp);
		if (CurrentID == IDtoSearch)
		{
			fseek(fp, -4, SEEK_CUR);
			IDOffset = ftell(fp);
			UpdateDialogCharBuffer("Found at %x\n", IDOffset);
			break;
		}
	}
	if (IDOffset == 0)
		UpdateDialogCharBuffer("ERROR: ID %x not found.\n", IDtoSearch);
	fseek(fp, OldOffset, SEEK_SET);
	return IDOffset;
}

int OutputNodeInfoToFile(FILE *fp, long OffsetInMPF, const char* OutTxtFileName, const char* OverrideTxtFileName, const char* BaseFileNameFormat)
{
	FILE *fout = fopen(OutTxtFileName, "w");
	FILE *fout2 = fopen(OverrideTxtFileName, "w");
	if (fout == NULL || fout2 == NULL)
	{
		perror("ERROR");
		return (-1);
	}


	fputs("# Interactive music NodeID definition file\n", fout);
	int i = 0;
	unsigned int NodeID = 0;
	unsigned int Time = 0;
	fseek(fp, OffsetInMPF + 0xC, SEEK_SET);
	while (!feof(fp))
	{
		if (fread(&NodeID, 4, 1, fp))
		{
			if ((i >= 0 && i <= 1314) || i >= 3240 && i <= 3256)
			{
				if (i == 1314)
				{
					fputs("# Title screen EventID override\n", fout2);
					fprintf(fout2, "%X = ", TITLESCREENEVENTID);
					fprintf(fout2, BaseFileNameFormat, i);
					fprintf(fout2, "\n");
					fclose(fout2);
				}
				fprintf(fout, "%X = ", NodeID);
				fprintf(fout, BaseFileNameFormat, i);
				fprintf(fout, "\n");
			}
		}
		else
			break;
		if (fread(&Time, 4, 1, fp)) // leftover from other project
		{
			//printf("time ...\n");
			//fprintf(fout, "Time = %.3fs\t%d samples\n", (float)Time / 1000, (int)(((float)Time / 1000)*SampleRate));
		}
		else
			break;
		i++;
	}
	if (!feof(fp))
	{
		fclose(fout);
		return 0;
	}
	UpdateDialogCharBuffer("Total number of nodes: %d\n", i);
	
	fclose(fout);
	return 1;
}

int ScanAndWriteASFs(FILE *fp, char* MainFileName)
{
	FILE *fout;
	void *FileBuffer;
	char OutFileName[267];
	unsigned int Magic = 0;
	unsigned int NextMagic = 0;
	unsigned int counter = 0;
	long unsigned int MagicOffset = 0;
	long unsigned int NextMagicOffset = 0;
	long unsigned int size = 0;
	long unsigned int totalsize = 0;
	fseek(fp, 0, SEEK_END);
	totalsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	UpdateDialogCharBuffer("Searching for streams...\n");
	while (!feof(fp))
	{
		fread(&Magic, 4, 1, fp);
		if (Magic == 0x6C484353)
		{
			MagicOffset = ftell(fp) - 4;
			UpdateDialogCharBuffer("Stream %d found at %X     ", counter, MagicOffset);
			while (!feof(fp))
			{
				fread(&NextMagic, 4, 1, fp);
				if (NextMagic == 0x6C484353)
				{
					NextMagicOffset = ftell(fp) - 4;;
					fseek(fp, MagicOffset, SEEK_SET);
					break;
				}
				NextMagicOffset = 0;
			}

			if (NextMagicOffset)
				size = NextMagicOffset - MagicOffset;
			else
			{
				fseek(fp, MagicOffset, SEEK_SET);
				size = totalsize - MagicOffset;
			}
			UpdateDialogCharBuffer("Size: %X\n", size);

			sprintf(OutFileName, MainFileName, counter);
			if (bScanForAllFiles)
			{
				fout = fopen(OutFileName, "wb");
				FileBuffer = malloc(size);
				fread(FileBuffer, size, 1, fp);
				fwrite(FileBuffer, size, 1, fout);
				free(FileBuffer);
				fclose(fout);
			}
			else if ((counter >= 0 && counter <= 1314) || counter >= 3240)
			{
				fout = fopen(OutFileName, "wb");
				FileBuffer = malloc(size);
				fread(FileBuffer, size, 1, fp);
				fwrite(FileBuffer, size, 1, fout);
				free(FileBuffer);
				fclose(fout);
			}
			else
				UpdateDialogCharBuffer("Skipping %d\n", counter);
			if (NextMagicOffset)
				fseek(fp, NextMagicOffset, SEEK_SET);
			else
				break;
			counter++;
		}
	}
	printf("Extraction successful!\n");
	bASFScannerFinished = 1;
	return 0;
}


DWORD WINAPI InstallerStateManager(LPVOID)
{
	while (1)
	{
		Sleep(10);
		if (bBreakThreadLoop)
			break;
		if (*(int*)THEGAMEFLOWMANAGER_ADDRESS == 3 && !bDialogShownAtLeastOnce)
		{
			DialogInterface_ShowDialog(&IntroMessageConfig);
			bDialogShownAtLeastOnce = 1;
		}
		if (SystemReturnValue == 69)
		{
			DialogInterface_ShowDialog(&ASFCleanup);
			SystemReturnValue = 0;
		}
		if (SystemReturnValue == 68)
		{
			DialogInterface_ShowDialog(&WAVCleanup);
			SystemReturnValue = 0;
		}
		if (SystemReturnValue == 67)
		{
			if(bUseOGGenc)
				DialogInterface_ShowDialog(&ASFCleanupDone1);
			else
				DialogInterface_ShowDialog(&ASFCleanupDone2);
			SystemReturnValue = 0;
		}
		if (SystemReturnValue == 66)
		{
			DialogInterface_ShowDialog(&WAVCleanupDone);
			SystemReturnValue = 0;
		}
		if (bNodeScannerFinished)
		{
			DialogInterface_ShowDialog(&InstallerSuccessMsg);
			bInstallerSuccess = 1;
			bNodeScannerFinished = 0;
		}
		if (ShowFileErrorDialog == 1)
		{
			DialogInterface_ShowDialog(&FileOpenErrorMsg);
			ShowFileErrorDialog = 0;
		}
		if (ShowFileErrorDialog == 2)
		{
			DialogInterface_ShowDialog(&FileOpenErrorMsg);
			ShowFileErrorDialog = 0;
		}
		if (bASFScannerFinished)
		{
			if (bUseASF)
				DialogInterface_ShowDialog(&ExtractSuccess1);
			else
				DialogInterface_ShowDialog(&ExtractSuccess2);
			bASFScannerFinished = 0;
		}

		switch (ButtonResult)
		{
			if (bDialogShownAtLeastOnce && *(int*)THEGAMEFLOWMANAGER_ADDRESS == 3)
			{
		case OK01:
			DialogInterface_ShowDialog_Custom(&InteractiveQuestion, INTERACTIVEYES, INTERACTIVENO, 0);
			break;
		case NO01:
			DialogInterface_ShowDialog(&NotInteractiveConfig);
			bInstallerSuccess = 1;
			bEnableInteractiveNoding = 0;
			break;
		case OK02:
			bBreakThreadLoop = 1;
			break;
		case YES1:
			DialogInterface_ShowDialog(&StartMessageConfig);
			break;
		case OK03:
			DialogInterface_ShowDialog_Custom(&FormatQuestion, ASFBUTTON, WAVBUTTON, OGGBUTTON);
			break;
		case USEWAV:
			bUseOGGenc = 0;
		case USEOGG:
			bUseASF = 0;
		case USEASF:
			DialogInterface_ShowDialog_Custom(&ExtractionMessage, EXTRACTIONBUTTON, 0, 0);
			break;
		case OK04:
			if (stat("InteractiveMusic", &st) == -1)
				mkdir("InteractiveMusic");
			else
				bInteractiveMusicFolderExists = 1;
			if (bInteractiveMusicFolderExists)
			{
				Sleep(10);
				DialogInterface_ShowDialog(&AlreadyExistsConfig);
			}
			else
			{
				DialogInterface_ShowDialog(&ExtractingMessage);

				fin = fopen("SOUND\\PFDATA\\MW_Music.mus", "rb");
			if (fin == NULL)
				ShowFileErrorDialog = 1;
			ScanAndWriteASFs(fin, "InteractiveMusic\\MW_Music.mus_%d.asf");
			fclose(fin);
			}
			break;
		case OK05:
			if (!bUseASF)
			{
				if (bInteractiveMusicFolderExists)
					DialogInterface_ShowDialog(&DecodingAlreadyExists);
				else
				{
					DialogInterface_ShowDialog(&SXToolDecodingMsg);
					batchfileout = fopen("XNFSTempBatch.bat", "w");
					fwrite(SXBatchScript, strlen(SXBatchScript), 1, batchfileout);
					fclose(batchfileout);
					SystemReturnValue = system("XNFSTempBatch.bat");
					remove("XNFSTempBatch.bat");
				}
			}
			break;
		case YES3:
			CurrentDialog_FEPrintf(NULL, MESSAGEOBJECTHASH, ASFCLEANUPMESSAGE);
			batchfileout = fopen("XNFSTempBatch3.bat", "w");
			fwrite(ASFCleanupScript, strlen(ASFCleanupScript), 1, batchfileout);
			fclose(batchfileout);
			SystemReturnValue = system("XNFSTempBatch3.bat");
			remove("XNFSTempBatch3.bat");
			break;
		case NO03:
			if (bUseOGGenc)
				DialogInterface_ShowDialog(&ASFNotCleaning1);
			else
				DialogInterface_ShowDialog(&ASFNotCleaning2);
			break;
		case YES4:
			CurrentDialog_FEPrintf(NULL, MESSAGEOBJECTHASH, WAVCLEANUPMESSAGE);
			batchfileout = fopen("XNFSTempBatch4.bat", "w");
			fwrite(WAVCleanupScript, strlen(WAVCleanupScript), 1, batchfileout);
			fclose(batchfileout);
			SystemReturnValue = system("XNFSTempBatch4.bat");
			remove("XNFSTempBatch4.bat");
			break;
		case NO04:
			DialogInterface_ShowDialog(&WAVNotCleaning);
			break;
		case OK06:
			if (bUseOGGenc)
			{
				DialogInterface_ShowMessage(DLG_ANIMATING, DLGTITLE_ATTN, OGGENCODINGFORMATTER, OGGEncQuality);
				BatchScriptBuffer = (char*)malloc(strlen(OGGEncBatchScript));
				sprintf(BatchScriptBuffer, OGGEncBatchScript, OGGEncQuality);
				batchfileout = fopen("XNFSTempBatch2.bat", "w");
				fwrite(BatchScriptBuffer, strlen(BatchScriptBuffer), 1, batchfileout);
				fclose(batchfileout);
				SystemReturnValue = system("XNFSTempBatch2.bat");
				remove("XNFSTempBatch2.bat");
			}
			break;
		case OK07:
			DialogInterface_ShowDialog(&ExtractingMessage);
			fin = fopen("SOUND\\PFDATA\\MW_Music.mus", "rb");
			if (fin == NULL)
			{
				ShowFileErrorDialog = 1;
				break;
			}
			fin2 = fopen("SOUND\\PFDATA\\MW_Music.mpf", "rb");
			if (fin2 == NULL)
			{
				ShowFileErrorDialog = 2;
				break;
			}
			if (bUseASF)
				FilenameFormat = "InteractiveMusic\\MW_Music.mus_%d.asf";
			else if (bUseOGGenc)
				FilenameFormat = "InteractiveMusic\\MW_Music.mus_%d.asf.ogg";
			else
				FilenameFormat = "InteractiveMusic\\MW_Music.mus_%d.asf.wav";
			if (stat("scripts\\XNFSMusicPlayer", &st) == -1)
				mkdir("scripts\\XNFSMusicPlayer");
			bNodeScannerFinished = OutputNodeInfoToFile(fin2, SearchForBankIDOffset(fin2, ReadIDFromBank(fin)), "scripts\\XNFSMusicPlayer\\NodePaths.txt", "scripts\\XNFSMusicPlayer\\Overrides.txt", FilenameFormat);
			fclose(fin);
			fclose(fin2);
			break;
			}
		}
		ButtonResult = 0;
	}
	if (bInstallerSuccess)
	{
		CIniReader inireader("XNFSMusicPlayer.ini");
		if (!bEnableInteractiveNoding)
			inireader.WriteInteger("Playback", "EnableInteractiveNoding", 0);
		else
			inireader.WriteInteger("Playback", "EnableInteractiveNoding", 1);
		inireader.WriteInteger("Installer", "InstallerCompleted", 1);
	}
	*(unsigned int*)EXITTHEGAMEFLAG_ADDRESS = 1;
	return 0;
}

int Init()
{
	CIniReader inireader("XNFSMusicPlayer.ini");
	bInstallerCompleted = inireader.ReadInteger("Installer", "InstallerCompleted", 0);
	bScanForAllFiles = inireader.ReadInteger("Installer", "ScanForAllFiles", 0);
	OGGEncQuality = inireader.ReadFloat("Installer", "OGGEncQuality", 6.0);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		freopen("CON", "w", stdout);
		freopen("CON", "w", stderr);
		Init();
		if (!bInstallerCompleted)
		{
			*(unsigned int*)BOOTFLOW_ADDRESS = SOMEBROKENFNG_ADDRESS;
			injector::MakeCALL(QUEUEGAMEMESSAGEHOOK_ADDRESS, QueueGameMessageHook, true);
			CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&InstallerStateManager, NULL, 0, NULL);
		}
	}
	return TRUE;
}
