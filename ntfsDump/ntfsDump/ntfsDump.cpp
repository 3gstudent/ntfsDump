
#include "stdafx.h"

#include <Windows.h>
#include <atlstr.h>

#include "NTFS_Common.h"
#include "NTFS_FileRecord.h"

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		printf("Use to copy a file from an NTFS partitioned volume by reading the raw volume and parsing the NTFS structures.\n");			printf("Similar to https://github.com/PowerShellMafia/PowerSploit/blob/master/Exfiltration/Invoke-NinjaCopy.ps1\n");	
		printf("Reference:https://www.codeproject.com/Articles/81456/An-NTFS-Parser-Lib\n");	
		printf("Usage:\n");
		printf("%s <source file full path> <target file path>\n", argv[0]);
		return 0;
	}

	CString m_filename = argv[1];
	_TCHAR volname = m_filename.GetAt(0);

	CNTFSVolume volume(volname);
	if (!volume.IsVolumeOK())
	{
		printf("[!] Not a valid NTFS volume or NTFS version < 3.0\n");
		return 0;
	}

	// parse root directory
	printf("[*] Try to parse root directory\n");
	CFileRecord fr(&volume);
	// we only need to parse INDEX_ROOT and INDEX_ALLOCATION
	// don't waste time and ram to parse unwanted attributes
	fr.SetAttrMask(MASK_INDEX_ROOT | MASK_INDEX_ALLOCATION);

	if (!fr.ParseFileRecord(MFT_IDX_ROOT))
	{
		printf("[!] Cannot read root directory of volume\n");
		return 0;
	}

	if (!fr.ParseAttrs())
	{
		printf("[!] Cannot parse attributes\n");
		return 0;
	}

	// find subdirectory
	printf("[*] Try to find subdirectory\n");
	CIndexEntry ie;

	int dirs = m_filename.Find(_T('\\'), 0);
	int dire = m_filename.Find(_T('\\'), dirs + 1);
	while (dire != -1)
	{
		CString pathname = m_filename.Mid(dirs + 1, dire - dirs - 1);

		if (fr.FindSubEntry((const _TCHAR*)pathname, ie))
		{
			if (!fr.ParseFileRecord(ie.GetFileReference()))
			{
				printf("[!] Cannot read root directory of volume\n");
				return 0;
			}

			if (!fr.ParseAttrs())
			{
				if (fr.IsCompressed())
					printf("[!] Compressed directory not supported yet\n");
				else if (fr.IsEncrypted())
					printf("[!] Encrypted directory not supported yet\n");
				else
					printf("[!] Cannot parse directory attributes\n");
				return 0;
			}
		}
		else
		{
			printf("[!] File not found\n");
			return 0;
		}

		dirs = dire;
		dire = m_filename.Find(_T('\\'), dirs + 1);
	}

	// dump it !
	printf("[*] Try to dump\n");
	CString filename = m_filename.Right(m_filename.GetLength() - dirs - 1);
	if (fr.FindSubEntry((const _TCHAR*)filename, ie))
	{
		if (!fr.ParseFileRecord(ie.GetFileReference()))
		{
			printf("[!] Cannot read file\n");
			return 0;
		}

		// We only need DATA attribute and StdInfo
		fr.SetAttrMask(MASK_DATA);
		if (!fr.ParseAttrs())
		{
			if (fr.IsCompressed())
				printf("[!] Compressed file not supported yet\n");
			else if (fr.IsEncrypted())
				printf("[!] Encrypted file not supported yet\n");
			else
				printf("[!] Cannot parse file attributes\n");
			return 0;
		}

		BYTE *filebuf = new BYTE[1024 * 1024];
		int bufSize = 1024 * 1024;

		// only pick the unnamed stream (file data)
		const CAttrBase *data = fr.FindStream();
		if (data)
		{
			DWORD datalen = (DWORD)data->GetDataSize();
			printf("    source file size:%d\n", datalen);
			DWORD len;
			if (datalen < bufSize)
			{
				if (data->ReadData(0, filebuf, datalen, &len) && len == datalen)
				{
					FILE* fp;
					int err = fopen_s(&fp, argv[2], "wb+");
					if (err != 0)
					{
						printf("[!] createfile error\n");
						fclose(fp);
						return 0;
					}

					fwrite(filebuf, len, 1, fp);
					fclose(fp);
				}
				else
				{
					printf("[!] Read data error\n");
					return 0;
				}
			}
			else
			{
				printf("    start reading through the while loop\n");

				FILE* fp;
				int err = fopen_s(&fp, argv[2], "wb+");
				if (err != 0)
				{
					printf("[!] createfile error\n");
					fclose(fp);
					return 0;
				}
				int leftdataSize = datalen;
				int offset = 0;
				int readSize = bufSize;
				printf("    remaining:");
				while (leftdataSize > 0)
				{
					if (data->ReadData(offset, filebuf, readSize, &len) && len == readSize)
					{
						fwrite(filebuf, len, 1, fp);
						printf("%d...", leftdataSize);

						leftdataSize = leftdataSize - bufSize;
						offset = offset + bufSize;
						if (leftdataSize < bufSize)
							readSize = leftdataSize;
					}
					else
					{
						printf("[!] Read data error\n");
						fclose(fp);
						return 0;
					}
				}
				fclose(fp);
			}
		}

		delete filebuf;
		printf("\n[*] All done.\n");
	}
	else
	{
		printf("[!] File not found\n");
		return 0;
	}

	return 0;
}

