// FavIconizer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "FavIconizer.h"
#include "ProgressDlg.h"
#include "DirFileEnum.h"
#include "shelllink.h"
#include <vector>

#include "regexpr2.h"
using namespace std;
using namespace regex;

#ifndef WIN64
#	pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='X86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#	pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"") 
#endif


#pragma comment(lib, "Urlmon")

#define MAX_LOADSTRING 100

typedef struct tagICONDIRENTRY
{
	BYTE  bWidth;
	BYTE  bHeight;
	BYTE  bColorCount;
	BYTE  bReserved;
	WORD  wPlanes;
	WORD  wBitCount;
	DWORD dwBytesInRes;
	DWORD dwImageOffset;
} ICONDIRENTRY;

typedef struct ICONHEADER
{
	WORD          idReserved;
	WORD          idType;
	WORD          idCount;
	ICONDIRENTRY  idEntries[1];
} ICONHEADER;

#define BM 0x4D42



// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
bool g_bThreadRunning = false;
bool g_bFetchAll = false;

// Forward declarations of functions included in this code module:
INT_PTR CALLBACK	MainDlg(HWND, UINT, WPARAM, LPARAM);
bool IsIconOrBmp(BYTE* pBuffer, DWORD dwLen);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

	HRESULT hRes = ::CoInitialize(NULL);
	if (!SUCCEEDED(hRes))
	{
		MessageBox(NULL, _T("Failed to initialize OLE!"), szTitle, MB_OK | MB_ICONERROR);
		return 1;
	}



	DialogBox(hInst, MAKEINTRESOURCE(IDD_MAINDLG), NULL, MainDlg);

	::CoUninitialize();
	return 0;
}

DWORD WINAPI ScanThread(LPVOID lParam)
{
	g_bThreadRunning = true;
	HWND hwndDlg = (HWND)lParam;

	::CoInitialize(NULL);

	CProgressDlg progDlg;
	progDlg.SetCancelMsg(_T("Cancelling... please wait"));
	progDlg.SetTitle(_T("Retrieving FavIcons from Websites"));
	progDlg.ShowModal(hwndDlg);

	//first get the favorites folder of the current user
	TCHAR FavPath[MAX_PATH];
	if (!SHGetSpecialFolderPath(NULL, FavPath, CSIDL_FAVORITES, FALSE))
	{
		//no favorites folder?
		MessageBox(hwndDlg, _T("could not locate your favorites folder!"), szTitle, MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}

	if (!SetCurrentDirectory(FavPath))
	{
		MessageBox(hwndDlg, _T("could not set the current directory!"), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}

	TCHAR FavIconPath[MAX_PATH];
	if (!SHGetSpecialFolderPath(NULL, FavIconPath, CSIDL_APPDATA, FALSE))
	{
		//no favorites folder?
		MessageBox(hwndDlg, _T("could not locate the %APPDATA% folder!"), szTitle, MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}

	_tcscat_s(FavIconPath, MAX_PATH, _T("\\FavIconizer"));
	CreateDirectory(FavIconPath, NULL);
	SetFileAttributes(FavIconPath, FILE_ATTRIBUTE_HIDDEN);

	std::vector<std::wstring> filelist;
	CDirFileEnum fileenumerator(FavPath);
	TCHAR currentPath[MAX_PATH];
	bool bIsDir = false;
	while (fileenumerator.NextFile(currentPath, &bIsDir))
	{
		if (!bIsDir)	// exclude directories
		{
			// also exclude non-url files
			size_t len = _tcslen(currentPath);
			if (_tcsicmp(&currentPath[len-4], _T(".url"))==0)
				filelist.push_back(std::wstring(currentPath));
		}
	}
	progDlg.SetProgress(0, (DWORD)filelist.size());

	wstring iconURL;

	// regex pattern to match <link rel="icon" href="some/url" type=image/ico>
	// or <link rel="icon" href="some/url" type=image/x-icon>
	rpattern pat(_T("<link[ \\t\\r\\n]*rel[ \\t\\r\\n]*=[ \\t\\r\\n]*\\\"icon\\\"[ \\t\\r\\n]*href[ \\t\\r\\n]*=[ \\t\\r\\n]*(.*?)[ \\t\\r\\n]*type[ \\t\\r\\n]*=[ \\t\\r\\n]*\\\"image/(ico|x-icon)\\\"[ \\t\\r\\n]*>"), _T(""), NOCASE|NORMALIZE|MULTILINE);

	int count = 0;
	for (std::vector<std::wstring>::iterator it = filelist.begin(); it != filelist.end(); ++it)
	{
		CUrlShellLink link;
		if (!link.Load(it->c_str()))
			continue;
		if (!link.GetIconLocation().substr(0, 4).compare(_T("http"))==0)
		{
			// link already has a non-url icon path
			if (!g_bFetchAll)
				continue;	// only fetch not already fetched icons.
		}
		progDlg.SetLine(1, link.GetPath().c_str(), true);
		if (_tcsncmp(_T("http"), link.GetPath().c_str(), 4)==0)
		{
			//yes, it's an url to http
			TCHAR cachefile[MAX_PATH*2];
			if (SUCCEEDED(URLDownloadToCacheFile(NULL, link.GetPath().c_str(), cachefile, MAX_PATH*4, 0, NULL)))
			{
				FILE *stream;
				errno_t err;

				iconURL.clear();
				// Open for read
				if ((err  = _tfopen_s(&stream, cachefile, _T("r") )) ==0)
				{
					//printf( "The file 'crt_fopen_s.c' was not opened\n" );
					char buffer[60000];
					size_t len = fread(buffer, sizeof(char), 60000, stream);
					TCHAR tbuf[60000];
					MultiByteToWideChar(CP_ACP, 0, buffer, len, tbuf, 60000);
					wstring reMsg = wstring(tbuf, len);
					try
					{
						match_results results;
						match_results::backref_type br = pat.match(reMsg, results);

						if (br.matched)
						{
							if (results.rlength(1)>0)
							{
								iconURL = results.backref(1).str();
							}
						}
						fclose(stream);
					}
					catch (...)
					{
					}
				}
				if (iconURL.empty())
				{
					// no icon url found in html tag: use http://www.domain.com/favicon.ico
					size_t off = 0;
					iconURL = link.GetPath();
					off = iconURL.find(':');
					if (off != string::npos)
					{
						off = iconURL.find('/', off+3);
						if (off != string::npos)
						{
							iconURL = iconURL.substr(0, off);
							iconURL += _T("/favicon.ico");
						}
					}
				}
				else
				{
					if (_tcsnicmp(iconURL.c_str(), _T("http://"), 7)!=0)
					{
						// not an absolute url but a relative one
						// we need to create an absolute url
						if (link.GetPath().c_str()[0] == '/')
						{
							size_t off = 0;
							iconURL = link.GetPath();
							off = iconURL.find(':');
							if (off != string::npos)
							{
								off = iconURL.find('/', off+1);
								if (off != string::npos)
								{
									iconURL = iconURL.substr(0, off) + _T("/") + iconURL;
								}
							}
						}
						else
						{
							size_t slashpos = link.GetPath().find_last_of('/');
							if (slashpos != wstring::npos)
							{
								iconURL = link.GetPath().substr(0, slashpos) + iconURL;
							}
						}
					}
				}
			}
			if (!iconURL.empty())
			{
				// now download the icon file
				TCHAR tempfilebuf[MAX_PATH*4];
				TCHAR buf[MAX_PATH];
				GetTempPath(MAX_PATH, buf);
				GetTempFileName(buf, _T("fav"), 0, tempfilebuf);
				_tcscat_s(tempfilebuf, MAX_PATH*4, _T(".ico"));
				if (SUCCEEDED(URLDownloadToFile(NULL, iconURL.c_str(), tempfilebuf, 0, NULL)))
				{
					// we have downloaded a file, but is it really an icon or maybe a 404 html page?
					bool isIcon = false;
					FILE * iconfile = NULL;
					if (_tfopen_s(&iconfile, tempfilebuf, _T("r"))==0)
					{
						BYTE* pBuffer = new BYTE[1024];
						size_t numread = fread(pBuffer, sizeof(BYTE), 1024, iconfile);
						isIcon = IsIconOrBmp(pBuffer, numread);
						fclose(iconfile);
						delete pBuffer;
					}
					if (isIcon)
					{
						// store the icons in the users appdata folder
						// the name of the icon is the name of the favorite url file but with .ico instead of .url extension
						FavIconPath;
						wstring filename = *it;
						filename = filename.substr(filename.find_last_of('\\'));
						filename = filename.substr(0, filename.find_last_of('.'));
						filename = filename + _T(".ico");
						wstring iconFilePath = FavIconPath;
						iconFilePath = iconFilePath + _T("\\") + filename;
						DeleteFile(iconFilePath.c_str());
						MoveFile(tempfilebuf, iconFilePath.c_str());
						link.SetIconLocation(iconFilePath.c_str());
						link.SetIconLocationIndex(0);
						link.Save(it->c_str());
					} 
				}
			}
		}
		count++;
		progDlg.SetProgress(count, filelist.size());
	}
	progDlg.Stop();
	::CoUninitialize();
	return 0;

}

// Message handler for about box.
INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		if (LOWORD(wParam) == IDC_FINDMISSING)
		{
			g_bFetchAll = false;
			CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
		}
		if (LOWORD(wParam) == IDC_FINDALL)
		{
			g_bFetchAll = true;
			CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
		}
		break;
	}
	return (INT_PTR)FALSE;
}


bool IsIconOrBmp(BYTE* pBuffer, DWORD dwLen)
{
	// Quick and dirty check to see if we actually got
	// an icon or a bitmap
	ICONHEADER*			pIconHeader = (ICONHEADER*) pBuffer;
	ICONDIRENTRY*		pIconEntry = (ICONDIRENTRY*) (pBuffer + sizeof(WORD) * 3);

	if ((pIconHeader->idType == 1)&&
		(pIconHeader->idReserved == 0)&&
		(dwLen >= sizeof(ICONHEADER) + sizeof(ICONDIRENTRY)) )
	{
		if (pIconEntry->dwImageOffset >= dwLen)
			goto checkifbmp;

		return true;
	}

	// Not an icon
checkifbmp:

	BITMAPFILEHEADER* pBmpFileHeader = (BITMAPFILEHEADER*) pBuffer;

	if ((dwLen < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))||
		(pBmpFileHeader->bfType != BM)||
		(pBmpFileHeader->bfSize != dwLen))
		return false;

	return true;
}

