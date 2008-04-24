// FavIconizer

// Copyright (C) 2007-2008 - Stefan Kueng

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "FavIconizer.h"
#include "DirFileEnum.h"
#include "shelllink.h"
#include "Debug.h"
#include <vector>
#include "shlwapi.h"
#include <regex>

using namespace std;

#ifndef WIN64
#	pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='X86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#	pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"") 
#endif


#pragma comment(lib, "Urlmon")
#pragma comment(lib, "shlwapi")

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
volatile LONG g_bThreadRunning = FALSE;
volatile LONG g_bUserCancelled = FALSE;
bool g_bFetchAll = false;

// Forward declarations of functions included in this code module:
INT_PTR CALLBACK	MainDlg(HWND, UINT, WPARAM, LPARAM);
bool IsIconOrBmp(BYTE* pBuffer, DWORD dwLen);
DWORD EndThreadWithError(HWND hwndDlg);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	hInst = hInstance;
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
	InterlockedExchange(&g_bThreadRunning, TRUE);
	HWND hwndDlg = (HWND)lParam;

	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDMISSING), TRUE);
	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDALL), TRUE);

	if (::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)!=S_OK)
	{
		return EndThreadWithError(hwndDlg);
	}

	//first get the favorites folder of the current user
	TCHAR FavPath[MAX_PATH];
	if (!SHGetSpecialFolderPath(NULL, FavPath, CSIDL_FAVORITES, FALSE))
	{
		//no favorites folder?
		MessageBox(hwndDlg, _T("could not locate your favorites folder!"), szTitle, MB_OK | MB_ICONEXCLAMATION);
		return EndThreadWithError(hwndDlg);
	}

	if (!SetCurrentDirectory(FavPath))
	{
		MessageBox(hwndDlg, _T("could not set the current directory!"), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
		return EndThreadWithError(hwndDlg);
	}

	TCHAR FavIconPath[MAX_PATH];
	if (!SHGetSpecialFolderPath(NULL, FavIconPath, CSIDL_APPDATA, FALSE))
	{
		//no favorites folder?
		MessageBox(hwndDlg, _T("could not locate the %APPDATA% folder!"), szTitle, MB_OK | MB_ICONEXCLAMATION);
		return EndThreadWithError(hwndDlg);
	}

	if (_tcscat_s(FavIconPath, MAX_PATH, _T("\\FavIconizer")))
	{
		return EndThreadWithError(hwndDlg);
	}
	CreateDirectory(FavIconPath, NULL);
	if (!PathIsDirectory(FavIconPath))
	{
		return EndThreadWithError(hwndDlg);
	}

	SetWindowText(GetDlgItem(hwndDlg, IDOK), _T("&Cancel"));

	std::vector<std::wstring> filelist;
	CDirFileEnum fileenumerator(FavPath);
	TCHAR currentPath[MAX_PATH];
	bool bIsDir = false;
	int nTotalLinks = 0;
	while (fileenumerator.NextFile(currentPath, &bIsDir))
	{
		if (!bIsDir)	// exclude directories
		{
			// also exclude non-url files
			size_t len = _tcslen(currentPath);
			if (_tcsicmp(&currentPath[len-4], _T(".url"))==0)
			{
				nTotalLinks++;
				CUrlShellLink link;
				if (!link.Load(currentPath))
					continue;
				if ((!link.GetIconLocation().empty())&&(!link.GetIconLocation().substr(0, 4).compare(_T("http"))==0))
				{
					// link already has a non-url icon path
					if (!g_bFetchAll)
						continue;	// only fetch not already fetched icons.
				}
				filelist.push_back(std::wstring(currentPath));
			}
		}
	}

	if (nTotalLinks == 0)
	{
		MessageBox(hwndDlg, _T("No favorite links found to check!"), szTitle, MB_OK | MB_ICONEXCLAMATION);
		return EndThreadWithError(hwndDlg);
	}
	if ((filelist.size() == 0)&&(!g_bFetchAll))
	{
		MessageBox(hwndDlg, _T("All favorite links already have a favicon!"), szTitle, MB_OK | MB_ICONINFORMATION);
		return EndThreadWithError(hwndDlg);
	}

	TCHAR sText[4096];
	if (nTotalLinks == (int)filelist.size())
		_stprintf_s(sText, 4096, _T("Checking link %ld of %ld..."), 0, nTotalLinks);
	else
		_stprintf_s(sText, 4096, _T("Checking link %ld of %ld, skipping %ld links..."), 0, filelist.size(), nTotalLinks-filelist.size());
	SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE1), sText);
	// start with no progress
	ShowWindow(GetDlgItem(hwndDlg, IDC_PROGRESS), SW_SHOW);
	SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETRANGE32, 0, filelist.size());
	SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETSTEP, 1, 0);

	wstring iconURL;

	// regex pattern to match <link rel="icon" href="some/url" type=image/ico>
	// or <link rel="icon" href="some/url" type=image/x-icon>
	const tr1::wregex pat(_T("<link[ \\t\\r\\n]*rel[ \\t\\r\\n]*=[ \\t\\r\\n]*\\\"(shortcut )?icon\\\"[ \\t\\r\\n]*href[ \\t\\r\\n]*=[ \\t\\r\\n\"]*(.*?)[ \\t\\r\\n\"/]*(type[ \\t\\r\\n]*=[ \\t\\r\\n\"]*\\\"image/(ico|x-icon|png|gif)\\\"[ \\t\\r\\n\"/]*)?>"), tr1::regex_constants::icase | tr1::regex_constants::collate | tr1::regex_constants::ECMAScript);

	int count = 0;
	for (std::vector<std::wstring>::iterator it = filelist.begin(); it != filelist.end(); ++it)
	{
		CUrlShellLink link;
		if (!link.Load(it->c_str()))
			continue;
		if (g_bUserCancelled)
			break;
		if (nTotalLinks == (int)filelist.size())
			_stprintf_s(sText, 4096, _T("Checking link %ld of %ld..."), count+1, nTotalLinks);
		else
			_stprintf_s(sText, 4096, _T("Checking link %ld of %ld, skipping %ld links..."), count+1, filelist.size(), nTotalLinks-filelist.size());
		SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE1), sText);
		SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE2), link.GetPath().c_str());
		SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_STEPIT, 0, 0);
		if (_tcsncmp(_T("http"), link.GetPath().c_str(), 4)==0)
		{
			//yes, it's an url to http
			iconURL.clear();
			TCHAR cachefile[MAX_PATH*2] = {0};
			TCHAR cachefolder[MAX_PATH] = {0};
			GetTempPath(MAX_PATH, cachefolder);
			GetTempFileName(cachefolder, _T("fvi"), 0, cachefile);
			if (URLDownloadToFile(NULL, link.GetPath().c_str(), cachefile, 0, NULL)==S_OK)
			{
				FILE *stream;
				errno_t err;

				// Open for read
				if ((err  = _tfopen_s(&stream, cachefile, _T("r") )) == 0)
				{
					// 60'000 bytes should be enough to find the <link...> tag
					char buffer[60000];
					size_t len = fread(buffer, sizeof(char), 60000, stream);
					if (len > 0)
					{
						TCHAR tbuf[70000];
						if (MultiByteToWideChar(CP_ACP, 0, buffer, len, tbuf, 70000))
						{
							wstring reMsg = wstring(tbuf, len);
							try
							{
								const tr1::wsregex_iterator endre;
								for (tr1::wsregex_iterator itre(reMsg.begin(), reMsg.end(), pat); itre != endre; ++itre)
								{
									const tr1::wsmatch match = *itre;
									if (match.size() > 2)
									{
										iconURL = wstring(match[2]).c_str();
									}
								}
							}
							catch (...)
							{
								TRACE(_T("regex error parsing html file\n"));
							}
							fclose(stream);
						}
					}
				}
				else
				{
					TRACE(_T("error opening html file\n"));
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
							if (link.GetPath().at(link.GetPath().size()-1) != '/')
							{
								size_t slashpos = link.GetPath().find_last_of('/');
								if (slashpos != wstring::npos)
								{
									iconURL = link.GetPath().substr(0, slashpos) + iconURL;
								}
							}
							else
							{
								iconURL = link.GetPath() + iconURL;
							}
						}
					}
				}
			}
			if (!iconURL.empty())
			{
				// now download the icon file
				TCHAR tempfilebuf[MAX_PATH*4] = {0};
				TCHAR buf[MAX_PATH] = {0};
				if (GetTempPath(MAX_PATH, buf))
				{
					if (GetTempFileName(buf, _T("fav"), 0, tempfilebuf))
					{
						_tcscat_s(tempfilebuf, MAX_PATH*4, _T(".ico"));
						if (g_bUserCancelled)
							break;
						if (URLDownloadToFile(NULL, iconURL.c_str(), tempfilebuf, 0, NULL) == S_OK)
						{
							// we have downloaded a file, but is it really an icon or maybe a 404 html page?
							bool isIcon = false;
							bool isPNG = false;
							bool isGIF = false;
							FILE * iconfile = NULL;
							if (_tfopen_s(&iconfile, tempfilebuf, _T("r"))==0)
							{
								BYTE* pBuffer = new BYTE[1024];
								size_t numread = fread(pBuffer, sizeof(BYTE), 1024, iconfile);
								if (numread > 0)
								{
									isIcon = IsIconOrBmp(pBuffer, numread);
									if (_strnicmp((const char *)(pBuffer+1), "png", 3) == 0)
										isPNG = true;
									if (_strnicmp((const char *)pBuffer, "gif", 3) == 0)
										isGIF = true;
								}
								fclose(iconfile);
								delete pBuffer;
							}
							else
							{
								TRACE(_T("error opening icon file\n"));
							}
							if (isIcon)
							{
								// store the icons in the users appdata folder
								// the name of the icon is the name of the favorite url file but with .ico instead of .url extension
								FavIconPath;
								wstring filename = *it;
								filename = filename.substr(filename.find_last_of('\\'));
								filename = filename.substr(0, filename.find_last_of('.'));
								wstring ext = iconURL.substr(iconURL.find_last_of('.'));
								if (ext.compare(_T(".ico")) == 0)
								{
									if (isPNG)
										ext = _T(".png");
									if (isGIF)
										ext = _T(".gif");
								}
								filename = filename + ext;
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
			}
			DeleteFile(cachefile);
		}
		count++;
		if (g_bUserCancelled)
			break;
	}
	SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE1), _T(""));
	SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE2), _T(""));
	SetWindowText(GetDlgItem(hwndDlg, IDOK), _T("&Exit"));
	ShowWindow(GetDlgItem(hwndDlg, IDC_PROGRESS), SW_HIDE);

	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDMISSING), TRUE);
	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDALL), TRUE);
	::CoUninitialize();
	InterlockedExchange(&g_bThreadRunning, FALSE);
	return 0;
}

DWORD EndThreadWithError(HWND hwndDlg)
{
	SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE1), _T(""));
	SetWindowText(GetDlgItem(hwndDlg, IDC_PROGLINE2), _T(""));
	SetWindowText(GetDlgItem(hwndDlg, IDOK), _T("&Exit"));
	ShowWindow(GetDlgItem(hwndDlg, IDC_PROGRESS), SW_HIDE);

	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDMISSING), TRUE);
	EnableWindow(GetDlgItem(hwndDlg, IDC_FINDALL), TRUE);
	::CoUninitialize();
	InterlockedExchange(&g_bThreadRunning, FALSE);
	return 1;
}

// Message handler for about box.
INT_PTR CALLBACK MainDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HWND hwndOwner; 
			RECT rc, rcDlg, rcOwner; 

			hwndOwner = GetDesktopWindow(); 

			GetWindowRect(hwndOwner, &rcOwner); 
			GetWindowRect(hDlg, &rcDlg); 
			CopyRect(&rc, &rcOwner); 

			OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top); 
			OffsetRect(&rc, -rc.left, -rc.top); 
			OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom); 

			SetWindowPos(hDlg, HWND_TOP, rcOwner.left + (rc.right / 2), rcOwner.top + (rc.bottom / 2), 0, 0,	SWP_NOSIZE); 
			HICON hIcon = (HICON)::LoadImage(hInst, MAKEINTRESOURCE(IDI_FAVICONIZER), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE|LR_SHARED);
			::SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			::SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
		}
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			if (!g_bThreadRunning)
			{
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
			else
			{
				InterlockedExchange(&g_bUserCancelled, TRUE);
				SetWindowText(GetDlgItem(hDlg, IDC_PROGLINE1), _T("Cancelling, please wait..."));
				SetWindowText(GetDlgItem(hDlg, IDC_PROGLINE2), _T(""));
			}
		}
		if (LOWORD(wParam) == IDC_FINDMISSING)
		{
			if (!g_bThreadRunning)
			{
				g_bFetchAll = false;
				InterlockedExchange(&g_bThreadRunning, TRUE);
				InterlockedExchange(&g_bUserCancelled, FALSE);
				CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
			}
		}
		if (LOWORD(wParam) == IDC_FINDALL)
		{
			if (!g_bThreadRunning)
			{
				g_bFetchAll = true;
				InterlockedExchange(&g_bThreadRunning, TRUE);
				InterlockedExchange(&g_bUserCancelled, FALSE);
				CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
			}
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
		goto checkifpngorgif;

	return true;

checkifpngorgif:
	const char * pngheader = (const char *) pBuffer;

	if (_strnicmp(pngheader+1, "png", 3))
		if (_strnicmp(pngheader, "gif", 3))
			return false;

	return true;
}

