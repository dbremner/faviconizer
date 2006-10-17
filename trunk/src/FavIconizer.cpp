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


#pragma comment(lib, "Urlmon")

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
bool g_bThreadRunning = false;

// Forward declarations of functions included in this code module:
INT_PTR CALLBACK	MainDlg(HWND, UINT, WPARAM, LPARAM);

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
	_tcscpy_s(FavIconPath, MAX_PATH, FavPath);
	_tcscat_s(FavIconPath, MAX_PATH, _T("\\_icons"));

	// maybe check here for success?
	CreateDirectory(FavIconPath, NULL);
	SetFileAttributes(FavIconPath, FILE_ATTRIBUTE_HIDDEN);

	std::vector<std::wstring> filelist;
	CDirFileEnum fileenumerator(FavPath);
	TCHAR currentPath[MAX_PATH];
	bool bIsDir = false;
	while (fileenumerator.NextFile(currentPath, &bIsDir))
	{
		if (!bIsDir)
			filelist.push_back(std::wstring(currentPath));
	}
	progDlg.SetProgress(0, (DWORD)filelist.size());

	wstring iconURL;

	rpattern pat(_T("<link[ \\t\\r\\n]*rel[ \\t\\r\\n]*=[ \\t\\r\\n]*\\\"icon\\\"[ \\t\\r\\n]*href[ \\t\\r\\n]*=[ \\t\\r\\n]*(.*?)[ \\t\\r\\n]*type[ \\t\\r\\n]*=[ \\t\\r\\n]*\\\"image/(ico|x-icon)\\\"[ \\t\\r\\n]*>"), _T(""), NOCASE|NORMALIZE|MULTILINE);

	for (std::vector<std::wstring>::iterator it = filelist.begin(); it != filelist.end(); ++it)
	{
		CUrlShellLink link;
		if (!link.Load(it->c_str()))
			continue;
		progDlg.SetLine(1, link.GetPath().c_str(), true);
		if (_tcsncmp(_T("http"), link.GetPath().c_str(), 4)==0)
		{
			//yes, it's an url to http
			TCHAR cachefile[MAX_PATH*2];
			if (SUCCEEDED(URLDownloadToCacheFile(NULL, link.GetPath().c_str(), cachefile, MAX_PATH*4, 0, NULL)))
			{
				FILE *stream;
				errno_t err;

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
						off = iconURL.find('/', off+1);
						if (off != string::npos)
						{
							iconURL = iconURL.substr(0, off);
							iconURL += _T("/favicon.ico");
						}
					}
				}
			}
		}
	}
/*
	for (int i=0; i<filelist.GetSize(); i++)
	{
		if (link.GetPath().Left(4).CompareNoCase(_T("http"))==0)
		{
			CString iconURL;
			if (iconURL.IsEmpty())
			{
				iconURL = _T("favicon.ico");
				DWORD dwService;
				CString strServer;
				CString strObject;
				INTERNET_PORT nPort;
				AfxParseURL(link.GetPath(), dwService, strServer, strObject, nPort);
				iconURL = _T("http://") + strServer + _T("/") + iconURL;
			} // if (iconURL.IsEmpty())
			else
			{
				if (!iconURL.Left(4).CompareNoCase(_T("http"))==0)
				{
					//not a full URL but a relative one
					if (iconURL.GetAt(0) == '/')
					{
						DWORD dwService;
						CString strServer;
						CString strObject;
						INTERNET_PORT nPort;
						AfxParseURL(link.GetPath(), dwService, strServer, strObject, nPort);
						iconURL = _T("http://") + strServer + _T("/") + iconURL;
					} // if (iconURL.GetAt(0) == '/')
					else
					{
						iconURL = link.GetPath().Left(link.GetPath().ReverseFind('/') + 1) + iconURL;
					}
				} // if (!iconURL.Left(4).CompareNoCase(_T("http"))==0)				
			}
			if (!m_runthread)
				break;
			//it's time to fetch the icon
			try
			{
				CInternetSession iconsession;
				GetDlgItem(IDC_LINKSTATUS)->SetWindowText(_T("getting icon..."));
				CStdioFile * pIconFile = iconsession.OpenURL(iconURL, 1, INTERNET_FLAG_TRANSFER_BINARY |INTERNET_FLAG_EXISTING_CONNECT);
				GetTempPath(sizeof(buf)/sizeof(TCHAR), buf);
				TCHAR tempfilebuf[MAX_PATH];
				GetTempFileName(buf, _T("fav"), 0, tempfilebuf);
				_tcscat(tempfilebuf, _T(".ico"));
				CFile iconfile;
				iconfile.Open(tempfilebuf, CFile::modeCreate | CFile::modeReadWrite);
				int len = 0;
				while (len = pIconFile->Read(buf, sizeof(buf)))
				{
					iconfile.Write(buf, len);
				}
				iconfile.Close();
				pIconFile->Close();
				delete pIconFile;
				iconsession.Close();
				//now check if it's really an icon we got
				BOOL isIcon = FALSE;
				if(iconfile.Open(tempfilebuf, CFile::modeRead | CFile::typeBinary))
				{
					int nSize = (int)iconfile.GetLength();
					BYTE* pBuffer = new BYTE[nSize];

					if(iconfile.Read(pBuffer, nSize) > 0)
					{
						if (IsIconOrBmp(pBuffer, nSize))
							isIcon = TRUE;
					}

					iconfile.Close();
					delete [] pBuffer;
				}
				if (isIcon)
				{
					GetDlgItem(IDC_LINKSTATUS)->SetWindowText(_T("storing icon..."));
					CString iconpath = _T("_icons\\") + linkfile.Right(linkfile.GetLength() - linkfile.ReverseFind('\\') - 1);
					iconpath = iconpath.Left(iconpath.ReverseFind('.')) + _T(".ico");
					DeleteFile(iconpath);
					MoveFile(tempfilebuf, iconpath);
					link.SetIconLocation(iconpath);
					link.SetIconLocationIndex(0);
					link.Save(linkfile);
					nIcons++;
				} // if (isIcon)
				else
				{
					GetDlgItem(IDC_LINKSTATUS)->SetWindowText(_T("no icon found!"));
					DeleteFile(tempfilebuf);
				}
			}
			catch (CInternetException* pEx)
			{
				pEx->Delete();
			}
			catch (CFileException* pEx)
			{
				pEx->Delete();
			}
		} // if (link.GetPath().Left(4).CompareNoCase(_T("http"))==0) 
		if (!m_runthread)
			break;
		m_totalProgress.StepIt();
		result.Format(_T("%d of %d links processed. %d icons found"), i+1, filelist.GetSize(), nIcons);
		GetDlgItem(IDC_RESULT)->SetWindowText(result);

	} // for (int i=0; i<filelist.GetSize(); i++) 

	//Closedown the OLE subsystem
	::CoUninitialize();
	GetDlgItem(IDOK)->SetWindowText(_T("OK"));
	GetDlgItem(IDC_LINKSTATUS)->SetWindowText(_T("finished!"));
	GetDlgItem(IDC_LINKPATH)->SetWindowText(_T(""));
*/
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
			CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
		}
		if (LOWORD(wParam) == IDC_FINDALL)
		{
			CreateThread(NULL, NULL, ScanThread, hDlg, 0, NULL);
		}
		break;
	}
	return (INT_PTR)FALSE;
}
