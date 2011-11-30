/* 
*	Copyright (C) 2006-2009 Team MediaPortal
*	http://www.team-mediaportal.com
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*   
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*   
*  You should have received a copy of the GNU General Public License
*  along with GNU Make; see the file COPYING.  If not, write to
*  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
*  http://www.gnu.org/copyleft/gpl.html
*
*/

// MPVLCsource.cpp : Defines the exported functions for the DLL application.
//
#pragma warning(disable : 4996)


#include "mpvlcsource.h"
#include "cmdlinetoargv.h"
#include <stdio.h>
#include <shlobj.h>

extern HINSTANCE hFilterInstance;
BOOL enableDebug = 0;

#define logging

#ifdef logging
static char logFile[MAX_PATH];
static WORD logFileParsed = -1;

void GetLogFile(char *pLog)
{
  SYSTEMTIME systemTime;
  GetLocalTime(&systemTime);
  if(logFileParsed != systemTime.wDay)
  {
    TCHAR folder[MAX_PATH];
    ::SHGetSpecialFolderPath(NULL,folder,CSIDL_COMMON_APPDATA,FALSE);
    sprintf(logFile,"%s\\Team MediaPortal\\MediaPortal TV Server\\log\\MPVLCSource-%04.4d-%02.2d-%02.2d.Log",folder, systemTime.wYear, systemTime.wMonth, systemTime.wDay);
    logFileParsed=systemTime.wDay; // rec
  }
  strcpy(pLog, &logFile[0]);
}

void LogPrint(const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);

  char buffer[1000];
  int tmp;
  va_start(ap,fmt);
  tmp=vsprintf(buffer, fmt, ap);
  va_end(ap);
  SYSTEMTIME systemTime;
  GetLocalTime(&systemTime);

  TCHAR filename[1024];
  GetLogFile(filename);
  FILE* fp = fopen(filename,"a+");

  if (fp!=NULL)
  {
    fprintf(fp,"%02.2d-%02.2d-%04.4d %02.2d:%02.2d:%02.2d.%03.3d [%x]%s\n",
      systemTime.wDay, systemTime.wMonth, systemTime.wYear,
      systemTime.wHour,systemTime.wMinute,systemTime.wSecond,
      systemTime.wMilliseconds,
      GetCurrentThreadId(),
      buffer);
    fclose(fp);
  }
  char buf[2048];
  sprintf_s(buf, sizeof(buf),"%02.2d-%02.2d-%04.4d %02.2d:%02.2d:%02.2d %s\n",
    systemTime.wDay, systemTime.wMonth, systemTime.wYear,
    systemTime.wHour,systemTime.wMinute,systemTime.wSecond,
    buffer);
  ::OutputDebugString(buf);
};

#define LogDebug if (enableDebug) LogPrint
#define LogError LogPrint
#define LogInfo LogPrint

#else
#define LogDebug //
#endif


DWORD WINAPI VlcStreamDiscardThread(__in  LPVOID lpParameter)
{
	HANDLE hPipe = (HANDLE)lpParameter;
	char tmpBuffer[IPTV_BUFFER_SIZE];
	OVERLAPPED o;
	o.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL);

	LogDebug("StreamDiscardThread start");

	DWORD startRecvTime = GetTickCount();
	do 
    {
		DWORD cbBytesRead;
		ResetEvent(o.hEvent);
		o.Internal = o.InternalHigh = o.Offset = o.OffsetHigh = 0;
		BOOL fSuccess = ReadFile( hPipe, tmpBuffer, sizeof(tmpBuffer), &cbBytesRead, &o);

		if (GetLastError() == ERROR_IO_PENDING)
		{	
			WaitForSingleObject(o.hEvent, 5000);
			fSuccess = GetOverlappedResult(hPipe, &o, &cbBytesRead, false);
		}
			
		LogDebug("StreamDiscardThread bytes read: %d", cbBytesRead);
		if (!fSuccess || cbBytesRead == 0)
		{   
			CancelIo(hPipe);
			LogDebug("StreamDiscardThread eof");
			break;
		} 

	} while (abs((signed long)(GetTickCount() - startRecvTime)) < 10000);

	CloseHandle(o.hEvent);	
	LogDebug("StreamDiscardThread end");
	return 0;
}


CMPVlcSourceStream::CMPVlcSourceStream(HRESULT *phr, CSource *pFilter)
  : CSourceStream(NAME("MediaPortal IPTV Stream"), phr, pFilter, L"Out"),
  m_seqNumber(0),
  m_buffsize(0),
  m_vlc(0),
  m_hPipe(0),
  m_argv(0),
  m_options(0),
  m_exec(0),
  m_exec_opt(0)
{
	GetModuleFileName(hFilterInstance, m_filterPath, sizeof(m_filterPath));
	if (strrchr(m_filterPath, '\\'))
		strrchr(m_filterPath, '\\')[0] = 0;
	sprintf_s(m_inifile,  "%s\\%s", m_filterPath, "mpvlcsource.ini");
	
	char tbuf[20];
	GetPrivateProfileString("main", "debug", "0", tbuf, sizeof(tbuf), m_inifile);
	if (tbuf[0] != '0')
		enableDebug = TRUE;
}

CMPVlcSourceStream::~CMPVlcSourceStream()
{
	Clear();
}

void CMPVlcSourceStream::Clear() 
{
  LogDebug("Clear()");
  if(m_hPipe)
  {
	  CloseHandle(m_hPipe);
	  m_hPipe = NULL;
  }
  if(CAMThread::ThreadExists())
  {
    CAMThread::CallWorker(CMD_EXIT);
    CAMThread::Close();
  }
  if(m_vlc)
  {
	 libvlc_vlm_release(m_vlc);
     libvlc_release (m_vlc);
	 m_vlc = 0;
  }
  if(m_argv)
	  GlobalFree(m_argv);
  if(m_options)
	  CoTaskMemFree(m_options);
  m_exec = m_exec_opt = NULL;
}


HRESULT CMPVlcSourceStream::GetMediaType(__inout CMediaType *pMediaType) 
{
  LogDebug("GetMediaType()");
  pMediaType->majortype = MEDIATYPE_Stream;
  pMediaType->subtype = MEDIASUBTYPE_MPEG2_TRANSPORT;
  return S_OK;
}

HRESULT CMPVlcSourceStream::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest) 
{
  LogDebug("DecideBufferSize()");
  HRESULT hr;
  CAutoLock cAutoLock(m_pFilter->pStateLock());

  CheckPointer(pAlloc, E_POINTER);
  CheckPointer(pRequest, E_POINTER);

  // If the bitmap file was not loaded, just fail here.

  // Ensure a minimum number of buffers
  if (pRequest->cBuffers == 0)
  {
	pRequest->cBuffers = 1;
  }
  pRequest->cbBuffer = IPTV_BUFFER_SIZE;

  ALLOCATOR_PROPERTIES Actual;
  hr = pAlloc->SetProperties(pRequest, &Actual);
  if (FAILED(hr)) 
  {
    return hr;
  }

  // Is this allocator unsuitable?
  if (Actual.cbBuffer < pRequest->cbBuffer) 
  {
    return E_FAIL;
  }

  return S_OK;
}

HRESULT CMPVlcSourceStream::DoBufferProcessingLoop(void) 
{
	Command com;
	HRESULT result = S_OK;
	BOOL bStop = false;

	OnThreadStartPlay();

	LogInfo("Starting grabber thread");

	if (m_exec)
	{
		LogInfo("Executing external command: %s %s", m_exec, m_exec_opt);
		::ShellExecuteA(0, NULL, m_exec, m_exec_opt, NULL, SW_HIDE);
		Sleep(m_exec_wait);
	}

	//libvlc_vlm_seek_media(m_vlc, "vlc_ds_stream", 0);

	if (libvlc_vlm_play_media (m_vlc, "vlc_ds_stream") != 0)
	{
		LogError("libvlc_vlm_play_media failed");
		return S_FALSE;
	}

	OVERLAPPED o;
	o.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL);
	o.Internal = o.InternalHigh = o.Offset = o.OffsetHigh = 0;

	ConnectNamedPipe(m_hPipe, &o);
	
	WaitForSingleObject(o.hEvent, 20000);
	BOOL fConnected = HasOverlappedIoCompleted(&o);

	SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);

	if (!fConnected)
	{
		LogError("ConnectNamedPipe failed");
		CancelIo(m_hPipe);
		CloseHandle(o.hEvent);
		return S_FALSE;	
	}
	else do 
	{
		BOOL requestAvail = FALSE;
		while ((requestAvail = CheckRequest(&com)) == FALSE) 
		{
			//LogDebug ("Command: %d", com);

			IMediaSample *pSample;

			HRESULT hr = GetDeliveryBuffer(&pSample,NULL,NULL,0);
			if (FAILED(hr))
				continue;	

			// fill buffer
			// ------------------------------------------------------------------------------------
			hr = S_OK;
			BYTE *pData;
			DWORD cbData;

			CheckPointer(pSample, E_POINTER);
			// Access the sample's data buffer
			pSample->GetPointer((BYTE **)&pData);
			cbData = pSample->GetSize();

			DWORD startRecvTime = GetTickCount();
			m_buffsize = 0;

			do 
			{
				DWORD cbBytesRead = 0;
				ResetEvent(o.hEvent);
				o.Internal = o.InternalHigh = o.Offset = o.OffsetHigh = 0;
				BOOL fSuccess = ReadFile( m_hPipe,  pData + m_buffsize,  cbData - m_buffsize,  &cbBytesRead, &o);

				if (GetLastError() == ERROR_IO_PENDING)
				{	
					for (int n=0; n < 20; n++) 
					{						
						if ((requestAvail = CheckRequest(&com)) == TRUE)
							break;
						if (WaitForSingleObject(o.hEvent, 1000) == WAIT_OBJECT_0)
							break;
					}
					fSuccess = GetOverlappedResult(m_hPipe, &o, &cbBytesRead, false);
				}
							
				if (!fSuccess || cbBytesRead == 0)
				{   
					CancelIo(m_hPipe);				
					break;
				} 
				m_buffsize += cbBytesRead;

			} while ( !requestAvail && m_buffsize < (cbData  * 3 / 4) 
						&& abs((signed long)(GetTickCount() - startRecvTime)) < 100);
			
			// ------------------------------------------------------------------------------------

			if (m_buffsize != 0 && !(requestAvail && com == CMD_STOP)) 
			{
				LogDebug("Posting %d / %d bytes", m_buffsize, pSample->GetSize());

				REFERENCE_TIME rtStart = startRecvTime;
				REFERENCE_TIME rtStop  = GetTickCount();

				pSample->SetTime(&rtStart, &rtStop);
				pSample->SetActualDataLength(m_buffsize);
				pSample->SetSyncPoint(TRUE);
				
				hr = Deliver(pSample);
				// downstream filter returns S_FALSE if it wants us to
				// stop or an error if it's reporting an error.
				if(hr != S_OK)
				{
					LogInfo("Deliver() returned %08x; stopping", hr);
					bStop = true;
				}

			} else
			{
				// FillBuffer returned false
				bStop = true;
				DeliverEndOfStream();
			}

			pSample->Release();

			if (bStop)
				break;
		}

		if (requestAvail)
		{
			LogInfo("Received command: %d", com);
			if (com == CMD_RUN || com == CMD_PAUSE) {
				Reply(NOERROR);
			} else if (com != CMD_STOP) {
				Reply((DWORD) E_UNEXPECTED);
				LogDebug("Unexpected command %d!!!", com);
			}
		}
	} while (com != CMD_STOP && bStop == false);
    
	//DeliverEndOfStream();
	LogDebug("end loop");

	HANDLE hSDThread = CreateThread(NULL, 0, &VlcStreamDiscardThread, LPVOID(m_hPipe), 0, 0);

	libvlc_vlm_stop_media(m_vlc, "vlc_ds_stream");
	LogDebug("libvlc_vlm_stop_media");

	if (WaitForSingleObject(hSDThread, 30000) == WAIT_TIMEOUT)
	{
		DWORD ec;
		LogError("Terminating StreamDiscardThread!");
		GetExitCodeThread(hSDThread, &ec);
		TerminateThread(hSDThread, ec);
	}
	DisconnectNamedPipe(m_hPipe); 
	LogDebug("DoBufferProcessingLoop end");
	
	CloseHandle(o.hEvent);
	return result;
}

bool CMPVlcSourceStream::Load(const TCHAR* fn) 
{
	char def_options[512];
	char def_sout[512];


	LogDebug("Load()");
	Clear();

	strncpy(m_fn, fn, sizeof(m_fn));

	sprintf(m_pipename, "\\\\.\\pipe\\vlc2ds_%d_%d", GetCurrentThreadId(), GetTickCount());

	LogDebug("Creating named pipe %s", m_pipename);
	m_hPipe = CreateNamedPipe( 
		m_pipename,             // pipe name 
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,       // read/write access 
		PIPE_TYPE_MESSAGE |       // message type pipe 
		PIPE_READMODE_BYTE |   // message-read mode 
		PIPE_WAIT,                // blocking mode 
		PIPE_UNLIMITED_INSTANCES, // max. instances  
		IPTV_BUFFER_SIZE,                  // output buffer size 
		IPTV_BUFFER_SIZE,                  // input buffer size 
		0,                        // client time-out 
		NULL);                    // default security attribute 
	if (!m_hPipe) 
	{
		LogError("CreateNamedPipe failed");	
		return false;
	}
	/* Load the VLC engine */
	m_vlc = libvlc_new (0, NULL);
	if (!m_vlc)
	{
		LogError("libvlc_new failed");
		return false;
	}	


	// parse input MRL and options
	GetPrivateProfileString("main", "options", "", def_options, sizeof(def_options), m_inifile);
	GetPrivateProfileString("main", "sout", "file{mux=ts,dst=\"\\%s\"}", def_sout, sizeof(def_sout), m_inifile);

	if (strlen(def_options) > 0)
	{		
		strcat_s(m_fn, " ");
		strcat_s(m_fn, def_options);
	}

	LogInfo("Adding media: %s", m_fn);

	int argc;
	m_argv = CommandLineToArgvA(m_fn, &argc);	
	m_options = (char**)CoTaskMemAlloc(argc * sizeof(char*));

	int nopt = 0;
	int noremux = -1;
	char *opt_out = 0;
	for (int n = 0; n < argc; n++)
	{
		if (m_argv[n][0] == '-' && m_argv[n][1] == '-')
			m_options[nopt] = m_argv[n] + 2;
		else if (m_argv[n][0] == ':')
			m_options[nopt] = m_argv[n] + 1;
		else
		{
			strncpy(m_input, m_argv[n], sizeof(m_input));
			continue;
		}

		if (strncmp(m_options[nopt], "sout", 4) == 0)			// disable direct ts dump if there are any sout options
			noremux = 0;
		
		if (strncmp(m_options[nopt], "sout=", 5) == 0)
			opt_out = m_options[nopt] + 5;
		else if (strncmp(m_options[nopt], "exec=", 5) == 0)
			m_exec = m_options[nopt] + 5;
		else if (strncmp(m_options[nopt], "exec-opt=", 9) == 0)
			m_exec_opt = m_options[nopt] + 9;
		else if (strncmp(m_options[nopt], "exec-wait=", 10) == 0)
			m_exec_wait = atoi(m_options[nopt] + 10);
		else if (strncmp(m_options[nopt], "no-remux", 8) == 0 && noremux == -1)
			noremux = 1;
		else
			nopt++;
	}


	char t_output[512];	
	if (noremux == 1)
	{
		sprintf_s(m_dump_opt, "ts-dump-file=%s", m_pipename);
		m_options[nopt++] = m_dump_opt;
		strcpy_s(m_output, "#dummy");
	}
	else
	{
		sprintf_s(t_output, def_sout, m_pipename);
		if (opt_out)
			sprintf_s(m_output, "%s:%s", opt_out, t_output);
		else
			sprintf_s(m_output, "#%s", t_output);
	}

	LogDebug("input=%s", m_input);
	LogDebug("output=%s", m_output);
	for (int i = 0; i < nopt; i++)
		LogDebug("options[%d]=%s", i, m_options[i]);

	if (libvlc_vlm_add_broadcast(m_vlc, "vlc_ds_stream", m_input, m_output, nopt, m_options, true, 0) != 0)
	{
		LogError("libvlc_vlm_add_broadcast failed");
		return false;   
	}
	return true;
}



// This is the constructor of a class that has been exported.
// see mpiptvsource.h for the class definition
CMPVlcSource::CMPVlcSource(IUnknown *pUnk, HRESULT *phr)
  : CSource(NAME("MediaPortal IPTV Source"), pUnk, CLSID_MPVlcSource)
{
  // The pin magically adds itself to our pin array.
  m_stream = new CMPVlcSourceStream(phr, this);

  if (phr)
  {
    if (m_stream == NULL)
      *phr = E_OUTOFMEMORY;
    else
      *phr = S_OK;
  } 

  m_fn = EMPTY_STRING;
}


CMPVlcSource::~CMPVlcSource()
{
  delete m_stream;
  if (m_fn != EMPTY_STRING) {
    CoTaskMemFree(m_fn);
    m_fn = EMPTY_STRING;
  }
}

STDMETHODIMP CMPVlcSource::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
  CheckPointer(ppv, E_POINTER);

  return 
    QI(IFileSourceFilter)
    __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CMPVlcSource::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt) 
{
  size_t length = wcstombs(NULL, pszFileName, 0);
  if(!(m_fn = (TCHAR*)CoTaskMemAlloc((length+1)*sizeof(TCHAR))))
    return E_OUTOFMEMORY;
  wcstombs(m_fn, pszFileName, length + 1);
  if(!m_stream->Load(m_fn))
    return E_FAIL;

  return S_OK;
}

STDMETHODIMP CMPVlcSource::GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt)
{
  if(!ppszFileName) return E_POINTER;

  if(!(*ppszFileName = (LPOLESTR)CoTaskMemAlloc((strlen(m_fn)+1)*sizeof(WCHAR))))
    return E_OUTOFMEMORY;

  mbstowcs(*ppszFileName, m_fn, strlen(m_fn) + 1);

  return S_OK;
}

CUnknown * WINAPI CMPVlcSource::CreateInstance(IUnknown *pUnk, HRESULT *phr)
{
  CMPVlcSource *pNewFilter = new CMPVlcSource(pUnk, phr );

  if (phr)
  {
    if (pNewFilter == NULL) 
      *phr = E_OUTOFMEMORY;
    else
      *phr = S_OK;
  }

  return pNewFilter;
}
