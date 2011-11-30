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

#pragma once
#ifndef __MPIPTVGUIDS_DEFINED
#define __MPIPTVGUIDS_DEFINED

#define _CRT_SECURE_NO_WARNINGS

#include <streams.h>

#include <vlc/vlc.h>
#pragma comment(lib, "libvlc.lib")

#ifdef REPLACE_MP_BUILTIN_FILTER
// {D3DD4C59-D3A7-4b82-9727-7B9203EB67C0}
DEFINE_GUID(CLSID_MPVlcSource, 
0xd3dd4c59, 0xd3a7, 0x4b82, 0x97, 0x27, 0x7b, 0x92, 0x3, 0xeb, 0x67, 0xc0);
#define g_wszPushSource     L"MediaPortal IPTV Source Filter"
#else

DEFINE_GUID(CLSID_MPVlcSource, 
0x3c0c7451, 0x993f, 0x460a, 0xa7, 0x14, 0x6a, 0x3, 0x4b, 0xde, 0x98, 0xcd);

#define g_wszPushSource     L"MediaPortal VLC Source Filter"
#endif

#define QI(i) (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :


#define EMPTY_STRING ""
#define IPTV_BUFFER_SIZE 128 * 1024 //By default 64KB buffer size



class CMPVlcSourceStream : public CSourceStream
{

protected:

	HANDLE m_hPipe;
	libvlc_instance_t * m_vlc;

	BOOL m_debug;

	int m_exec_wait;

	TCHAR m_filterPath[MAX_PATH];
	TCHAR m_inifile[MAX_PATH];

	TCHAR m_fn[2048];
	TCHAR m_pipename[MAX_PATH];
	TCHAR m_dump_opt[MAX_PATH];

	TCHAR m_input[2048];
	TCHAR m_output[2048];

	PCHAR* m_argv; 
	PCHAR* m_options;

	PCHAR m_exec;
	PCHAR m_exec_opt;

	DWORD m_seqNumber;
	DWORD m_buffsize;

	HRESULT FillBuffer(IMediaSample *pSamp) { return S_OK; };
	HRESULT GetMediaType(__inout CMediaType *pMediaType);
	HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest);
	HRESULT DoBufferProcessingLoop(void);


public:

    CMPVlcSourceStream(HRESULT *phr, CSource *pFilter);
    ~CMPVlcSourceStream();

	bool Load(const TCHAR* fn);
	void Clear();

};

// This class is exported from the mpiptvsource.dll
class CMPVlcSource : public CSource, public IFileSourceFilter
{

private:
    // Constructor is private because you have to use CreateInstance
    CMPVlcSource(IUnknown *pUnk, HRESULT *phr);
    ~CMPVlcSource();

    CMPVlcSourceStream *m_stream;
	TCHAR* m_fn;

public:
	// IFileSourceFilter
	DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
	STDMETHODIMP Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt);
	STDMETHODIMP GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt);
	static CUnknown * WINAPI CreateInstance(IUnknown *pUnk, HRESULT *phr);  
};

#endif
