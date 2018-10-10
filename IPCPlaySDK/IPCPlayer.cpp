#include "IPCPlayer.hpp"


CIPCPlayer::CIPCPlayer()
{
	ZeroMemory(&m_nZeroOffset, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_nZeroOffset));
	/*
	ʹ��CCriticalSectionProxy�����������ֱ�ӵ���InitializeCriticalSection����
	InitializeCriticalSection(&m_csVideoCache);*/

	// ���ô��룬���첽��Ⱦ��֡�������
	// InitializeCriticalSection(&m_cslistAVFrame);
	/*
	InitializeCriticalSection(&m_csAudioCache);
	InitializeCriticalSection(&m_csParser);
	//InitializeCriticalSection(&m_csBorderRect);
	//InitializeCriticalSection(&m_csListYUV);
	InitializeCriticalSection(&m_csListRenderUnit);
	InitializeCriticalSection(&m_cslistRenderWnd);

	InitializeCriticalSection(&m_csCaptureYUV);
	InitializeCriticalSection(&m_csCaptureYUVEx);
	InitializeCriticalSection(&m_csFilePlayCallBack);
	InitializeCriticalSection(&m_csYUVFilter);
	*/
	m_nMaxFrameSize = 1024 * 256;
	nSize = sizeof(CIPCPlayer);
	m_nAudioPlayFPS = 50;
	m_nSampleFreq = 8000;
	m_nSampleBit = 16;
	m_nProbeStreamTimeout = 10000;	// ����
	m_nMaxYUVCache = 10;
	m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
	m_nDecodeDelay = -1;
	m_nCoordinate = Coordinte_Wnd;
}

__int64 CIPCPlayer::LargerFileSeek(HANDLE hFile, __int64 nOffset64, DWORD MoveMethod)
{
	LARGE_INTEGER Offset;
	Offset.QuadPart = nOffset64;
	Offset.LowPart = SetFilePointer(hFile, Offset.LowPart, &Offset.HighPart, MoveMethod);

	if (Offset.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		Offset.QuadPart = -1;
	}
	return Offset.QuadPart;
}

bool CIPCPlayer::IsIPCVideoFrame(IPCFrameHeader *pFrameHeader, bool &bIFrame, int nSDKVersion)
{
	bIFrame = false;
	if (nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
	{
		switch (pFrameHeader->nType)
		{
		case FRAME_P:		// BP֡������࣬����ǰ�ã��Լ��ٱȽϴ���
		case FRAME_B:
			return true;
		case 0:
		case FRAME_IDR:
		case FRAME_I:
			bIFrame = true;
			return true;
		default:
			return false;
		}
	}
	else
	{
		switch (pFrameHeader->nType)
		{// �ɰ�SDK�У�0Ϊbbp֡ ,1ΪI֡ ,2Ϊ��Ƶ֡
		case 0:
			return true;
			break;
		case 1:
			bIFrame = true;
			return true;
			break;
		default:
			return false;
			break;
		}
	}
}

int CIPCPlayer::GetFrame(IPCFrameHeader *pFrame, bool bFirstFrame)
{
	if (!m_hVideoFile)
		return IPC_Error_FileNotOpened;
	LARGE_INTEGER liFileSize;
	if (!GetFileSizeEx(m_hVideoFile, &liFileSize))
		return GetLastError();
	byte *pBuffer = _New byte[m_nMaxFrameSize];
	shared_ptr<byte>TempBuffPtr(pBuffer);
	DWORD nMoveMothod = FILE_BEGIN;
	__int64 nMoveOffset = sizeof(IPC_MEDIAINFO);

	if (!bFirstFrame)
	{
		nMoveMothod = FILE_END;
		nMoveOffset = -m_nMaxFrameSize;
	}

	if (liFileSize.QuadPart >= m_nMaxFrameSize &&
		LargerFileSeek(m_hVideoFile, nMoveOffset, nMoveMothod) == INVALID_SET_FILE_POINTER)
		return GetLastError();
	DWORD nBytesRead = 0;
	DWORD nDataLength = m_nMaxFrameSize;

	if (!ReadFile(m_hVideoFile, pBuffer, nDataLength, &nBytesRead, nullptr))
	{
		OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
		return GetLastError();
	}
	nDataLength = nBytesRead;
	char *szKey1 = "MOVD";
	char *szKey2 = "IMWH";
	int nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
	if (nOffset < 0)
	{
		nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
		if (nOffset < 0)
			return IPC_Error_MaxFrameSizeNotEnough;
	}
	nOffset -= offsetof(IPCFrameHeader, nFrameTag);	// ���˵�֡ͷ
	if (nOffset < 0)
		return IPC_Error_NotVideoFile;
	// �����������m_nMaxFrameSize�����ڵ�����֡
	pBuffer += nOffset;
	nDataLength -= nOffset;
	bool bFoundVideo = false;

	FrameParser Parser;
#ifdef _DEBUG
	int nAudioFrames = 0;
	int nVideoFrames = 0;
	while (ParserFrame(&pBuffer, nDataLength, &Parser))
	{
		switch (Parser.pHeader->nType)
		{
		case 0:
		case 1:
		{
			nVideoFrames++;
			bFoundVideo = true;
			break;
		}
		case 2:      // G711 A�ɱ���֡
		case FRAME_G711U:      // G711 U�ɱ���֡
		{
			nAudioFrames++;
			break;
		}
		default:
		{
			assert(false);
			break;
		}
		}
		if (bFoundVideo && bFirstFrame)
			break;

		nOffset += Parser.nFrameSize;
	}
	OutputMsg("%s In Last %d bytes:VideoFrames:%d\tAudioFrames:%d.\n", __FUNCTION__, m_nMaxFrameSize, nVideoFrames, nAudioFrames);
#else
	while (ParserFrame(&pBuffer, nDataLength, &Parser))
	{
		if (Parser.pHeaderEx->nType == 0 ||
			Parser.pHeaderEx->nType == 1)
		{
			bFoundVideo = true;
		}
		if (bFoundVideo && bFirstFrame)
		{
			break;
		}
		nOffset += Parser.nFrameSize;
	}
#endif
	if (SetFilePointer(m_hVideoFile, sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return GetLastError();
	if (bFoundVideo)
	{
		memcpy_s(pFrame, sizeof(IPCFrameHeader), Parser.pHeader, sizeof(IPCFrameHeader));
		return IPC_Succeed;
	}
	else
		return IPC_Error_MaxFrameSizeNotEnough;
}

int CIPCPlayer::GetLastFrameID(int &nLastFrameID)
{
	if (!m_hVideoFile)
		return IPC_Error_FileNotOpened;
	LARGE_INTEGER liFileSize;
	if (!GetFileSizeEx(m_hVideoFile, &liFileSize))
		return GetLastError();
	byte *pBuffer = _New byte[m_nMaxFrameSize];
	shared_ptr<byte>TempBuffPtr(pBuffer);

	if (liFileSize.QuadPart >= m_nMaxFrameSize &&
		LargerFileSeek(m_hVideoFile, -m_nMaxFrameSize, FILE_END) == INVALID_SET_FILE_POINTER)
		return GetLastError();
	DWORD nBytesRead = 0;
	DWORD nDataLength = m_nMaxFrameSize;

	if (!ReadFile(m_hVideoFile, pBuffer, nDataLength, &nBytesRead, nullptr))
	{
		OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
		return GetLastError();
	}
	nDataLength = nBytesRead;
	char *szKey1 = "MOVD";		// �°��IPC¼���ļ�ͷ
	char *szKey2 = "IMWH";		// �ϰ汾��IPC¼���ļ���ʹ���˸��ӽݵ��ļ�ͷ
	int nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
	if (nOffset < 0)
	{
		nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
		if (nOffset < 0)
			return IPC_Error_MaxFrameSizeNotEnough;
		else if (nOffset < offsetof(IPCFrameHeader, nFrameTag))
		{
			pBuffer += (nOffset + 4);
			nDataLength -= (nOffset + 4);
			nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey2, 4);
		}
	}
	else if (nOffset < offsetof(IPCFrameHeader, nFrameTag))
	{
		pBuffer += (nOffset + 4);
		nDataLength -= (nOffset + 4);
		nOffset = KMP_StrFind(pBuffer, nDataLength, (byte *)szKey1, 4);
	}
	nOffset -= offsetof(IPCFrameHeader, nFrameTag);	// ���˵�֡ͷ
	if (nOffset < 0)
		return IPC_Error_NotVideoFile;
	// �����������m_nMaxFrameSize�����ڵ�����֡
	pBuffer += nOffset;
	nDataLength -= nOffset;
	bool bFoundVideo = false;

	FrameParser Parser;
#ifdef _DEBUG
	int nAudioFrames = 0;
	int nVideoFrames = 0;
	while (ParserFrame(&pBuffer, nDataLength, &Parser))
	{
		switch (Parser.pHeaderEx->nType)
		{
		case 0:
		case FRAME_B:
		case FRAME_I:
		case FRAME_IDR:
		case FRAME_P:
		{
			nVideoFrames++;
			nLastFrameID = Parser.pHeaderEx->nFrameID;
			bFoundVideo = true;
			break;
		}
		case FRAME_G711A:      // G711 A�ɱ���֡
		case FRAME_G711U:      // G711 U�ɱ���֡
		case FRAME_G726:       // G726����֡
		case FRAME_AAC:        // AAC����֡��
		{
			nAudioFrames++;
			break;
		}
		default:
		{
			assert(false);
			break;
		}
		}

		nOffset += Parser.nFrameSize;
	}
	OutputMsg("%s In Last %d bytes:VideoFrames:%d\tAudioFrames:%d.\n", __FUNCTION__, m_nMaxFrameSize, nVideoFrames, nAudioFrames);
#else
	while (ParserFrame(&pBuffer, nDataLength, &Parser))
	{
		if (Parser.pHeaderEx->nType == FRAME_B ||
			Parser.pHeaderEx->nType == FRAME_I ||
			Parser.pHeaderEx->nType == FRAME_P)
		{
			nLastFrameID = Parser.pHeaderEx->nFrameID;
			bFoundVideo = true;
		}
		nOffset += Parser.nFrameSize;
	}
#endif
	if (SetFilePointer(m_hVideoFile, sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return GetLastError();
	if (bFoundVideo)
		return IPC_Succeed;
	else
		return IPC_Error_MaxFrameSizeNotEnough;
}

bool CIPCPlayer::InitizlizeDx(AVFrame *pAvFrame )
{
	if (!m_hRenderWnd)
		return true;
	//		���ܴ���ֻ���벻��ʾͼ������
	// 		if (!m_hRenderWnd)
	// 			return false;
	// ��ʼ��ʾ���
	//if (GetOsMajorVersion() < Win7MajorVersion)
	if (m_bEnableDDraw)
	{
		m_pDDraw = _New CDirectDraw();
		if (m_pDDraw)
		{
			if (m_bEnableHaccel)
			{
				DDSURFACEDESC2 ddsd = { 0 };
				FormatNV12::Build(ddsd, m_nVideoWidth, m_nVideoHeight);
				m_cslistRenderWnd.Lock();
				m_pDDraw->Create<FormatNV12>(m_hRenderWnd, ddsd);
				m_cslistRenderWnd.Unlock();
				m_pYUVImage = make_shared<ImageSpace>();
				m_pYUVImage->dwLineSize[0] = m_nVideoWidth;
				m_pYUVImage->dwLineSize[1] = m_nVideoWidth >> 1;
			}
			else
			{
				//����DirectDraw����  
				DDSURFACEDESC2 ddsd = { 0 };
				FormatYV12::Build(ddsd, m_nVideoWidth, m_nVideoHeight);
				m_cslistRenderWnd.Lock();
				m_pDDraw->Create<FormatYV12>(m_hRenderWnd, ddsd);
				m_cslistRenderWnd.Unlock();
				m_pYUVImage = make_shared<ImageSpace>();
				m_pYUVImage->dwLineSize[0] = m_nVideoWidth;
				m_pYUVImage->dwLineSize[1] = m_nVideoWidth >> 1;
				m_pYUVImage->dwLineSize[2] = m_nVideoWidth >> 1;
				m_pDDraw->SetExternDraw(m_pDCCallBack, m_pDCCallBackParam);
			}
			Autolock(&m_csListRenderUnit);
			for (auto it = m_listRenderUnit.begin(); it != m_listRenderUnit.end(); it++)
				(*it)->ReInitialize(m_nVideoWidth, m_nVideoHeight);
		}
		return true;
	}
	else
	{
		if (!m_pDxSurface)
			m_pDxSurface = _New CDxSurfaceEx;

		// ʹ���߳���CDxSurface������ʾͼ��
		if (m_pDxSurface)		// D3D�豸��δ��ʼ��
		{
			//m_pSimpleWnd = make_shared<CSimpleWnd>(m_nVideoWidth, m_nVideoHeight);
			DxSurfaceInitInfo &InitInfo = m_DxInitInfo;
			InitInfo.nSize = sizeof(DxSurfaceInitInfo);
			if (m_bEnableHaccel)
			{
				m_pDxSurface->SetD3DShared(m_bD3dShared);
				AVCodecID nCodecID = AV_CODEC_ID_H264;
				if (m_nVideoCodec == CODEC_H265)
					nCodecID = AV_CODEC_ID_HEVC;
				InitInfo.nFrameWidth = CVideoDecoder::GetAlignedDimension(nCodecID, m_nVideoWidth);
				InitInfo.nFrameHeight = CVideoDecoder::GetAlignedDimension(nCodecID, m_nVideoHeight);
				InitInfo.nD3DFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
			}
			else
			{
				///if (GetOsMajorVersion() < 6)
				///	InitInfo.nD3DFormat = D3DFMT_A8R8G8B8;		// ��XP�����£�ĳЩ������ʾ����ʾYV12����ʱ���ᵼ��CPUռ���ʻ����������������D3D9��ü�����ʾ��һ��BUG
				InitInfo.nFrameWidth = m_nVideoWidth;
				InitInfo.nFrameHeight = m_nVideoHeight;
				InitInfo.nD3DFormat = m_nPixelFormat;//(D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
			}

			InitInfo.bWindowed = TRUE;
			// 				if (!m_pWndDxInit->GetSafeHwnd())
			// 					InitInfo.hPresentWnd = m_hRenderWnd;
			// 				else
			InitInfo.hPresentWnd = m_pWndDxInit->GetSafeHwnd();
			//InitInfo.hPresentWnd = m_hRenderWnd;

			if (m_nRocateAngle == Rocate90 ||
				m_nRocateAngle == Rocate270 ||
				m_nRocateAngle == RocateN90)
				swap(InitInfo.nFrameWidth, InitInfo.nFrameHeight);
			// ׼�����ر���ͼƬ
			if (!m_pszBackImagePath)
			{

				WCHAR szImagePath_jpeg[1024] = { 0 };
				WCHAR szImagePath_png[1024] = { 0 };
				WCHAR szImagePath_bmp[1024] = { 0 };
				WCHAR szTempPath[1024] = { 0 };
				GetModuleFileNameW(g_hDllModule, szTempPath, 1024);
				int nPos = WcsReserverFind(szTempPath, L'\\');

				wcsncpy_s(szImagePath_jpeg, 1024, szTempPath, nPos);
				wcsncpy_s(szImagePath_png, 1024, szTempPath, nPos);
				wcsncpy_s(szImagePath_bmp, 1024, szTempPath, nPos);

				wcscat_s(szImagePath_jpeg, 1024, L"\\BackImage.jpg");
				wcscat_s(szImagePath_png, 1024, L"\\BackImage.png");
				wcscat_s(szImagePath_bmp, 1024, L"\\BackImage.bmp");

				if (PathFileExistsW(szImagePath_jpeg))
					m_pDxSurface->SetBackgroundPictureFile(szImagePath_jpeg, m_hRenderWnd);
				else if (PathFileExistsW(szImagePath_png))
					m_pDxSurface->SetBackgroundPictureFile(szImagePath_png, m_hRenderWnd);
				else if (PathFileExistsW(szImagePath_bmp))
					m_pDxSurface->SetBackgroundPictureFile(szImagePath_bmp, m_hRenderWnd);
			}
			else if (PathFileExistsW(m_pszBackImagePath))
				m_pDxSurface->SetBackgroundPictureFile(m_pszBackImagePath, m_hRenderWnd);

			m_pDxSurface->DisableVsync();		// ���ô�ֱͬ��������֡���п��ܳ�����ʾ����ˢ���ʣ��Ӷ��ﵽ���ٲ��ŵ�Ŀ��
			if (!m_pDxSurface->InitD3D(InitInfo.hPresentWnd,
				InitInfo.nFrameWidth,
				InitInfo.nFrameHeight,
				InitInfo.bWindowed,
				InitInfo.nD3DFormat))
			{
				OutputMsg("%s Initialize DxSurface failed.\n", __FUNCTION__);
#ifdef _DEBUG
				OutputMsg("%s \tObject:%d DxSurface failed,Line %d Time = %d.\n", __FUNCTION__, m_nObjIndex, __LINE__, timeGetTime() - m_nLifeTime);
#endif
				return false;
			}
			m_pDxSurface->SetCoordinateMode(m_nCoordinate);
			m_pDxSurface->SetExternDraw(m_pDCCallBack, m_pDCCallBackParam);
			return true;
		}
		else
			return false;
	}
}

CIPCPlayer::CIPCPlayer(HWND hWnd, CHAR *szFileName , char *szLogFile)
{
	ZeroMemory(&m_nZeroOffset, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_nZeroOffset));
#ifdef _DEBUG
	m_pCSGlobalCount->Lock();
	m_nObjIndex = m_nGloabalCount++;
	m_pCSGlobalCount->Unlock();
	m_nLifeTime = timeGetTime();

	// 		m_OuputTime.nDecode = m_nLifeTime;
	// 		m_OuputTime.nInputStream = m_nLifeTime;
	// 		m_OuputTime.nRender = m_nLifeTime;

	OutputMsg("%s \tObject:%d m_nLifeTime = %d.\n", __FUNCTION__, m_nObjIndex, m_nLifeTime);
#endif 
	m_nSDKVersion = IPC_IPC_SDK_VERSION_2015_12_16;
	if (szLogFile)
	{
		strcpy(m_szLogFileName, szLogFile);
		m_pRunlog = make_shared<CRunlogA>(szLogFile);
	}
	m_hEvnetYUVReady = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_hEventDecodeStart = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_hEventYUVRequire = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	m_hEventFrameCopied = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	/*
	ʹ��CCriticalSectionProxy�����������ֱ�ӵ���InitializeCriticalSection����
	InitializeCriticalSection(&m_csVideoCache); */

	m_csDsoundEnum->Lock();
	if (!m_pDsoundEnum)
		m_pDsoundEnum = make_shared<CDSoundEnum>();	///< ��Ƶ�豸ö����
	m_csDsoundEnum->Unlock();
	m_nAudioPlayFPS = 50;
	m_nSampleFreq = 8000;
	m_nSampleBit = 16;
	m_nProbeStreamTimeout = 10000;	// ����
	m_nMaxYUVCache = 10;
	m_nCoordinate = Coordinte_Wnd;
#ifdef _DEBUG
	OutputMsg("%s Alloc a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
	nSize = sizeof(CIPCPlayer);
	m_nMaxFrameSize = 1024 * 256;
	m_nVideoFPS = 25;				// FPS��Ĭ��ֵΪ25
	m_fPlayRate = 1;
	m_fPlayInterval = 40.0f;
	//m_nVideoCodec	 = CODEC_H264;		// ��ƵĬ��ʹ��H.264����
	m_nVideoCodec = CODEC_UNKNOWN;
	m_nAudioCodec = CODEC_G711U;		// ��ƵĬ��ʹ��G711U����
	//#ifdef _DEBUG
	//		m_nMaxFrameCache = 200;				// Ĭ�������Ƶ��������Ϊ200
	// #else
	m_nMaxFrameCache = 100;				// Ĭ�������Ƶ��������Ϊ100
	m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');

	m_hRenderWnd = hWnd;
	// #endif
	if (szFileName)
	{
		m_pszFileName = _New char[MAX_PATH];
		strcpy(m_pszFileName, szFileName);
		// ���ļ�
		m_hVideoFile = CreateFileA(m_pszFileName,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_ARCHIVE,
			NULL);
		if (m_hVideoFile != INVALID_HANDLE_VALUE)
		{
			int nError = GetFileHeader();
			if (nError != IPC_Succeed)
			{
				OutputMsg("%s %d(%s):Not a IPC Media File.\n", __FILE__, __LINE__, __FUNCTION__);
				ClearOnException();
				throw std::exception("Not a IPC Media File.");
			}
			// GetLastFrameIDȡ�õ������һ֡��ID����֡����Ҫ�ڴ˻�����+1
			if (m_pMediaHeader)
			{
				m_nSDKVersion = m_pMediaHeader->nSDKversion;
				switch (m_nSDKVersion)
				{
				case IPC_IPC_SDK_VERSION_2015_09_07:
				case IPC_IPC_SDK_VERSION_2015_10_20:
				case IPC_IPC_SDK_GSJ_HEADER:
				{
					m_nVideoFPS = 25;
					m_nVideoCodec = CODEC_UNKNOWN;
					m_nVideoWidth = 0;
					m_nVideoHeight = 0;
					// ȡ�õ�һ֡�����һ֡����Ϣ
					if (GetFrame(&m_FirstFrame, true) != IPC_Succeed ||
						GetFrame(&m_LastFrame, false) != IPC_Succeed)
					{
						OutputMsg("%s %d(%s):Can't get the First or Last.\n", __FILE__, __LINE__, __FUNCTION__);
						ClearOnException();
						throw std::exception("Can't get the First or Last.");
					}
					// ȡ���ļ���ʱ��(ms)
					__int64 nTotalTime = 0;
					__int64 nTotalTime2 = 0;
					if (m_pMediaHeader->nCameraType == 1)	// ��Ѷʿ���
					{
						nTotalTime = (m_LastFrame.nFrameUTCTime - m_FirstFrame.nFrameUTCTime) * 100;
						nTotalTime2 = (m_LastFrame.nTimestamp - m_FirstFrame.nTimestamp) / 10000;
					}
					else
					{
						nTotalTime = (m_LastFrame.nFrameUTCTime - m_FirstFrame.nFrameUTCTime) * 1000;
						nTotalTime2 = (m_LastFrame.nTimestamp - m_FirstFrame.nTimestamp) / 1000;
					}
					if (nTotalTime < 0)
					{
						OutputMsg("%s %d(%s):The Frame timestamp is invalid.\n", __FILE__, __LINE__, __FUNCTION__);
						ClearOnException();
						throw std::exception("The Frame timestamp is invalid.");
					}
					if (nTotalTime2 > 0)
					{
						m_nTotalTime = nTotalTime2;
						// ������ʱ��Ԥ����֡��
						m_nTotalFrames = m_nTotalTime / 40;		// �ϰ��ļ�ʹ�ù̶�֡��,ÿ֡���Ϊ40ms
						m_nTotalFrames += 25;
					}
					else if (nTotalTime > 0)
					{
						m_nTotalTime = nTotalTime;
						// ������ʱ��Ԥ����֡��
						m_nTotalFrames = m_nTotalTime / 40;		// �ϰ��ļ�ʹ�ù̶�֡��,ÿ֡���Ϊ40ms
						m_nTotalFrames += 50;
					}
					else
					{
						OutputMsg("%s %d(%s):Frame timestamp error.\n", __FILE__, __LINE__, __FUNCTION__);
						ClearOnException();
						throw std::exception("Frame timestamp error.");
					}
					break;
				}

				case IPC_IPC_SDK_VERSION_2015_12_16:
				{
					int nError = GetLastFrameID(m_nTotalFrames);
					if (nError != IPC_Succeed)
					{
						OutputMsg("%s %d(%s):Can't get last FrameID .\n", __FILE__, __LINE__, __FUNCTION__);
						ClearOnException();
						throw std::exception("Can't get last FrameID.");
					}
					m_nTotalFrames++;
					m_nVideoCodec = m_pMediaHeader->nVideoCodec;
					m_nAudioCodec = m_pMediaHeader->nAudioCodec;
					if (m_nVideoCodec == CODEC_UNKNOWN)
					{
						m_nVideoWidth = 0;
						m_nVideoHeight = 0;
					}
					else
					{
						m_nVideoWidth = m_pMediaHeader->nVideoWidth;
						m_nVideoHeight = m_pMediaHeader->nVideoHeight;
						if (!m_nVideoWidth || !m_nVideoHeight)
						{
							// 								OutputMsg("%s %d(%s):Invalid Mdeia File Header.\n", __FILE__, __LINE__, __FUNCTION__);
							// 								ClearOnException();
							// 								throw std::exception("Invalid Mdeia File Header.");
							m_nVideoCodec = CODEC_UNKNOWN;
						}
					}
					if (m_pMediaHeader->nFps == 0)
						m_nVideoFPS = 25;
					else
						m_nVideoFPS = m_pMediaHeader->nFps;
				}
				break;
				default:
				{
					OutputMsg("%s %d(%s):Invalid SDK Version.\n", __FILE__, __LINE__, __FUNCTION__);
					ClearOnException();
					throw std::exception("Invalid SDK Version.");
				}
				}
				m_nFileFrameInterval = 1000 / m_nVideoFPS;
			}
			// 				m_hCacheFulled = CreateEvent(nullptr, true, false, nullptr);
			// 				m_bThreadSummaryRun = true;
			// 				m_hThreadGetFileSummary = (HANDLE)_beginthreadex(nullptr, 0, ThreadGetFileSummary, this, 0, 0);
			// 				if (!m_hThreadGetFileSummary)
			// 				{
			// 					OutputMsg("%s %d(%s):_beginthreadex ThreadGetFileSummary Failed.\n", __FILE__, __LINE__, __FUNCTION__);
			// 					ClearOnException();
			// 					throw std::exception("_beginthreadex ThreadGetFileSummary Failed.");
			// 				}
			m_nParserBufferSize = m_nMaxFrameSize * 4;
			m_pParserBuffer = (byte *)_aligned_malloc(m_nParserBufferSize, 16);
		}
		else
		{
			OutputMsg("%s %d(%s):Open file failed.\n", __FILE__, __LINE__, __FUNCTION__);
			ClearOnException();
			throw std::exception("Open file failed.");
		}
	}

}

CIPCPlayer::CIPCPlayer(HWND hWnd, int nBufferSize, char *szLogFile )
{
	ZeroMemory(&m_nZeroOffset, sizeof(CIPCPlayer) - offsetof(CIPCPlayer, m_nZeroOffset));
#ifdef _DEBUG
	m_pCSGlobalCount->Lock();
	m_nObjIndex = m_nGloabalCount++;
	m_pCSGlobalCount->Unlock();
	m_nLifeTime = timeGetTime();

	OutputMsg("%s \tObject:%d m_nLifeTime = %d.\n", __FUNCTION__, m_nObjIndex, m_nLifeTime);
#endif 
	m_nSDKVersion = IPC_IPC_SDK_VERSION_2015_12_16;
	if (szLogFile)
	{
		strcpy(m_szLogFileName, szLogFile);
		m_pRunlog = make_shared<CRunlogA>(szLogFile);
	}
	m_hEvnetYUVReady = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_hEventDecodeStart = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_hEventYUVRequire = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	m_hEventFrameCopied = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	// ʹ��CCriticalSectionProxy�����������ֱ�ӵ���InitializeCriticalSection����

	m_csDsoundEnum->Lock();
	if (!m_pDsoundEnum)
		m_pDsoundEnum = make_shared<CDSoundEnum>();	///< ��Ƶ�豸ö����
	m_csDsoundEnum->Unlock();
	m_nAudioPlayFPS = 50;
	m_nSampleFreq = 8000;
	m_nSampleBit = 16;
	m_nProbeStreamTimeout = 10000;	// ����
	m_nMaxYUVCache = 10;
#ifdef _DEBUG
	OutputMsg("%s Alloc a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
	nSize = sizeof(CIPCPlayer);
	m_nMaxFrameSize = 1024 * 256;
	m_nVideoFPS = 25;				// FPS��Ĭ��ֵΪ25
	m_fPlayRate = 1;
	m_fPlayInterval = 40.0f;
	//m_nVideoCodec	 = CODEC_H264;		// ��ƵĬ��ʹ��H.264����
	m_nVideoCodec = CODEC_UNKNOWN;
	m_nAudioCodec = CODEC_G711U;		// ��ƵĬ��ʹ��G711U����
	//#ifdef _DEBUG
	//		m_nMaxFrameCache = 200;				// Ĭ�������Ƶ��������Ϊ200
	// #else
	m_nMaxFrameCache = 100;				// Ĭ�������Ƶ��������Ϊ100
	m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');

	AddRenderWindow(hWnd, nullptr);
	m_hRenderWnd = hWnd;
	m_nDecodeDelay = -1;
	// #endif

}

CIPCPlayer::~CIPCPlayer()
{
#ifdef _DEBUG
	OutputMsg("%s \tReady to Free a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
#endif
	//StopPlay(0);
	/*
	if (m_hWnd)
	{
	if (m_bRefreshWnd)
	{
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = ::BeginPaint(m_hWnd, &ps);
	HBRUSH hBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
	::SetBkColor(hdc, RGB(0, 0, 0));
	RECT rtWnd;
	GetWindowRect(m_hWnd, &rtWnd);
	::ScreenToClient(m_hWnd, (LPPOINT)&rtWnd);
	::ScreenToClient(m_hWnd, ((LPPOINT)&rtWnd) + 1);
	if (GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)
	swap(rtWnd.left, rtWnd.right);

	::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rtWnd, NULL, 0, NULL);
	::EndPaint(m_hWnd, &ps);
	}
	}
	*/
	if (m_pParserBuffer)
	{
		//delete[]m_pParserBuffer;
		_aligned_free(m_pParserBuffer);
#ifdef _DEBUG
		m_pParserBuffer = nullptr;
#endif
	}
	if (m_pDsBuffer)
	{
		m_pDsPlayer->DestroyDsoundBuffer(m_pDsBuffer);
#ifdef _DEBUG
		m_pDsBuffer = nullptr;
#endif
	}
	if (m_pDecoder)
		m_pDecoder.reset();

	if (m_pRocateImage)
	{
		_aligned_free(m_pRocateImage);
		m_pRocateImage = nullptr;
	}
	if (m_pRacoateFrame)
	{
		av_free(m_pRacoateFrame);
		m_pRacoateFrame = nullptr;
	}
	m_listVideoCache.clear();
	if (m_pszFileName)
		delete[]m_pszFileName;
	if (m_hVideoFile)
		CloseHandle(m_hVideoFile);

	if (m_hEvnetYUVReady)
		CloseHandle(m_hEvnetYUVReady);
	if (m_hEventDecodeStart)
		CloseHandle(m_hEventDecodeStart);

	if (m_hEventYUVRequire)
		CloseHandle(m_hEventYUVRequire);
	if (m_hEventFrameCopied)
		CloseHandle(m_hEventFrameCopied);

	if (m_hRenderAsyncEvent && !m_pSyncPlayer)
	{
		CloseHandle(m_hRenderAsyncEvent);
		m_hRenderAsyncEvent = nullptr;
	}

	/*
	ʹ��CCriticalSectionProxy�����������ֱ�ӵ���DeleteCriticalSection����
	DeleteCriticalSection(&m_csVideoCache);*/

	if (m_pszBackImagePath)
	{
		delete[]m_pszBackImagePath;
	}
#ifdef _DEBUG
	OutputMsg("%s \tFinish Free a \tObject:%d.\n", __FUNCTION__, m_nObjIndex);
	OutputMsg("%s \tObject:%d Exist Time = %u(ms).\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
}

int CIPCPlayer::AddRenderWindow(HWND hRenderWnd, LPRECT pRtRender, bool bPercent )
{
	if (!hRenderWnd)
		return IPC_Error_InvalidParameters;
	// 		if (hRenderWnd == m_hRenderWnd)
	// 			return IPC_Succeed;
	Autolock(&m_cslistRenderWnd);
	if (m_bEnableDDraw)
	{
		if (m_listRenderUnit.size() >= 4)
			return IPC_Error_RenderWndOverflow;
		auto itFind = find_if(m_listRenderUnit.begin(), m_listRenderUnit.end(), UnitFinder(hRenderWnd));
		if (itFind != m_listRenderUnit.end())
			return IPC_Succeed;

		m_listRenderUnit.push_back(make_shared<RenderUnit>(hRenderWnd, pRtRender, m_bEnableHaccel));
	}
	else
	{
		if (m_listRenderWnd.size() >= 4)
			return IPC_Error_RenderWndOverflow;
		auto itFind = find_if(m_listRenderWnd.begin(), m_listRenderWnd.end(), WndFinder(hRenderWnd));
		if (itFind != m_listRenderWnd.end())
			return IPC_Succeed;

		m_listRenderWnd.push_back(make_shared<RenderWnd>(hRenderWnd, pRtRender, bPercent));
	}

	return IPC_Succeed;
}

int CIPCPlayer::RemoveRenderWindow(HWND hRenderWnd)
{
	if (!hRenderWnd)
		return IPC_Error_InvalidParameters;

	Autolock(&m_cslistRenderWnd);
	if (m_listRenderWnd.size() < 1)
		return IPC_Succeed;
	if (m_bEnableDDraw)
	{
		auto itFind = find_if(m_listRenderUnit.begin(), m_listRenderUnit.end(), UnitFinder(hRenderWnd));
		if (itFind != m_listRenderUnit.end())
		{
			m_listRenderUnit.erase(itFind);
			InvalidateRect(hRenderWnd, nullptr, true);
		}
		if (hRenderWnd == m_hRenderWnd)
		{
			if (m_listRenderUnit.size() > 0)
				m_hRenderWnd = m_listRenderUnit.front()->hRenderWnd;
			else
				m_hRenderWnd = nullptr;
			return IPC_Succeed;
		}
	}
	else
	{
		auto itFind = find_if(m_listRenderWnd.begin(), m_listRenderWnd.end(), WndFinder(hRenderWnd));
		if (itFind != m_listRenderWnd.end())
		{
			m_listRenderWnd.erase(itFind);
			InvalidateRect(hRenderWnd, nullptr, true);
		}
		if (hRenderWnd == m_hRenderWnd)
		{
			if (m_listRenderWnd.size() > 0)
				m_hRenderWnd = m_listRenderWnd.front()->hRenderWnd;
			else
				m_hRenderWnd = nullptr;
			return IPC_Succeed;
		}
	}


	return IPC_Succeed;
}

// ��ȡ��ʾͼ�񴰿ڵ�����
int CIPCPlayer::GetRenderWindows(HWND* hWndArray, int &nSize)
{
	if (!hWndArray || !nSize)
		return IPC_Error_InvalidParameters;
	Autolock(&m_cslistRenderWnd);
	if (nSize < m_listRenderWnd.size())
		return IPC_Error_BufferOverflow;
	else
	{
		int nRetSize = 0;
		for (auto it = m_listRenderWnd.begin(); it != m_listRenderWnd.end(); it++)
			hWndArray[nRetSize++] = (*it)->hRenderWnd;
		nSize = nRetSize;
		return IPC_Succeed;
	}
}

// ����������ͷ
// ��������ʱ������֮ǰ��������������ͷ
int CIPCPlayer::SetStreamHeader(CHAR *szStreamHeader, int nHeaderSize)
{
	if (nHeaderSize != sizeof(IPC_MEDIAINFO))
		return IPC_Error_InvalidParameters;
	IPC_MEDIAINFO *pMediaHeader = (IPC_MEDIAINFO *)szStreamHeader;
	if (pMediaHeader->nMediaTag != IPC_TAG)
		return IPC_Error_InvalidParameters;
	m_pMediaHeader = make_shared<IPC_MEDIAINFO>();
	if (m_pMediaHeader)
	{
		memcpy_s(m_pMediaHeader.get(), sizeof(IPC_MEDIAINFO), szStreamHeader, sizeof(IPC_MEDIAINFO));
		m_nSDKVersion = m_pMediaHeader->nSDKversion;
		switch (m_nSDKVersion)
		{
		case IPC_IPC_SDK_VERSION_2015_09_07:
		case IPC_IPC_SDK_VERSION_2015_10_20:
		case IPC_IPC_SDK_GSJ_HEADER:
		{
			m_nVideoFPS = 25;
			m_nVideoCodec = CODEC_UNKNOWN;
			m_nVideoWidth = 0;
			m_nVideoHeight = 0;
			break;
		}
		case IPC_IPC_SDK_VERSION_2015_12_16:
		{
			m_nVideoCodec = m_pMediaHeader->nVideoCodec;
			m_nAudioCodec = m_pMediaHeader->nAudioCodec;
			if (m_nVideoCodec == CODEC_UNKNOWN)
			{
				m_nVideoWidth = 0;
				m_nVideoHeight = 0;
			}
			else
			{
				m_nVideoWidth = m_pMediaHeader->nVideoWidth;
				m_nVideoHeight = m_pMediaHeader->nVideoHeight;
				if (!m_nVideoWidth || !m_nVideoHeight)
					//return IPC_Error_MediaFileHeaderError;
					m_nVideoCodec = CODEC_UNKNOWN;
			}
			if (m_pMediaHeader->nFps == 0)
				m_nVideoFPS = 25;
			else
				m_nVideoFPS = m_pMediaHeader->nFps;
			m_nPlayFrameInterval = 1000 / m_nVideoFPS;
		}
		break;
		default:
			return IPC_Error_InvalidSDKVersion;
		}
		m_nFileFrameInterval = 1000 / m_nVideoFPS;
		return IPC_Succeed;
	}
	else
		return IPC_Error_InsufficentMemory;
}

int CIPCPlayer::SetBorderRect(HWND hWnd, LPRECT pRectBorder, bool bPercent )
{
	RECT rtVideo = { 0 };
	// 		rtVideo.left = rtBorder.left;
	// 		rtVideo.right = m_nVideoWidth - rtBorder.right;
	// 		rtVideo.top += rtBorder.top;
	// 		rtVideo.bottom = m_nVideoHeight - rtBorder.bottom;
	if (bPercent)
	{
		if ((pRectBorder->left + pRectBorder->right) >= 100 ||
			(pRectBorder->top + pRectBorder->bottom) >= 100)
			return IPC_Error_InvalidParameters;
	}
	else
	{
		if ((pRectBorder->left + pRectBorder->right) >= m_nVideoWidth ||
			(pRectBorder->top + pRectBorder->bottom) >= m_nVideoHeight)
			return IPC_Error_InvalidParameters;
	}

	Autolock(&m_cslistRenderWnd);
	auto itFind = find_if(m_listRenderWnd.begin(), m_listRenderWnd.end(), WndFinder(hWnd));
	if (itFind != m_listRenderWnd.end())
		(*itFind)->SetBorder(pRectBorder, bPercent);
	return IPC_Succeed;
}

int CIPCPlayer::StartPlay(bool bEnaleAudio , bool bEnableHaccel , bool bFitWindow )
{
#ifdef _DEBUG
	OutputMsg("%s \tObject:%d Time = %d.\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
	m_bPause = false;
	m_bFitWindow = bFitWindow;
	if (GetOsMajorVersion() >= 6)
		m_bEnableHaccel = bEnableHaccel;
	m_bPlayerInialized = false;
	// 		if (!m_hWnd || !IsWindow(m_hWnd))
	// 		{
	// 			return IPC_Error_InvalidWindow;
	// 		}
	if (m_pszFileName)
	{
		if (m_hVideoFile == INVALID_HANDLE_VALUE)
		{
			return GetLastError();
		}

		if (!m_pMediaHeader ||	// �ļ�ͷ��Ч
			!m_nTotalFrames)	// �޷�ȡ����Ƶ��֡��
			return IPC_Error_NotVideoFile;
		// �����ļ������߳�
		m_bThreadParserRun = true;
		m_hThreadFileParser = (HANDLE)_beginthreadex(nullptr, 0, ThreadFileParser, this, 0, 0);

	}
	else
	{
		//  		if (!m_pMediaHeader)
		//  			return IPC_Error_NotInputStreamHeader;		// δ����������ͷ
		m_listVideoCache.clear();
		m_listAudioCache.clear();
	}

	AddRenderWindow(m_hRenderWnd, nullptr);

	m_bStopFlag = false;
	// �����������߳�
	m_bThreadDecodeRun = true;

	if (m_pStreamParser)
	{
		m_bStreamParserRun = true;
		m_hThreadStreamParser = (HANDLE)_beginthreadex(nullptr, 256 * 1024, ThreadStreamParser, this, 0, 0);
	}
	// 		m_pDecodeHelperPtr = make_shared<DecodeHelper>();
	//		m_hQueueTimer = m_TimeQueue.CreateTimer(TimerCallBack, this, 0, 20);
	m_hRenderAsyncEvent = CreateEvent(nullptr, false, false, nullptr);
	m_hThreadDecode = (HANDLE)_beginthreadex(nullptr, 256 * 1024, ThreadDecode, this, 0, 0);
	//m_hThreadReander = (HANDLE)_beginthreadex(nullptr, 256*1024, ThreadRender, this, 0, 0);

	if (!m_hThreadDecode)
	{
#ifdef _DEBUG
		OutputMsg("%s \tObject:%d ThreadPlayVideo Start failed:%d.\n", __FUNCTION__, m_nObjIndex, GetLastError());
#endif
		return IPC_Error_VideoThreadStartFailed;
	}
	if (m_hRenderWnd)
		EnableAudio(bEnaleAudio);
	else
		EnableAudio(false);
	m_dwStartTime = timeGetTime();
	return IPC_Succeed;
}

int CIPCPlayer::StartSyncPlay(bool bFitWindow,CIPCPlayer *pSyncSource,int nVideoFPS )
{
#ifdef _DEBUG
	OutputMsg("%s \tObject:%d Time = %d.\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
	if (!pSyncSource)
	{// ������Ϊͬ��Դ
		if (nVideoFPS <= 0)
			return IPC_Error_InvalidParameters;

		int nIPCPlayInterval = 1000 / nVideoFPS;
		m_nVideoFPS = nVideoFPS;
		m_hRenderAsyncEvent = CreateEvent(nullptr, false, false, nullptr);
		m_pRenderTimer = make_shared<CMMEvent>(m_hRenderAsyncEvent, nIPCPlayInterval);
	}
	else
	{// pSyncSource��Ϊͬ��Դ
		m_pSyncPlayer = pSyncSource;
		m_nVideoFPS = pSyncSource->m_nVideoFPS;
		m_hRenderAsyncEvent = pSyncSource->m_hRenderAsyncEvent;
	}
	m_bPause = false;
	m_bFitWindow = bFitWindow;
	
	m_bEnableHaccel = false;
	m_bPlayerInialized = false;

	m_listVideoCache.clear();
	m_listAudioCache.clear();

	AddRenderWindow(m_hRenderWnd, nullptr);

	m_bStopFlag = false;
	// �����������߳�
	m_bThreadDecodeRun = true;
	
	m_hThreadDecode = (HANDLE)_beginthreadex(nullptr, 256 * 1024, ThreadAsyncDecode, this, 0, 0);
	//m_hThreadAsyncReander = (HANDLE)_beginthreadex(nullptr, 256 * 1024, ThreadAsyncRender, this, 0, 0);

	if (!m_hThreadDecode)
	{
#ifdef _DEBUG
		OutputMsg("%s \tObject:%d ThreadPlayVideo Start failed:%d.\n", __FUNCTION__, m_nObjIndex, GetLastError());
#endif
		return IPC_Error_VideoThreadStartFailed;
	}
	
	EnableAudio(false);
	m_dwStartTime = timeGetTime();
	return IPC_Succeed;
}
int CIPCPlayer::GetFileHeader()
{
	if (!m_hVideoFile)
		return IPC_Error_FileNotOpened;
	DWORD dwBytesRead = 0;
	m_pMediaHeader = make_shared<IPC_MEDIAINFO>();
	if (!m_pMediaHeader)
	{
		CloseHandle(m_hVideoFile);
		return -1;
	}

	if (SetFilePointer(m_hVideoFile, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		assert(false);
		return 0;
	}

	if (!ReadFile(m_hVideoFile, (void *)m_pMediaHeader.get(), sizeof(IPC_MEDIAINFO), &dwBytesRead, nullptr))
	{
		CloseHandle(m_hVideoFile);
		return GetLastError();
	}
	// ������Ƶ�ļ�ͷ
	if ((m_pMediaHeader->nMediaTag != IPC_TAG &&
		m_pMediaHeader->nMediaTag != GSJ_TAG) ||
		dwBytesRead != sizeof(IPC_MEDIAINFO))
	{
		CloseHandle(m_hVideoFile);
		return IPC_Error_NotVideoFile;
	}
	m_nSDKVersion = m_pMediaHeader->nSDKversion;
	switch (m_nSDKVersion)
	{

	case IPC_IPC_SDK_VERSION_2015_09_07:
	case IPC_IPC_SDK_VERSION_2015_10_20:
	case IPC_IPC_SDK_GSJ_HEADER:
	{
		m_nVideoFPS = 25;
		m_nVideoCodec = CODEC_UNKNOWN;
		m_nVideoWidth = 0;
		m_nVideoHeight = 0;
	}
	break;
	case IPC_IPC_SDK_VERSION_2015_12_16:
	{
		m_nVideoCodec = m_pMediaHeader->nVideoCodec;
		m_nAudioCodec = m_pMediaHeader->nAudioCodec;
		if (m_nVideoCodec == CODEC_UNKNOWN)
		{
			m_nVideoWidth = 0;
			m_nVideoHeight = 0;
		}
		else
		{
			m_nVideoWidth = m_pMediaHeader->nVideoWidth;
			m_nVideoHeight = m_pMediaHeader->nVideoHeight;
			if (!m_nVideoWidth || !m_nVideoHeight)
				//return IPC_Error_MediaFileHeaderError;
				m_nVideoCodec = CODEC_UNKNOWN;
		}
		if (m_pMediaHeader->nFps == 0)
			m_nVideoFPS = 25;
		else
			m_nVideoFPS = m_pMediaHeader->nFps;
	}
	break;
	default:

		return IPC_Error_InvalidSDKVersion;
	}
	m_nFileFrameInterval = 1000 / m_nVideoFPS;

	return IPC_Succeed;
}

int CIPCPlayer::InputStream(unsigned char *szFrameData, int nFrameSize, UINT nCacheSize, bool bThreadInside /*�Ƿ��ڲ��̵߳��ñ�־*/)
{
	if (!szFrameData || nFrameSize < sizeof(IPCFrameHeader))
		return IPC_Error_InvalidFrame;

	m_bIpcStream = false;
	int nMaxCacheSize = m_nMaxFrameCache;
	if (nCacheSize != 0)
		nMaxCacheSize = nCacheSize;
	if (m_bStopFlag)
		return IPC_Error_PlayerHasStop;
	if (!bThreadInside)
	{// �������ڲ��̣߳�����Ҫ�����Ƶ����Ƶ�����Ƿ��Ѿ�����
		if (!m_bThreadDecodeRun || !m_hThreadDecode)
		{
#ifdef _DEBUG
			//				OutputMsg("%s \tObject:%d Video decode thread not run.\n", __FUNCTION__, m_nObjIndex);
#endif
			return IPC_Error_VideoThreadNotRun;
		}
	}

	IPCFrameHeader *pHeaderEx = (IPCFrameHeader *)szFrameData;
	if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
	{
		switch (pHeaderEx->nType)
		{
		case FRAME_P:
		case FRAME_B:
		case 0:
		case FRAME_I:
		case FRAME_IDR:
		{
			// 				if (!m_hThreadPlayVideo)
			// 					return IPC_Error_VideoThreadNotRun;
			CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (m_listVideoCache.size() >= nMaxCacheSize)
				return IPC_Error_FrameCacheIsFulled;
			StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval, m_nSDKVersion);
			if (!pStream)
				return IPC_Error_InsufficentMemory;
			m_listVideoCache.push_back(pStream);
			break;
		}
		case FRAME_G711A:      // G711 A�ɱ���֡
		case FRAME_G711U:      // G711 U�ɱ���֡
		case FRAME_G726:       // G726����֡
		case FRAME_AAC:        // AAC����֡��
		{
			// 				if (!m_hThreadPlayVideo)
			// 					return IPC_Error_AudioThreadNotRun;

			if (m_fPlayRate != 1.0f)
				break;
			CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (m_listAudioCache.size() >= nMaxCacheSize * 2)
			{
				if (m_bEnableAudio)
					return IPC_Error_FrameCacheIsFulled;
				else
					m_listAudioCache.pop_front();
			}
			StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval / 2);
			if (!pStream)
				return IPC_Error_InsufficentMemory;
			m_listAudioCache.push_back(pStream);
			m_nAudioFrames++;
			break;
		}
		default:
		{
			assert(false);
			return IPC_Error_InvalidFrameType;
			break;
		}
		}
	}
	else
	{
		switch (pHeaderEx->nType)
		{
		case 0:				// ��ƵBP֡
		case 1:				// ��ƵI֡
		{
			CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (m_listVideoCache.size() >= nMaxCacheSize)
				return IPC_Error_FrameCacheIsFulled;
			StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval, m_nSDKVersion);
			if (!pStream)
				return IPC_Error_InsufficentMemory;
			m_listVideoCache.push_back(pStream);
			break;
		}

		case 2:				// ��Ƶ֡
		case FRAME_G711U:
			//case FRAME_G726:    // G726����֡
		{
			if (!m_bEnableAudio)
				break;
			if (m_fPlayRate != 1.0f)
				break;
			CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (m_listAudioCache.size() >= nMaxCacheSize * 2)
				return IPC_Error_FrameCacheIsFulled;
			Frame(szFrameData)->nType = CODEC_G711U;			// �ɰ�SDKֻ֧��G711U���룬��������ǿ��ת��ΪG711U������ȷ����
			StreamFramePtr pStream = make_shared<StreamFrame>(szFrameData, nFrameSize, m_nFileFrameInterval / 2);
			if (!pStream)
				return IPC_Error_InsufficentMemory;
			m_listAudioCache.push_back(pStream);
			m_nAudioFrames++;
			break;
		}
		default:
		{
			//assert(false);
			return IPC_Error_InvalidFrameType;
			break;
		}
		}
	}

	return IPC_Succeed;
}

int CIPCPlayer::InputStream(IN byte *pFrameData, IN int nFrameType, IN int nFrameLength, int nFrameNum, time_t nFrameTime)
{
	if (m_bStopFlag)
		return IPC_Error_PlayerHasStop;

	if (!m_bThreadDecodeRun || !m_hThreadDecode)
	{
		CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
		if (m_listVideoCache.size() >= 25)
		{
			OutputMsg("%s \tObject:%d Video decode thread not run.\n", __FUNCTION__, m_nObjIndex);
			return IPC_Error_VideoThreadNotRun;
		}
	}
	DWORD dwThreadCode = 0;
	GetExitCodeThread(m_hThreadDecode, &dwThreadCode);
	if (dwThreadCode != STILL_ACTIVE)		// �߳����˳�
	{
		TraceMsgA("%s ThreadDecode has exit Abnormally.\n", __FUNCTION__);
		return IPC_Error_VideoThreadAbnormalExit;
	}

	m_bIpcStream = true;
	switch (nFrameType)
	{
	case 0:									// ��˼I֡��Ϊ0�����ǹ̼���һ��BUG���д�����
	case IPC_IDR_FRAME: 	// IDR֡��
	case IPC_I_FRAME:		// I֡��		
	case IPC_P_FRAME:       // P֡��
	case IPC_B_FRAME:       // B֡��
	case IPC_GOV_FRAME: 	// GOV֡��
	{
		//m_nVideoFraems++;
		StreamFramePtr pStream = make_shared<StreamFrame>(pFrameData, nFrameType, nFrameLength, nFrameNum, nFrameTime);
		CAutoLock lock(&m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
		if (m_listVideoCache.size() >= m_nMaxFrameCache)
		{
			// #ifdef _DEBUG
			// 					OutputMsg("%s \tObject:%d Video Frame cache is Fulled.\n", __FUNCTION__, m_nObjIndex);
			// #endif
			return IPC_Error_FrameCacheIsFulled;
		}
		if (!pStream)
		{
			// #ifdef _DEBUG
			// 					OutputMsg("%s \tObject:%d InsufficentMemory when alloc memory for video frame.\n", __FUNCTION__, m_nObjIndex);
			// #endif
			return IPC_Error_InsufficentMemory;
		}
		m_listVideoCache.push_back(pStream);
	}
	break;
	case IPC_711_ALAW:      // 711 A�ɱ���֡
	case IPC_711_ULAW:      // 711 U�ɱ���֡
	case IPC_726:           // 726����֡
	case IPC_AAC:           // AAC����֡��
	{
		m_nAudioCodec = (IPC_CODEC)nFrameType;
		// 				if ((timeGetTime() - m_dwInputStream) >= 20000)
		// 				{
		// 					TraceMsgA("%s VideoFrames = %d\tAudioFrames = %d.\n", __FUNCTION__, m_nVideoFraems, m_nAudioFrames1);
		// 					m_dwInputStream = timeGetTime();
		// 				}
		if (!m_bEnableAudio)
			break;
		StreamFramePtr pStream = make_shared<StreamFrame>(pFrameData, nFrameType, nFrameLength, nFrameNum, nFrameTime);
		if (!pStream)
			return IPC_Error_InsufficentMemory;
		CAutoLock lock(&m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
		m_nAudioFrames++;
		m_listAudioCache.push_back(pStream);
	}
	break;
	default:
	{
		assert(false);
		break;
	}
	}
	return 0;
}

// ����δ��������
int CIPCPlayer::InputStream(IN byte *pData, IN int nLength)
{
	if (m_pStreamParser/* || m_hThreadStreamParser*/)
	{
		CAutoLock lock(&m_csVideoCache,false,__FILE__,__FUNCTION__,__LINE__);
		if (m_listVideoCache.size() >= m_nMaxFrameCache)
			return IPC_Error_FrameCacheIsFulled;
		lock.Unlock();
		list<FrameBufferPtr> listFrame;
		if (m_pStreamParser->ParserFrame(pData, nLength, listFrame) > 0)
		{
			for (auto it = listFrame.begin(); it != listFrame.end();)
			{
				IPC_STREAM_TYPE nFrameType;
				if (IsH264KeyFrame((*it)->pBuffer))
					nFrameType = IPC_I_FRAME;
				else
					nFrameType = IPC_P_FRAME;
				if (InputStream((*it)->pBuffer, nFrameType, (*it)->nLength, 0, 0) == IPC_Succeed)
				{
					it = listFrame.erase(it);
				}
				else
				{
					Sleep(10);
					continue;
				}
			}

		}
		return IPC_Succeed;
	}
	else
		return IPC_Error_StreamParserNotStarted;
}
int CIPCPlayer::InputDHStream(byte *pBuffer, int nLength)
{
	if (!pBuffer || !nLength)
		return IPC_Error_InvalidParameters;
	if (!m_pDHStreamParser)
	{
		m_pDHStreamParser = make_shared<CDHStreamParser>(pBuffer, nLength);
	}
	else
	{
		int nRet = m_pDHStreamParser->InputStream(pBuffer, nLength);
		if (nRet != IPC_Succeed)
			return nRet;
	}


	return IPC_Succeed;
}
bool CIPCPlayer::StopPlay(DWORD nTimeout )
{
#ifdef _DEBUG
	TraceFunction();
	OutputMsg("%s \tObject:%d Time = %d.\n", __FUNCTION__, m_nObjIndex, timeGetTime() - m_nLifeTime);
#endif
	m_bStopFlag = true;
	m_bThreadParserRun = false;
	m_bThreadDecodeRun = false;
	m_bThreadPlayAudioRun = false;
	m_bStreamParserRun = false;
	HANDLE hArray[16] = { 0 };
	int nHandles = 0;
	if (!m_pSyncPlayer)
		SetEvent(m_hRenderAsyncEvent);

	if (m_bEnableAudio)
	{
		EnableAudio(false);
	}
	m_cslistRenderWnd.Lock();
	m_hRenderWnd = nullptr;
	for (auto it = m_listRenderWnd.begin(); it != m_listRenderWnd.end();)
	{
		InvalidateRect((*it)->hRenderWnd, nullptr, true);
		it = m_listRenderWnd.erase(it);
	}
	m_cslistRenderWnd.Unlock();
	m_bPause = false;
	DWORD dwThreadExitCode = 0;
	if (m_hThreadFileParser)
	{
		GetExitCodeThread(m_hThreadFileParser, &dwThreadExitCode);
		if (dwThreadExitCode == STILL_ACTIVE)		// �߳���������
			hArray[nHandles++] = m_hThreadFileParser;
	}

	// 		if (m_hThreadReander)
	// 		{
	// 			GetExitCodeThread(m_hThreadReander, &dwThreadExitCode);
	// 			if (dwThreadExitCode == STILL_ACTIVE)		// �߳���������
	// 				hArray[nHandles++] = m_hThreadReander;
	// 		}

	if (m_hThreadStreamParser)
	{
		GetExitCodeThread(m_hThreadStreamParser, &dwThreadExitCode);
		if (dwThreadExitCode == STILL_ACTIVE)		// �߳���������
			hArray[nHandles++] = m_hThreadStreamParser;
	}

	if (m_hThreadDecode)
	{
		GetExitCodeThread(m_hThreadDecode, &dwThreadExitCode);
		if (dwThreadExitCode == STILL_ACTIVE)		// �߳���������
			hArray[nHandles++] = m_hThreadDecode;
	}

	if (m_hThreadPlayAudio)
	{
		ResumeThread(m_hThreadPlayAudio);
		GetExitCodeThread(m_hThreadPlayAudio, &dwThreadExitCode);
		if (dwThreadExitCode == STILL_ACTIVE)		// �߳���������
			hArray[nHandles++] = m_hThreadPlayAudio;
	}
	// 		if (m_hThreadGetFileSummary)
	// 			hArray[nHandles++] = m_hThreadGetFileSummary;
	m_csAudioCache.Lock();
	m_listAudioCache.clear();
	m_csAudioCache.Unlock();

	m_csVideoCache.Lock();
	m_listVideoCache.clear();
	m_csVideoCache.Unlock();

	if (nHandles > 0)
	{
		double dfWaitTime = GetExactTime();
		if (WaitForMultipleObjects(nHandles, hArray, true, nTimeout) == WAIT_TIMEOUT)
		{
			OutputMsg("%s Object %d Wait for thread exit timeout.\n", __FUNCTION__, m_nObjIndex);
			m_bAsnycClose = true;
			return false;
		}
		double dfWaitTimeSpan = TimeSpanEx(dfWaitTime);
		OutputMsg("%s Object %d Wait for thread TimeSpan = %.3f.\n", __FUNCTION__, m_nObjIndex, dfWaitTimeSpan);
	}
	else
		OutputMsg("%s Object %d All Thread has exit normal!.\n", __FUNCTION__, m_nObjIndex);
	if (m_hThreadFileParser)
	{
		CloseHandle(m_hThreadFileParser);
		m_hThreadFileParser = nullptr;
		OutputMsg("%s ThreadParser has exit.\n", __FUNCTION__);
	}
	if (m_hThreadDecode)
	{
		CloseHandle(m_hThreadDecode);
		m_hThreadDecode = nullptr;
#ifdef _DEBUG
		OutputMsg("%s ThreadPlayVideo Object:%d has exit.\n", __FUNCTION__, m_nObjIndex);
#endif
	}
	if (m_hThreadStreamParser)
	{
		CloseHandle(m_hThreadStreamParser);
		m_hThreadStreamParser = nullptr;
		OutputMsg("%s ThreadStreamParser Object:%d has exit.\n", __FUNCTION__, m_nObjIndex);
	}
	if (m_hThreadPlayAudio)
	{
		CloseHandle(m_hThreadPlayAudio);
		m_hThreadPlayAudio = nullptr;
		OutputMsg("%s ThreadPlayAudio has exit.\n", __FUNCTION__);
	}
	EnableAudio(false);
	if (m_pFrameOffsetTable)
	{
		delete[]m_pFrameOffsetTable;
		m_pFrameOffsetTable = nullptr;
	}

	if (m_hRenderAsyncEvent &&!m_pSyncPlayer)
	{
		CloseHandle(m_hRenderAsyncEvent);
		m_hRenderAsyncEvent = nullptr;
	}
	m_pDHStreamParser = nullptr;
	
	m_pStreamParser = nullptr;
	m_hThreadDecode = nullptr;
	m_hThreadFileParser = nullptr;
	m_hThreadPlayAudio = nullptr;
	m_pFrameOffsetTable = nullptr;
	return true;
}

int  CIPCPlayer::EnableHaccel(IN bool bEnableHaccel )
{
	if (m_hThreadDecode)
		return IPC_Error_PlayerHasStart;
	else
	{
		if (bEnableHaccel)
		{
			if (GetOsMajorVersion() >= 6)
			{
				m_nPixelFormat = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
				m_bEnableHaccel = bEnableHaccel;
			}
			else
				return IPC_Error_UnsupportHaccel;
		}
		else
			m_bEnableHaccel = bEnableHaccel;
		return IPC_Succeed;
	}
}

bool  CIPCPlayer::IsSupportHaccel(IN IPC_CODEC nCodec)
{
	enum AVCodecID nAvCodec = AV_CODEC_ID_NONE;
	switch (nCodec)
	{
	case CODEC_H264:
		nAvCodec = AV_CODEC_ID_H264;
		break;
	case CODEC_H265:
		nAvCodec = AV_CODEC_ID_H265;
		break;
	default:
		return false;
	}
	shared_ptr<CVideoDecoder>pDecoder = make_shared<CVideoDecoder>();
	UINT nAdapter = D3DADAPTER_DEFAULT;
	if (!pDecoder->InitD3D(nAdapter))
		return false;
	if (pDecoder->CodecIsSupported(nAvCodec) == S_OK)
		return true;
	else
		return false;
}

int CIPCPlayer::GetPlayerInfo(PlayerInfo *pPlayInfo)
{
	if (!pPlayInfo)
		return IPC_Error_InvalidParameters;
	if (m_hThreadDecode || m_hVideoFile)
	{
		ZeroMemory(pPlayInfo, sizeof(PlayerInfo));
		pPlayInfo->nVideoCodec = m_nVideoCodec;
		pPlayInfo->nVideoWidth = m_nVideoWidth;
		pPlayInfo->nVideoHeight = m_nVideoHeight;
		pPlayInfo->nAudioCodec = m_nAudioCodec;
		pPlayInfo->bAudioEnabled = m_bEnableAudio;
		pPlayInfo->tTimeEplased = (time_t)(1000 * (GetExactTime() - m_dfTimesStart));
		pPlayInfo->nFPS = m_nVideoFPS;
		pPlayInfo->nPlayFPS = m_nPlayFPS;
		pPlayInfo->nCacheSize = m_nVideoCache;
		pPlayInfo->nCacheSize2 = m_nAudioCache;
		if (!m_bIpcStream)
		{
			pPlayInfo->bFilePlayFinished = m_bFilePlayFinished;
			pPlayInfo->nCurFrameID = m_nCurVideoFrame;
			pPlayInfo->nTotalFrames = m_nTotalFrames;
			if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
			{
				pPlayInfo->tTotalTime = m_nTotalFrames * 1000 / m_nVideoFPS;
				pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_nFirstFrameTime) / 1000;
			}
			else
			{
				pPlayInfo->tTotalTime = m_nTotalTime;
				if (m_pMediaHeader->nCameraType == 1)	// ��Ѷʿ���
					pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_FirstFrame.nTimestamp) / (1000 * 1000);
				else
				{
					pPlayInfo->tCurFrameTime = (m_tCurFrameTimeStamp - m_FirstFrame.nTimestamp) / 1000;
					pPlayInfo->nCurFrameID = pPlayInfo->tCurFrameTime*m_nVideoFPS / 1000;
				}
			}
		}
		else
			pPlayInfo->tTotalTime = 0;

		return IPC_Succeed;
	}
	else
		return IPC_Error_PlayerNotStart;
}

int CIPCPlayer::SnapShot(IN CHAR *szFileName, IN SNAPSHOT_FORMAT nFileFormat)
{
	if (!szFileName || !strlen(szFileName))
		return -1;
	if (m_hThreadDecode)
	{
		if (WaitForSingleObject(m_hEvnetYUVReady, 5000) == WAIT_TIMEOUT)
			return IPC_Error_PlayerNotStart;
		char szAvError[1024] = { 0 };
		// 			if (m_pSnapshot && m_pSnapshot->GetPixelFormat() != m_nDecodePirFmt)
		// 				m_pSnapshot.reset();

		if (!m_pSnapshot)
		{
			if (m_nDecodePixelFmt == AV_PIX_FMT_DXVA2_VLD)
				m_pSnapshot = make_shared<CSnapshot>(AV_PIX_FMT_YUV420P, m_nVideoWidth, m_nVideoHeight);
			else
				m_pSnapshot = make_shared<CSnapshot>(m_nDecodePixelFmt, m_nVideoWidth, m_nVideoHeight);

			if (!m_pSnapshot)
				return IPC_Error_InsufficentMemory;
			if (m_pSnapshot->SetCodecID(m_nVideoCodec) != IPC_Succeed)
				return IPC_Error_UnsupportedCodec;
		}

		SetEvent(m_hEventYUVRequire);
		if (WaitForSingleObject(m_hEventFrameCopied, 2000) == WAIT_TIMEOUT)
			return IPC_Error_SnapShotFailed;
		int nResult = IPC_Succeed;
		switch (nFileFormat)
		{
		case XIFF_BMP:
			if (!m_pSnapshot->SaveBmp(szFileName))
				nResult = IPC_Error_SnapShotFailed;
			break;
		case XIFF_JPG:
			if (!m_pSnapshot->SaveJpeg(szFileName))
				nResult = IPC_Error_SnapShotFailed;
			break;
		default:
			nResult = IPC_Error_UnsupportedFormat;
			break;
		}
		//m_pSnapshot.reset();
		return nResult;
	}
	else
		return IPC_Error_PlayerNotStart;
	return IPC_Succeed;
}

void CIPCPlayer::ProcessSnapshotRequire(AVFrame *pAvFrame)
{
	if (!pAvFrame)
		return;
	if (WaitForSingleObject(m_hEventYUVRequire, 0) == WAIT_TIMEOUT)
		return;

	if (pAvFrame->format == AV_PIX_FMT_YUV420P ||
		pAvFrame->format == AV_PIX_FMT_YUVJ420P)
	{// �ݲ�֧��dxva Ӳ����֡
		m_pSnapshot->CopyFrame(pAvFrame);
		SetEvent(m_hEventFrameCopied);
	}
	else if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
	{
		m_pSnapshot->CopyDxvaFrame(pAvFrame);
		SetEvent(m_hEventFrameCopied);
	}
	else
	{
		return;
	}
}

int CIPCPlayer::SetRate(IN float fPlayRate)
{
#ifdef _DEBUG
	m_nRenderFPS = 0;
	dfTRender = 0.0f;
	m_nRenderFrames = 0;
#endif
	if (m_bIpcStream)
	{
		return IPC_Error_NotFilePlayer;
	}
	if (fPlayRate > (float)m_nVideoFPS)
		fPlayRate = m_nVideoFPS;
	// ȡ�õ�ǰ��ʾ����ˢ���ʣ���ʾ����ˢ���ʾ�������ʾͼ������֡��
	// ͨ��ͳ��ÿ��ʾһ֡ͼ��(���������ʾ)�ķѵ�ʱ��

	DEVMODE   dm;
	dm.dmSize = sizeof(DEVMODE);
	::EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
	m_fPlayInterval = (int)(1000 / (m_nVideoFPS*fPlayRate));
	/// marked by xionggao.lee @2016.03.23
	// 		float nMinPlayInterval = ((float)1000) / dm.dmDisplayFrequency;
	// 		if (m_fPlayInterval < nMinPlayInterval)
	// 			m_fPlayInterval = nMinPlayInterval;
	m_nPlayFPS = 1000 / m_fPlayInterval;
	m_fPlayRate = fPlayRate;

	return IPC_Succeed;
}

int CIPCPlayer::SeekFrame(IN int nFrameID, bool bUpdate )
{
	if (!m_bSummaryIsReady)
		return IPC_Error_SummaryNotReady;

	if (!m_hVideoFile || !m_pFrameOffsetTable)
		return IPC_Error_NotFilePlayer;

	if (nFrameID < 0 || nFrameID > m_nTotalFrames)
		return IPC_Error_InvalidFrame;
	m_csVideoCache.Lock();
	m_listVideoCache.clear();
	m_csVideoCache.Unlock();

	m_csAudioCache.Lock();
	m_listAudioCache.clear();
	m_csAudioCache.Unlock();

	// ���ļ�ժҪ�У�ȡ���ļ�ƫ����Ϣ
	// ���������I֡
	int nForward = nFrameID, nBackWord = nFrameID;
	while (nForward < m_nTotalFrames)
	{
		if (m_pFrameOffsetTable[nForward].bIFrame)
			break;
		nForward++;
	}
	if (nForward >= m_nTotalFrames)
		nForward--;

	while (nBackWord > 0 && nBackWord < m_nTotalFrames)
	{
		if (m_pFrameOffsetTable[nBackWord].bIFrame)
			break;
		nBackWord--;
	}

	if ((nForward - nFrameID) <= (nFrameID - nBackWord))
		m_nFrametoRead = nForward;
	else
		m_nFrametoRead = nBackWord;
	m_nCurVideoFrame = m_nFrametoRead;
	//TraceMsgA("%s Seek to Frame %d\tFrameTime = %I64d\n", __FUNCTION__, m_nFrametoRead, m_pFrameOffsetTable[m_nFrametoRead].tTimeStamp/1000);
	if (m_hThreadFileParser)
		SetSeekOffset(m_pFrameOffsetTable[m_nFrametoRead].nOffset);
	else
	{// ֻ���ڵ����Ľ����ļ�ʱ�ƶ��ļ�ָ��
		CAutoLock lock(&m_csParser, false, __FILE__, __FUNCTION__, __LINE__);
		m_nParserOffset = 0;
		m_nParserDataLength = 0;
		LONGLONG nOffset = m_pFrameOffsetTable[m_nFrametoRead].nOffset;
		if (LargerFileSeek(m_hVideoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
	}
	if (bUpdate &&
		m_hThreadDecode &&	// �������������߳�
		m_bPause &&				// ��������ͣģʽ			
		m_pDecoder)				// ����������������
	{
		// ��ȡһ֡,�����Խ���,��ʾ
		DWORD nBufferSize = m_pFrameOffsetTable[m_nFrametoRead].nFrameSize;
		LONGLONG nOffset = m_pFrameOffsetTable[m_nFrametoRead].nOffset;

		byte *pBuffer = _New byte[nBufferSize + 1];
		if (!pBuffer)
			return IPC_Error_InsufficentMemory;

		unique_ptr<byte>BufferPtr(pBuffer);
		DWORD nBytesRead = 0;
		if (LargerFileSeek(m_hVideoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}

		if (!ReadFile(m_hVideoFile, pBuffer, nBufferSize, &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
		AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
		shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
		AVFrame *pAvFrame = av_frame_alloc();
		shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);
		av_init_packet(pAvPacket);
		m_nCurVideoFrame = Frame(pBuffer)->nFrameID;
		pAvPacket->size = Frame(pBuffer)->nLength;
		if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
			pAvPacket->data = pBuffer + sizeof(IPCFrameHeaderEx);
		else
			pAvPacket->data = pBuffer + sizeof(IPCFrameHeader);
		int nGotPicture = 0;
		char szAvError[1024] = { 0 };
		int nAvError = m_pDecoder->Decode(pAvFrame, nGotPicture, pAvPacket);
		if (nAvError < 0)
		{
			av_strerror(nAvError, szAvError, 1024);
			OutputMsg("%s Decode error:%s.\n", __FUNCTION__, szAvError);
			return IPC_Error_DecodeFailed;
		}
		av_packet_unref(pAvPacket);
		if (nGotPicture)
		{
			RenderFrame(pAvFrame);
		}
		av_frame_unref(pAvFrame);
		return IPC_Succeed;
	}

	return IPC_Succeed;
}

int CIPCPlayer::SeekTime(IN time_t tTimeOffset, bool bUpdate)
{
	if (!m_hVideoFile)
		return IPC_Error_NotFilePlayer;
	if (!m_bSummaryIsReady)
		return IPC_Error_SummaryNotReady;

	int nFrameID = 0;
	if (m_nVideoFPS == 0 || m_nFileFrameInterval == 0)
	{// ʹ�ö��ַ�����
		nFrameID = BinarySearch(tTimeOffset);
		if (nFrameID == -1)
			return IPC_Error_InvalidTimeOffset;
	}
	else
	{
		int nTimeDiff = tTimeOffset;// -m_pFrameOffsetTable[0].tTimeStamp;
		if (nTimeDiff < 0)
			return IPC_Error_InvalidTimeOffset;
		nFrameID = nTimeDiff / m_nFileFrameInterval;
	}
	return SeekFrame(nFrameID, bUpdate);
}

int CIPCPlayer::GetFrame(INOUT byte **pBuffer, OUT UINT &nBufferSize)
{
	if (!m_hVideoFile)
		return IPC_Error_NotFilePlayer;
	if (m_hThreadDecode || m_hThreadFileParser)
		return IPC_Error_PlayerHasStart;
	if (!m_pFrameOffsetTable || !m_nTotalFrames)
		return IPC_Error_SummaryNotReady;

	DWORD nBytesRead = 0;
	FrameParser Parser;
	CAutoLock lock(&m_csParser, false, __FILE__, __FUNCTION__, __LINE__);
	byte *pFrameBuffer = &m_pParserBuffer[m_nParserOffset];
	if (!ParserFrame(&pFrameBuffer, m_nParserDataLength, &Parser))
	{
		// �������ݳ�ΪnDataLength
		memmove(m_pParserBuffer, pFrameBuffer, m_nParserDataLength);
		if (!ReadFile(m_hVideoFile, &m_pParserBuffer[m_nParserDataLength], (m_nParserBufferSize - m_nParserDataLength), &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
		m_nParserOffset = 0;
		m_nParserDataLength += nBytesRead;
		pFrameBuffer = m_pParserBuffer;
		if (!ParserFrame(&pFrameBuffer, m_nParserDataLength, &Parser))
		{
			return IPC_Error_NotVideoFile;
		}
	}
	m_nParserOffset += Parser.nFrameSize;
	*pBuffer = (byte *)Parser.pHeaderEx;
	nBufferSize = Parser.nFrameSize;
	return IPC_Succeed;
}

int CIPCPlayer::SeekNextFrame()
{
	if (m_hThreadDecode &&	// �������������߳�
		m_bPause &&				// ��������ͣģʽ			
		m_pDecoder)				// ����������������
	{
		if (!m_hVideoFile || !m_pFrameOffsetTable)
			return IPC_Error_NotFilePlayer;

		m_csVideoCache.Lock();
		m_listVideoCache.clear();
		//m_nFrameOffset = 0;
		m_csVideoCache.Unlock();

		m_csAudioCache.Lock();
		m_listAudioCache.clear();
		m_csAudioCache.Unlock();

		// ��ȡһ֡,�����Խ���,��ʾ
		DWORD nBufferSize = m_pFrameOffsetTable[m_nCurVideoFrame].nFrameSize;
		LONGLONG nOffset = m_pFrameOffsetTable[m_nCurVideoFrame].nOffset;

		byte *pBuffer = _New byte[nBufferSize + 1];
		if (!pBuffer)
			return IPC_Error_InsufficentMemory;

		unique_ptr<byte>BufferPtr(pBuffer);
		DWORD nBytesRead = 0;
		if (LargerFileSeek(m_hVideoFile, nOffset, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			OutputMsg("%s LargerFileSeek  Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}

		if (!ReadFile(m_hVideoFile, pBuffer, nBufferSize, &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return GetLastError();
		}
		AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
		shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
		AVFrame *pAvFrame = av_frame_alloc();
		shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);
		av_init_packet(pAvPacket);
		pAvPacket->size = Frame(pBuffer)->nLength;
		if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
			pAvPacket->data = pBuffer + sizeof(IPCFrameHeaderEx);
		else
			pAvPacket->data = pBuffer + sizeof(IPCFrameHeader);
		int nGotPicture = 0;
		char szAvError[1024] = { 0 };
		int nAvError = m_pDecoder->Decode(pAvFrame, nGotPicture, pAvPacket);
		if (nAvError < 0)
		{
			av_strerror(nAvError, szAvError, 1024);
			OutputMsg("%s Decode error:%s.\n", __FUNCTION__, szAvError);
			return IPC_Error_DecodeFailed;
		}
		av_packet_unref(pAvPacket);
		if (nGotPicture)
		{
			RenderFrame(pAvFrame);
			ProcessYUVCapture(pAvFrame, (LONGLONG)GetExactTime() * 1000);

			m_tCurFrameTimeStamp = m_pFrameOffsetTable[m_nCurVideoFrame].tTimeStamp;
			m_nCurVideoFrame++;
			Autolock(&m_csFilePlayCallBack);
			if (m_pFilePlayCallBack)
				m_pFilePlayCallBack(this, m_pUserFilePlayer);
		}
		av_frame_unref(pAvFrame);
		return IPC_Succeed;
	}
	else
		return IPC_Error_PlayerIsNotPaused;
}

int CIPCPlayer::EnableAudio(bool bEnable )
{
	TraceFunction();
	// 		if (m_fPlayRate != 1.0f)
	// 			return IPC_Error_AudioFailed;
	if (m_nAudioCodec == CODEC_UNKNOWN)
	{
		return IPC_Error_UnsupportedCodec;
	}
	if (m_pDsoundEnum->GetAudioPlayDevices() <= 0)
		return IPC_Error_AudioFailed;
	if (bEnable)
	{
		if (!m_pDsPlayer)
			m_pDsPlayer = make_shared<CDSound>(m_hRenderWnd);
		if (!m_pDsPlayer->IsInitialized())
		{
			if (!m_pDsPlayer->Initialize(m_hRenderWnd, m_nAudioPlayFPS, 1, m_nSampleFreq, m_nSampleBit))
			{
				m_pDsPlayer = nullptr;
				m_bEnableAudio = false;
				return IPC_Error_AudioFailed;
			}
		}
		m_pDsBuffer = m_pDsPlayer->CreateDsoundBuffer();
		if (!m_pDsBuffer)
		{
			m_bEnableAudio = false;
			assert(false);
			return IPC_Error_AudioFailed;
		}
		m_bThreadPlayAudioRun = true;
		m_hAudioFrameEvent[0] = CreateEvent(nullptr, false, true, nullptr);
		m_hAudioFrameEvent[1] = CreateEvent(nullptr, false, true, nullptr);

		m_hThreadPlayAudio = (HANDLE)_beginthreadex(nullptr, 0, m_nAudioPlayFPS == 8 ? ThreadPlayAudioGSJ : ThreadPlayAudioIPC, this, 0, 0);
		m_pDsBuffer->StartPlay();
		m_pDsBuffer->SetVolume(50);
		m_dfLastTimeAudioPlay = 0.0f;
		m_dfLastTimeAudioSample = 0.0f;
		m_bEnableAudio = true;
	}
	else
	{
		if (m_hThreadPlayAudio)
		{
			if (m_bThreadPlayAudioRun)		// ��δִ��ֹͣ��Ƶ�����̵߳Ĳ���
			{
				m_bThreadPlayAudioRun = false;
				ResumeThread(m_hThreadPlayAudio);
				WaitForSingleObject(m_hThreadPlayAudio, INFINITE);
				CloseHandle(m_hThreadPlayAudio);
				m_hThreadPlayAudio = nullptr;
				OutputMsg("%s ThreadPlayAudio has exit.\n", __FUNCTION__);
				m_csAudioCache.Lock();
				m_listAudioCache.clear();
				m_csAudioCache.Unlock();
			}
			CloseHandle(m_hAudioFrameEvent[0]);
			CloseHandle(m_hAudioFrameEvent[1]);
		}

		if (m_pDsBuffer)
		{
			m_pDsPlayer->DestroyDsoundBuffer(m_pDsBuffer);
			m_pDsBuffer = nullptr;
		}
		if (m_pDsPlayer)
			m_pDsPlayer = nullptr;
		m_bEnableAudio = false;
	}
	return IPC_Succeed;
}

void CIPCPlayer::Refresh()
{
	if (m_hRenderWnd)
	{
		::InvalidateRect(m_hRenderWnd, nullptr, true);
		Autolock(&m_cslistRenderWnd);
		//m_pDxSurface->Present(m_hRenderWnd);					
		if (m_listRenderWnd.size() > 0)
		{
			for (auto it = m_listRenderWnd.begin(); it != m_listRenderWnd.end(); it++)
				::InvalidateRect((*it)->hRenderWnd, nullptr, true);
		}
	}
}

void CIPCPlayer::SetBackgroundImage(LPCWSTR szImageFilePath)
{
	if (szImageFilePath)
	{
		if (PathFileExistsW(szImageFilePath))
		{
			m_pszBackImagePath = new WCHAR[wcslen(szImageFilePath) + 1];
			wcscpy_s(m_pszBackImagePath, wcslen(szImageFilePath) + 1, szImageFilePath);
		}
	}
	else
	{
		if (m_pszBackImagePath)
		{
			delete[]m_pszBackImagePath;
			m_pszBackImagePath = nullptr;
		}
	}

}

// ��������ʧ��ʱ������0�����򷵻�������ľ��
long CIPCPlayer::AddLineArray(POINT *pPointArray, int nCount, float fWidth, D3DCOLOR nColor)
{
	if (m_pDxSurface)
	{
		return m_pDxSurface->AddD3DLineArray(pPointArray, nCount, fWidth, nColor);
	}
	else
	{
		//assert(false);
		return IPC_Error_DXRenderNotInitialized;
	}
}

int	CIPCPlayer::RemoveLineArray(long nIndex)
{
	if (m_pDxSurface)
	{
		return m_pDxSurface->RemoveD3DLineArray(nIndex);
	}
	else
	{
		//assert(false);
		return IPC_Error_DXRenderNotInitialized;
	}
}

long CIPCPlayer::AddPolygon(POINT *pPointArray, int nCount, WORD *pVertexIndex, DWORD nColor)
{
	if (m_pDxSurface)
		return m_pDxSurface->AddPolygon(pPointArray, nCount, pVertexIndex, nColor);
	else
		return IPC_Error_DXRenderNotInitialized;
}

int CIPCPlayer::RemovePolygon(long nIndex)
{
	if (m_pDxSurface)
	{
		m_pDxSurface->RemovePolygon(nIndex);
		return IPC_Succeed;
	}
	else
	{
		//assert(false);
		return IPC_Error_DXRenderNotInitialized;
	}
}

int CIPCPlayer::SetCallBack(IPC_CALLBACK nCallBackType, IN void *pUserCallBack, IN void *pUserPtr)
{
	switch (nCallBackType)
	{
	case ExternDcDraw:
	{
		m_pDCCallBack = pUserCallBack;
		m_pDCCallBackParam = pUserPtr;
		if (m_pDxSurface)
		{
			m_pDxSurface->SetExternDraw(pUserCallBack, pUserPtr);
		}
		return IPC_Succeed;
	}
	break;
	case ExternDcDrawEx:
		if (m_pDxSurface)
		{
			assert(false);
			return IPC_Succeed;
		}
		else
			return IPC_Error_DxError;
		break;
	case YUVCapture:
	{
		Autolock(&m_csCaptureYUV);
		m_pfnCaptureYUV = (CaptureYUV)pUserCallBack;
		m_pUserCaptureYUV = pUserPtr;
	}
	break;
	case YUVCaptureEx:
	{
		Autolock(&m_csCaptureYUVEx)
			m_pfnCaptureYUVEx = (CaptureYUVEx)pUserCallBack;
		m_pUserCaptureYUVEx = pUserPtr;
	}
	break;
	case YUVFilter:
	{
		Autolock(&m_csYUVFilter);
		m_pfnYUVFilter = (CaptureYUVEx)pUserCallBack;
		m_pUserYUVFilter = pUserPtr;
	}
	break;
	case FramePaser:
	{
		// 			m_pfnCaptureFrame = (CaptureFrame)pUserCallBack;
		// 			m_pUserCaptureFrame = pUserPtr;
	}
	break;
	case FilePlayer:
	{
		Autolock(&m_csFilePlayCallBack);
		m_pFilePlayCallBack = (FilePlayProc)pUserCallBack;
		m_pUserFilePlayer = pUserPtr;
	}
	break;
	case RGBCapture:
	{
		Autolock(&m_csCaptureRGB);
		m_pUserCaptureRGB = (CaptureRGB)pUserCallBack;
		m_pUserCaptureRGB = pUserPtr;
	}
		break;
	default:
		return IPC_Error_InvalidParameters;
		break;
	}
	return IPC_Succeed;
}

int CIPCPlayer::GetFileSummary(volatile bool &bWorking)
{
	//#ifdef _DEBUG
	double dfTimeStart = GetExactTime();
	//#endif
	DWORD nBufferSize = 1024 * 1024 * 16;

	// ���ٷ����ļ�����ΪStartPlay�Ѿ�����������ȷ��
	DWORD nOffset = sizeof(IPC_MEDIAINFO);
	if (SetFilePointer(m_hVideoFile, nOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		assert(false);
		return -1;
	}
	if (!m_pFrameOffsetTable)
	{
		m_pFrameOffsetTable = _New FileFrameInfo[m_nTotalFrames];
		ZeroMemory(m_pFrameOffsetTable, sizeof(FileFrameInfo)*m_nTotalFrames);
	}

	byte *pBuffer = _New byte[nBufferSize];
	while (!pBuffer)
	{
		if (nBufferSize <= 1024 * 512)
		{// ��512K���ڴ涼�޷�����Ļ������˳�
			OutputMsg("%s Can't alloc enough memory.\n", __FUNCTION__);
			assert(false);
			return IPC_Error_InsufficentMemory;
		}
		nBufferSize /= 2;
		pBuffer = _New byte[nBufferSize];
	}
	shared_ptr<byte>BufferPtr(pBuffer);
	byte *pFrame = nullptr;
	int nFrameSize = 0;
	int nVideoFrames = 0;
	int nAudioFrames = 0;
	LONG nSeekOffset = 0;
	DWORD nBytesRead = 0;
	DWORD nDataLength = 0;
	byte *pFrameBuffer = nullptr;
	FrameParser Parser;
	int nFrameOffset = sizeof(IPC_MEDIAINFO);
	bool bIFrame = false;
	bool bStreamProbed = false;		// �Ƿ��Ѿ�̽�������
	const UINT nMaxCache = 100;
	bool bFirstBlockIsFilled = true;
	int nAllFrames = 0;
	//m_bEnableAudio = true;			// �ȿ�����Ƶ��ǣ���������Ƶ����,�����ڹر���Ƶ���򻺴����ݻ��Զ�ɾ��

	while (true && bWorking)
	{
		double dfT1 = GetExactTime();
		if (!ReadFile(m_hVideoFile, &pBuffer[nDataLength], (nBufferSize - nDataLength), &nBytesRead, nullptr))
		{
			OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return IPC_Error_ReadFileFailed;
		}
		dfT1 = GetExactTime();
		if (nBytesRead == 0)		// δ��ȡ�κ����ݣ��Ѿ��ﵽ�ļ���β
			break;
		pFrameBuffer = pBuffer;
		nDataLength += nBytesRead;
		int nLength1 = 0;
		while (true && bWorking)
		{
			if (!ParserFrame(&pFrameBuffer, nDataLength, &Parser))
				break;
			nAllFrames++;
			nLength1 = 16 * 1024 * 1024 - (pFrameBuffer - pBuffer);
			if (nLength1 != nDataLength)
			{
				int nBreak = 3;
			}
			if (bFirstBlockIsFilled)
			{
				if (InputStream((byte *)Parser.pHeader, Parser.nFrameSize, (UINT)nMaxCache, true) == IPC_Error_FrameCacheIsFulled &&
					bWorking)
				{
					m_nSummaryOffset = nFrameOffset;
					m_csVideoCache.Lock();
					int nVideoSize = m_listVideoCache.size();
					m_csVideoCache.Unlock();

					m_csAudioCache.Lock();
					int nAudioSize = m_listAudioCache.size();
					m_csAudioCache.Unlock();

					m_nHeaderFrameID = m_listVideoCache.front()->FrameHeader()->nFrameID;
					TraceMsgA("HeadFrame ID = %d.\n", m_nHeaderFrameID);
					bFirstBlockIsFilled = false;
				}
			}

			if (IsIPCVideoFrame(Parser.pHeader, bIFrame, m_nSDKVersion))	// ֻ��¼��Ƶ֡���ļ�ƫ��
			{
				// 					if (m_nVideoCodec == CODEC_UNKNOWN &&
				// 						bIFrame &&
				// 						!bStreamProbed)
				// 					{// ����̽������
				// 						bStreamProbed = ProbeStream((byte *)Parser.pRawFrame, Parser.nRawFrameSize);
				// 					}
				if (nVideoFrames < m_nTotalFrames)
				{
					if (m_pFrameOffsetTable)
					{
						m_pFrameOffsetTable[nVideoFrames].nOffset = nFrameOffset;
						m_pFrameOffsetTable[nVideoFrames].nFrameSize = Parser.nFrameSize;
						m_pFrameOffsetTable[nVideoFrames].bIFrame = bIFrame;
						// ����֡ID���ļ����ż������ȷ����ÿһ֡�Ĳ���ʱ��
						if (m_nSDKVersion >= IPC_IPC_SDK_VERSION_2015_12_16 && m_nSDKVersion != IPC_IPC_SDK_GSJ_HEADER)
							m_pFrameOffsetTable[nVideoFrames].tTimeStamp = nVideoFrames*m_nFileFrameInterval * 1000;
						else
							m_pFrameOffsetTable[nVideoFrames].tTimeStamp = Parser.pHeader->nTimestamp;
					}
				}
				else
					OutputMsg("%s %d(%s) Frame (%d) overflow.\n", __FILE__, __LINE__, __FUNCTION__, nVideoFrames);
				nVideoFrames++;
			}
			else
			{
				m_nAudioCodec = (IPC_CODEC)Parser.pHeaderEx->nType;
				nAudioFrames++;
			}

			nFrameOffset += Parser.nFrameSize;
		}
		nOffset += nBytesRead;
		// 			if (bFirstBlockIsFilled && m_bThreadSummaryRun)
		// 			{
		// 				SetEvent(m_hCacheFulled);
		// 				m_nSummaryOffset = nFrameOffset;
		// 				CAutoLock lock(&m_csVideoCache);
		// 				m_nHeaderFrameID = m_listVideoCache.front()->FrameHeader()->nFrameID;
		// 				TraceMsgA("VideoCache = %d\tAudioCache = %d.\n", m_listVideoCache.size(), m_listAudioCache.size());
		// 				bFirstBlockIsFilled = false;
		// 			}
		// �������ݳ�ΪnDataLength
		memcpy(pBuffer, pFrameBuffer, nDataLength);
		ZeroMemory(&pBuffer[nDataLength], nBufferSize - nDataLength);
	}
	m_nTotalFrames = nVideoFrames;
	//#ifdef _DEBUG
	OutputMsg("%s TimeSpan = %.3f\tnVideoFrames = %d\tnAudioFrames = %d.\n", __FUNCTION__, TimeSpanEx(dfTimeStart), nVideoFrames, nAudioFrames);
	//#endif		
	m_bSummaryIsReady = true;
	return IPC_Succeed;
}

bool CIPCPlayer::ParserFrame(INOUT byte **ppBuffer,	INOUT DWORD &nDataSize,	FrameParser* pFrameParser)
{
	int nOffset = 0;
	if (nDataSize < sizeof(IPCFrameHeaderEx))
		return false;
	if (Frame(*ppBuffer)->nFrameTag != IPC_TAG &&
		Frame(*ppBuffer)->nFrameTag != GSJ_TAG)
	{
		static char *szKey1 = "MOVD";
		static char *szKey2 = "IMWH";
		nOffset = KMP_StrFind(*ppBuffer, nDataSize, (byte *)szKey1, 4);
		if (nOffset < 0)
			nOffset = KMP_StrFind(*ppBuffer, nDataSize, (byte *)szKey2, 4);
		nOffset -= offsetof(IPCFrameHeader, nFrameTag);
	}

	if (nOffset < 0)
		return false;

	byte *pFrameBuff = *ppBuffer;
	if (m_nSDKVersion < IPC_IPC_SDK_VERSION_2015_12_16 || m_nSDKVersion == IPC_IPC_SDK_GSJ_HEADER)
	{// �ɰ��ļ�
		// ֡ͷ��Ϣ������
		if ((nOffset + sizeof(IPCFrameHeader)) >= nDataSize)
			return false;
		pFrameBuff += nOffset;
		// ֡���ݲ�����
		if (nOffset + FrameSize2(pFrameBuff) >= nDataSize)
			return false;
		if (pFrameParser)
		{
			pFrameParser->pHeader = (IPCFrameHeader *)(pFrameBuff);
			bool bIFrame = false;
			// 				if (IsIPCVideoFrame(pFrameParser->pHeaderEx, bIFrame,m_nSDKVersion))
			// 					OutputMsg("Frame ID:%d\tType = Video:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
			// 				else
			// 					OutputMsg("Frame ID:%d\tType = Audio:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
			pFrameParser->nFrameSize = FrameSize2(pFrameBuff);
			pFrameParser->pRawFrame = *ppBuffer + sizeof(IPCFrameHeader);
			pFrameParser->nRawFrameSize = Frame2(pFrameBuff)->nLength;
		}
		nDataSize -= (FrameSize2(pFrameBuff) + nOffset);
		pFrameBuff += FrameSize2(pFrameBuff);
	}
	else
	{// �°��ļ�
		// ֡ͷ��Ϣ������
		if ((nOffset + sizeof(IPCFrameHeaderEx)) >= nDataSize)
			return false;

		pFrameBuff += nOffset;

		// ֡���ݲ�����
		if (nOffset + FrameSize(pFrameBuff) >= nDataSize)
			return false;

		if (pFrameParser)
		{
			pFrameParser->pHeaderEx = (IPCFrameHeaderEx *)pFrameBuff;
			bool bIFrame = false;
			// 				if (IsIPCVideoFrame(pFrameParser->pHeaderEx, bIFrame,m_nSDKVersion))
			// 					OutputMsg("Frame ID:%d\tType = Video:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
			// 				else
			// 					OutputMsg("Frame ID:%d\tType = Audio:%d.\n", pFrameParser->pHeaderEx->nFrameID, pFrameParser->pHeaderEx->nType);
			pFrameParser->nFrameSize = FrameSize(pFrameBuff);
			pFrameParser->pRawFrame = pFrameBuff + sizeof(IPCFrameHeaderEx);
			pFrameParser->nRawFrameSize = Frame(pFrameBuff)->nLength;
		}
		nDataSize -= (FrameSize(pFrameBuff) + nOffset);
		pFrameBuff += FrameSize(pFrameBuff);
	}
	*ppBuffer = pFrameBuff;
	return true;
}

///< @brief ��Ƶ�ļ������߳�
UINT __stdcall CIPCPlayer::ThreadFileParser(void *p)
{// ��ָ������Ч�Ĵ��ھ������ѽ�������ļ����ݷ��벥�Ŷ��У����򲻷��벥�Ŷ���
	CIPCPlayer* pThis = (CIPCPlayer *)p;
	LONGLONG nSeekOffset = 0;
	DWORD nBufferSize = pThis->m_nMaxFrameSize * 4;
	DWORD nBytesRead = 0;
	DWORD nDataLength = 0;

	LARGE_INTEGER liFileSize;
	if (!GetFileSizeEx(pThis->m_hVideoFile, &liFileSize))
		return 0;

	if (pThis->GetFileSummary(pThis->m_bThreadParserRun) != IPC_Succeed)
	{
		assert(false);
		return 0;
	}

	byte *pBuffer = _New byte[nBufferSize];
	shared_ptr<byte>BufferPtr(pBuffer);
	FrameParser Parser;
	pThis->m_tLastFrameTime = 0;
	if (SetFilePointer(pThis->m_hVideoFile, (LONG)sizeof(IPC_MEDIAINFO), nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
	{
		pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
		assert(false);
		return 0;
	}

#ifdef _DEBUG
	double dfT1 = GetExactTime();
	bool bOuputTime = false;
#endif
	IPCFrameHeaderEx HeaderEx;
	int nInputResult = 0;
	bool bFileEnd = false;
	while (pThis->m_bThreadParserRun)
	{
		if (pThis->m_bPause)
		{
			Sleep(20);
			continue;
		}
		if (pThis->m_nSummaryOffset)
		{
			CAutoLock lock(&pThis->m_csVideoCache);
			if (pThis->m_listVideoCache.size() < pThis->m_nMaxFrameCache)
			{
				if (SetFilePointer(pThis->m_hVideoFile, (LONG)pThis->m_nSummaryOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
					pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
				pThis->m_nSummaryOffset = 0;
				lock.Unlock();
				Sleep(20);
			}
			else
			{
				lock.Unlock();
				Sleep(20);
				continue;
			}
		}
		else if (nSeekOffset = pThis->GetSeekOffset())	// �Ƿ���Ҫ�ƶ��ļ�ָ��,��nSeekOffset��Ϊ0������Ҫ�ƶ��ļ�ָ��
		{
			pThis->OutputMsg("Detect SeekFrame Operation.\n");

			pThis->m_csVideoCache.Lock();
			pThis->m_listVideoCache.clear();
			pThis->m_csVideoCache.Unlock();

			pThis->m_csAudioCache.Lock();
			pThis->m_listAudioCache.clear();
			pThis->m_csAudioCache.Unlock();

			pThis->SetSeekOffset(0);
			bFileEnd = false;
			pThis->m_bFilePlayFinished = false;
			nDataLength = 0;
#ifdef _DEBUG
			pThis->m_bSeekSetDetected = true;
#endif
			if (SetFilePointer(pThis->m_hVideoFile, (LONG)nSeekOffset, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
				pThis->OutputMsg("%s SetFilePointer Failed,Error = %d.\n", __FUNCTION__, GetLastError());
		}
		if (bFileEnd)
		{// �ļ���ȡ���£��Ҳ��Ŷ���Ϊ�գ�����Ϊ���Ž���
			pThis->m_csVideoCache.Lock();
			int nVideoCacheSize = pThis->m_listVideoCache.size();
			pThis->m_csVideoCache.Unlock();
			if (nVideoCacheSize == 0)
			{
				pThis->m_bFilePlayFinished = true;
			}
			Sleep(20);
			continue;
		}
		if (!ReadFile(pThis->m_hVideoFile, &pBuffer[nDataLength], (nBufferSize - nDataLength), &nBytesRead, nullptr))
		{
			pThis->OutputMsg("%s ReadFile Failed,Error = %d.\n", __FUNCTION__, GetLastError());
			return 0;
		}

		if (nBytesRead == 0)
		{// �����ļ���β
			pThis->OutputMsg("%s Reaching File end nBytesRead = %d.\n", __FUNCTION__, nBytesRead);
			LONGLONG nOffset = 0;
			if (!GetFilePosition(pThis->m_hVideoFile, nOffset))
			{
				pThis->OutputMsg("%s GetFilePosition Failed,Error =%d.\n", __FUNCTION__, GetLastError());
				return 0;
			}
			if (nOffset == liFileSize.QuadPart)
			{
				bFileEnd = true;
				pThis->OutputMsg("%s Reaching File end.\n", __FUNCTION__);
			}
		}
		else
			pThis->m_bFilePlayFinished = false;
		nDataLength += nBytesRead;
		byte *pFrameBuffer = pBuffer;

		bool bIFrame = false;
		bool bFrameInput = true;
		while (pThis->m_bThreadParserRun)
		{
			if (pThis->m_bPause)		// ͨ��pause ��������ͣ���ݶ�ȡ
			{
				Sleep(20);
				continue;
			}
			if (bFrameInput)
			{
				bFrameInput = false;
				if (!pThis->ParserFrame(&pFrameBuffer, nDataLength, &Parser))
					break;
				nInputResult = pThis->InputStream((byte *)Parser.pHeader, Parser.nFrameSize, 0);
				switch (nInputResult)
				{
				case IPC_Succeed:
				case IPC_Error_InvalidFrameType:
				default:
					bFrameInput = true;
					break;
				case IPC_Error_FrameCacheIsFulled:	// ����������
					bFrameInput = false;
					Sleep(10);
					break;
				}
			}
			else
			{
				nInputResult = pThis->InputStream((byte *)Parser.pHeader, Parser.nFrameSize);
				switch (nInputResult)
				{
				case IPC_Succeed:
				case IPC_Error_InvalidFrameType:
				default:
					bFrameInput = true;
					break;
				case IPC_Error_FrameCacheIsFulled:	// ����������
					bFrameInput = false;
					Sleep(10);
					break;
				}
			}
		}
		// �������ݳ�ΪnDataLength
		memmove(pBuffer, pFrameBuffer, nDataLength);
#ifdef _DEBUG
		ZeroMemory(&pBuffer[nDataLength], nBufferSize - nDataLength);
#endif
		// ���ǵ������������̣߳�����Ҫ�ݻ���ȡ����
		// 			if (!pThis->m_hWnd )
		// 			{
		// 				Sleep(10);
		// 			}
	}
	return 0;
}

int CIPCPlayer::EnableStreamParser(IPC_CODEC nCodec)
{
	if (m_pStreamParser || m_hThreadStreamParser)
		return IPC_Error_StreamParserExisted;
	m_pStreamParser = make_shared<CStreamParser>();
	AVCodecID nAvCodec = AV_CODEC_ID_NONE;
	switch (nCodec)
	{
	case CODEC_H264:
		nAvCodec = AV_CODEC_ID_H264;
		break;
	case CODEC_H265:
		nAvCodec = AV_CODEC_ID_H265;
		break;
	default:
		return IPC_Error_UnsupportedCodec;
	}
	int nError = m_pStreamParser->InitStreamParser(nAvCodec);
	if (nError != IPC_Succeed)
		return nError;

	return IPC_Succeed;
}

///< @brief ��Ƶ�������߳�
UINT __stdcall CIPCPlayer::ThreadStreamParser(void *p)
{
	CIPCPlayer* pThis = (CIPCPlayer *)p;
	return 0;
// 	int nBufferLength = 0;
// 	bool bGetFrame = false;
// 	bool bSleeped = false;
// 	list<AVPacketPtr> listFrame;
// 	while (pThis->m_bStreamParserRun)
// 	{
// 		bSleeped = false;
// 		if (pThis->m_pStreamParser->ParserFrame(listFrame) > 0)
// 		{
// 			for (auto it = listFrame.begin(); pThis->m_bStreamParserRun && it != listFrame.end();)
// 			{
// 				bSleeped = false;
// 				
// 				if (pThis->InputStream((*it)->data, IPC_I_FRAME, (*it)->size, 0, 0) == IPC_Succeed)
// 				{
// 					it = listFrame.erase(it);
// 					pThis->m_pStreamParser->SetFrameCahceFulled(false);
// 				}
// 				else
// 				{
// 					pThis->m_pStreamParser->SetFrameCahceFulled(true);
// 					bSleeped = true;
// 					Sleep(10);
// 					continue;
// 				}
// 			}
// 		}
// 		if (!bSleeped)
// 			Sleep(10);
// 	}
// 	
// 	return 0;
}

// ̽����Ƶ��������,Ҫ���������I֡
bool CIPCPlayer::ProbeStream(byte *szFrameBuffer, int nBufferLength)
{
	shared_ptr<CVideoDecoder>pDecodec = make_shared<CVideoDecoder>();
	if (!m_pStreamProbe)
		m_pStreamProbe = make_shared<StreamProbe>();

	if (!m_pStreamProbe)
		return false;
	m_pStreamProbe->nProbeCount++;
	m_pStreamProbe->GetProbeStream(szFrameBuffer, nBufferLength);
	if (m_pStreamProbe->nProbeDataLength <= 64)
		return false;
	if (pDecodec->ProbeStream(this, ReadAvData, m_nMaxFrameSize) != 0)
	{
		OutputMsg("%s Failed in ProbeStream,you may need to input more stream.\n", __FUNCTION__);
		pDecodec->CancelProbe();
		//assert(false);
		return false;
	}
	pDecodec->CancelProbe();
	if (pDecodec->m_nCodecId == AV_CODEC_ID_NONE)
	{
		OutputMsg("%s Unknown Video Codec or not found any codec in the stream.\n", __FUNCTION__);
		assert(false);
		return false;
	}

	if (!pDecodec->m_pAVCtx->width || !pDecodec->m_pAVCtx->height)
	{
		assert(false);
		return false;
	}
	if (pDecodec->m_nCodecId == AV_CODEC_ID_H264)
	{
		m_nVideoCodec = CODEC_H264;
		OutputMsg("%s Video Codec:%H.264 Width = %d\tHeight = %d.\n", __FUNCTION__, pDecodec->m_pAVCtx->width, pDecodec->m_pAVCtx->height);
	}
	else if (pDecodec->m_nCodecId == AV_CODEC_ID_HEVC)
	{
		m_nVideoCodec = CODEC_H265;
		OutputMsg("%s Video Codec:%H.265 Width = %d\tHeight = %d.\n", __FUNCTION__, pDecodec->m_pAVCtx->width, pDecodec->m_pAVCtx->height);
	}
	else
	{
		m_nVideoCodec = CODEC_UNKNOWN;
		OutputMsg("%s Unsupported Video Codec.\n", __FUNCTION__);
		assert(false);
		return false;
	}
	m_pStreamProbe->nProbeAvCodecID = pDecodec->m_nCodecId;
	m_pStreamProbe->nProbeWidth = pDecodec->m_pAVCtx->width;
	m_pStreamProbe->nProbeHeight = pDecodec->m_pAVCtx->height;
	m_nVideoHeight = pDecodec->m_pAVCtx->height;
	m_nVideoWidth = pDecodec->m_pAVCtx->width;
	return true;
}

/// @brief ��NV12ͼ��ת��ΪYUV420Pͼ��
void CIPCPlayer::CopyNV12ToYUV420P(byte *pYV12, byte *pNV12[2], int src_pitch[2], unsigned width, unsigned height)
{
	byte* dstV = pYV12 + width*height;
	byte* dstU = pYV12 + width*height / 4;
	UINT heithtUV = height / 2;
	UINT widthUV = width / 2;
	byte *pSrcUV = pNV12[1];
	byte *pSrcY = pNV12[0];
	int &nYpitch = src_pitch[0];
	int &nUVpitch = src_pitch[1];

	// ����Y����
	for (int i = 0; i < height; i++)
		memcpy(pYV12 + i*width, pSrcY + i*nYpitch, width);

	// ����VU����
	for (int i = 0; i < heithtUV; i++)
	{
		for (int j = 0; j < width; j++)
		{
			dstU[i*widthUV + j] = pSrcUV[i*nUVpitch + 2 * j];
			dstV[i*widthUV + j] = pSrcUV[i*nUVpitch + 2 * j + 1];
		}
	}
}

/// @brief ��DxvaӲ����NV12֡ת����YV12ͼ��
void CIPCPlayer::CopyDxvaFrame(byte *pYuv420, AVFrame *pAvFrameDXVA)
{
	if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
		return;

	IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
	D3DLOCKED_RECT lRect;
	D3DSURFACE_DESC SurfaceDesc;
	pSurface->GetDesc(&SurfaceDesc);
	HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
		return;
	}

	// Y����ͼ��
	byte *pSrcY = (byte *)lRect.pBits;

	// UV����ͼ��
	//byte *pSrcUV = (byte *)lRect.pBits + lRect.Pitch * SurfaceDesc.Height;
	byte *pSrcUV = (byte *)lRect.pBits + lRect.Pitch * pAvFrameDXVA->height;

	byte* dstY = pYuv420;
	byte* dstV = pYuv420 + pAvFrameDXVA->width*pAvFrameDXVA->height;
	byte* dstU = pYuv420 + pAvFrameDXVA->width*pAvFrameDXVA->height / 4;

	UINT heithtUV = pAvFrameDXVA->height / 2;
	UINT widthUV = pAvFrameDXVA->width / 2;

	// ����Y����
	for (int i = 0; i < pAvFrameDXVA->height; i++)
		memcpy(&dstY[i*pAvFrameDXVA->width], &pSrcY[i*lRect.Pitch], pAvFrameDXVA->width);

	// ����VU����
	for (int i = 0; i < heithtUV; i++)
	{
		for (int j = 0; j < widthUV; j++)
		{
			dstU[i*widthUV + j] = pSrcUV[i*lRect.Pitch + 2 * j];
			dstV[i*widthUV + j] = pSrcUV[i*lRect.Pitch + 2 * j + 1];
		}
	}

	pSurface->UnlockRect();
}

void CIPCPlayer::CopyDxvaFrameYV12(byte **ppYV12, int &nStrideY, int &nWidth, int &nHeight, AVFrame *pAvFrameDXVA)
{
	if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
		return;

	IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
	D3DLOCKED_RECT lRect;
	D3DSURFACE_DESC SurfaceDesc;
	pSurface->GetDesc(&SurfaceDesc);
	HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
		return;
	}

	// Y����ͼ��
	byte *pSrcY = (byte *)lRect.pBits;
	nStrideY = lRect.Pitch;
	nWidth = SurfaceDesc.Width;
	nHeight = SurfaceDesc.Height;

	int nPictureSize = lRect.Pitch*SurfaceDesc.Height;
	int nYUVSize = nPictureSize * 3 / 2;
	*ppYV12 = (byte *)_aligned_malloc(nYUVSize, 32);
#ifdef _DEBUG
	ZeroMemory(*ppYV12, nYUVSize);
#endif
	gpu_memcpy(*ppYV12, lRect.pBits, nPictureSize);

	UINT heithtUV = SurfaceDesc.Height >> 1;
	UINT widthUV = lRect.Pitch >> 1;
	byte *pSrcUV = (byte *)lRect.pBits + nPictureSize;
	byte* dstV = *ppYV12 + nPictureSize;
	byte* dstU = *ppYV12 + nPictureSize + nPictureSize / 4;
	// ����VU����
	int nOffset = 0;
	for (int i = 0; i < heithtUV; i++)
	{
		for (int j = 0; j < widthUV; j++)
		{
			dstV[nOffset / 2 + j] = pSrcUV[nOffset + 2 * j];
			dstU[nOffset / 2 + j] = pSrcUV[nOffset + 2 * j + 1];
		}
		nOffset += lRect.Pitch;
	}
	pSurface->UnlockRect();
}

void CIPCPlayer::CopyDxvaFrameNV12(byte **ppNV12, int &nStrideY, int &nWidth, int &nHeight, AVFrame *pAvFrameDXVA)
{
	if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
		return;

	IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
	D3DLOCKED_RECT lRect;
	D3DSURFACE_DESC SurfaceDesc;
	pSurface->GetDesc(&SurfaceDesc);
	HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
		return;
	}
	// Y����ͼ��
	byte *pSrcY = (byte *)lRect.pBits;
	nStrideY = lRect.Pitch;
	nWidth = SurfaceDesc.Width;
	nHeight = SurfaceDesc.Height;

	int nPictureSize = lRect.Pitch*SurfaceDesc.Height;
	int nYUVSize = nPictureSize * 3 / 2;
	*ppNV12 = (byte *)_aligned_malloc(nYUVSize, 32);
#ifdef _DEBUG
	ZeroMemory(*ppNV12, nYUVSize);
#endif
	gpu_memcpy(*ppNV12, lRect.pBits, nYUVSize);
	pSurface->UnlockRect();
}

bool CIPCPlayer::LockDxvaFrame(AVFrame *pAvFrameDXVA, byte **ppSrcY, byte **ppSrcUV, int &nPitch)
{
	if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
		return false;

	IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
	D3DLOCKED_RECT lRect;
	D3DSURFACE_DESC SurfaceDesc;
	pSurface->GetDesc(&SurfaceDesc);
	HRESULT hr = pSurface->LockRect(&lRect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		OutputMsg("%s IDirect3DSurface9::LockRect failed:hr = %08.\n", __FUNCTION__, hr);
		return false;
	}
	// Y����ͼ��
	*ppSrcY = (byte *)lRect.pBits;
	// UV����ͼ��
	//(PBYTE)SrcRect.pBits + SrcRect.Pitch * m_pDDraw->m_dwHeight;
	*ppSrcUV = (byte *)lRect.pBits + lRect.Pitch * pAvFrameDXVA->height;
	nPitch = lRect.Pitch;
	return true;
}

void CIPCPlayer::UnlockDxvaFrame(AVFrame *pAvFrameDXVA)
{
	if (pAvFrameDXVA->format != AV_PIX_FMT_DXVA2_VLD)
		return;

	IDirect3DSurface9* pSurface = (IDirect3DSurface9 *)pAvFrameDXVA->data[3];
	pSurface->UnlockRect();
}
// ��YUVC420P֡���Ƶ�YV12������
void CIPCPlayer::CopyFrameYUV420(byte *pYUV420, int nYUV420Size, AVFrame *pFrame420P)
{
	byte *pDest = pYUV420;
	int nStride = pFrame420P->width;
	int nSize = nStride * nStride;
	int nHalfSize = (nSize) >> 1;
	byte *pDestY = pDest;										// Y������ʼ��ַ

	byte *pDestU = pDest + nSize;								// U������ʼ��ַ
	int nSizeofU = nHalfSize >> 1;

	byte *pDestV = pDestU + (size_t)(nHalfSize >> 1);			// V������ʼ��ַ
	int nSizeofV = nHalfSize >> 1;

	// YUV420P��U��V�����Ե������ΪYV12��ʽ
	// ����Y����
	for (int i = 0; i < pFrame420P->height; i++)
		memcpy_s(pDestY + i * nStride, nSize * 3 / 2 - i*nStride, pFrame420P->data[0] + i * pFrame420P->linesize[0], pFrame420P->width);

	// ����YUV420P��U������Ŀ���YV12��U����
	for (int i = 0; i < pFrame420P->height / 2; i++)
		memcpy_s(pDestU + i * nStride / 2, nSizeofU - i*nStride / 2, pFrame420P->data[1] + i * pFrame420P->linesize[1], pFrame420P->width / 2);

	// ����YUV420P��V������Ŀ���YV12��V����
	for (int i = 0; i < pFrame420P->height / 2; i++)
		memcpy_s(pDestV + i * nStride / 2, nSizeofV - i*nStride / 2, pFrame420P->data[2] + i * pFrame420P->linesize[2], pFrame420P->width / 2);
}

void CIPCPlayer::ProcessYUVFilter(AVFrame *pAvFrame, LONGLONG nTimestamp)
{
	if (m_csYUVFilter.TryLock())
	{// ��m_pfnYUVFileter�У��û���Ҫ��YUV���ݴ����֣��ٷֳ�YUV����
		if (m_pfnYUVFilter)
		{
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// dxva Ӳ����֡
				CopyDxvaFrame(m_pYUV, pAvFrame);
				byte* pU = m_pYUV + pAvFrame->width*pAvFrame->height;
				byte* pV = m_pYUV + pAvFrame->width*pAvFrame->height / 4;
				m_pfnYUVFilter(this,
					m_pYUV,
					pU,
					pV,
					pAvFrame->width,
					pAvFrame->width / 2,
					pAvFrame->width,
					pAvFrame->height,
					nTimestamp,
					m_pUserYUVFilter);
			}
			else
				m_pfnYUVFilter(this,
				pAvFrame->data[0],
				pAvFrame->data[1],
				pAvFrame->data[2],
				pAvFrame->linesize[0],
				pAvFrame->linesize[1],
				pAvFrame->width,
				pAvFrame->height,
				nTimestamp,
				m_pUserYUVFilter);
		}
		m_csYUVFilter.Unlock();
	}
}

void CIPCPlayer::ProcessYUVCapture(AVFrame *pAvFrame, LONGLONG nTimestamp)
{
	if (m_csCaptureYUV.TryLock())
	{
		if (m_pfnCaptureYUV)
		{
			int nPictureSize = 0;
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// Ӳ���뻷����,m_pYUV�ڴ���Ҫ�������룬����ߴ�
				int nStrideY = 0;
				int nWidth = 0, nHeight = 0;
				CopyDxvaFrameNV12(&m_pYUV, nStrideY, nWidth, nHeight, pAvFrame);
				m_nYUVSize = nStrideY*nHeight * 3 / 2;
				m_pYUVPtr = shared_ptr<byte>(m_pYUV, _aligned_free);
				m_pfnCaptureYUV(this, m_pYUV, m_nYUVSize, nStrideY, nStrideY >> 1, nWidth, nHeight, nTimestamp, m_pUserCaptureYUV);
			}
			else
			{
				nPictureSize = pAvFrame->linesize[0] * pAvFrame->height;
				int nUVSize = nPictureSize / 2;
				if (!m_pYUV)
				{
					m_nYUVSize = nPictureSize * 3 / 2;
					m_pYUV = (byte *)_aligned_malloc(m_nYUVSize, 32);
					m_pYUVPtr = shared_ptr<byte>(m_pYUV, _aligned_free);
				}
				memcpy(m_pYUV, pAvFrame->data[0], nPictureSize);
				memcpy(&m_pYUV[nPictureSize], pAvFrame->data[1], nUVSize / 2);
				memcpy(&m_pYUV[nPictureSize + nUVSize / 2], pAvFrame->data[2], nUVSize / 2);
				m_pfnCaptureYUV(this, m_pYUV, m_nYUVSize, pAvFrame->linesize[0], pAvFrame->linesize[1], pAvFrame->width, pAvFrame->height, nTimestamp, m_pUserCaptureYUV);
			}
			//TraceMsgA("%s m_pfnCaptureYUV = %p", __FUNCTION__, m_pfnCaptureYUV);
		}
		m_csCaptureYUV.Unlock();
	}
	if (m_csCaptureYUVEx.TryLock())
	{
		if (m_pfnCaptureYUVEx)
		{
			if (!m_pYUV)
			{
				m_nYUVSize = pAvFrame->width * pAvFrame->height * 3 / 2;
				m_pYUV = (byte *)av_malloc(m_nYUVSize);
				m_pYUVPtr = shared_ptr<byte>(m_pYUV, av_free);
			}
			if (pAvFrame->format == AV_PIX_FMT_DXVA2_VLD)
			{// dxva Ӳ����֡
				//CopyDxvaFrameNV12(m_pYUV, pAvFrame);
				byte *pY = NULL;
				byte *pUV = NULL;
				int nPitch = 0;
				LockDxvaFrame(pAvFrame, &pY, &pUV, nPitch);
				byte* pU = m_pYUV + pAvFrame->width*pAvFrame->height;
				byte* pV = m_pYUV + pAvFrame->width*pAvFrame->height / 4;

				m_pfnCaptureYUVEx(this,
					pY,
					pUV,
					NULL,
					nPitch,
					nPitch / 2,
					pAvFrame->width,
					pAvFrame->height,
					nTimestamp,
					m_pUserCaptureYUVEx);
				UnlockDxvaFrame(pAvFrame);
			}
			else
			{
				m_pfnCaptureYUVEx(this,
					pAvFrame->data[0],
					pAvFrame->data[1],
					pAvFrame->data[2],
					pAvFrame->linesize[0],
					pAvFrame->linesize[1],
					pAvFrame->width,
					pAvFrame->height,
					nTimestamp,
					m_pUserCaptureYUVEx);
			}
		}
		m_csCaptureYUVEx.Unlock();
	}
	if (m_csCaptureRGB.TryLock())
	{
		if (m_pCaptureRGB)
		{
			PixelConvert convert(pAvFrame, D3DFMT_R8G8B8);
			if (convert.ConvertPixel() > 0)
				m_pCaptureRGB(this, convert.pImage, pAvFrame->width, pAvFrame->height, nTimestamp, m_pUserCaptureRGB);
		}
			
		m_csCaptureRGB.Unlock();
	}
}

/// @brief			����̽���ȡ���ݰ��ص�����
/// @param [in]		opaque		�û�����Ļص���������ָ��
/// @param [in]		buf			��ȡ���ݵĻ���
/// @param [in]		buf_size	����ĳ���
/// @return			ʵ�ʶ�ȡ���ݵĳ���
int CIPCPlayer::ReadAvData(void *opaque, uint8_t *buf, int buf_size)
{
	AvQueue *pAvQueue = (AvQueue *)opaque;
	CIPCPlayer *pThis = (CIPCPlayer *)pAvQueue->pUserData;

	int nReturnVal = buf_size;
	int nRemainedLength = 0;
	pAvQueue->pAvBuffer = buf;
	if (!pThis->m_pStreamProbe)
		return 0;
	int &nDataLength = pThis->m_pStreamProbe->nProbeDataRemained;
	byte *pProbeBuff = pThis->m_pStreamProbe->pProbeBuff;
	int &nProbeOffset = pThis->m_pStreamProbe->nProbeOffset;
	if (nDataLength > 0)
	{
		nRemainedLength = nDataLength - nProbeOffset;
		if (nRemainedLength > buf_size)
		{
			memcpy_s(buf, buf_size, &pProbeBuff[nProbeOffset], buf_size);
			nProbeOffset += buf_size;
			nDataLength -= buf_size;
		}
		else
		{
			memcpy_s(buf, buf_size, &pProbeBuff[nProbeOffset], nRemainedLength);
			nDataLength -= nRemainedLength;
			nProbeOffset = 0;
			nReturnVal = nRemainedLength;
		}
		return nReturnVal;
	}
	else
		return 0;
}


UINT __stdcall CIPCPlayer::ThreadPlayAudioGSJ(void *p)
{
	CIPCPlayer *pThis = (CIPCPlayer *)p;
	int nAudioFrameInterval = pThis->m_fPlayInterval / 2;

	DWORD nResult = 0;
	int nTimeSpan = 0;
	StreamFramePtr FramePtr;
	int nAudioError = 0;
	byte *pPCM = nullptr;
	shared_ptr<CAudioDecoder> pAudioDecoder = make_shared<CAudioDecoder>();
	int nPCMSize = 0;
	int nDecodeSize = 0;
	__int64 nFrameEvent = 0;
	if (pThis->m_nAudioPlayFPS == 8)
		Sleep(250);
	// Ԥ����һ֡���Գ�ʼ����Ƶ������
	while (pThis->m_bThreadPlayAudioRun)
	{
		if (!FramePtr)
		{
			CAutoLock lock(&pThis->m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (pThis->m_listAudioCache.size() > 0)
			{
				FramePtr = pThis->m_listAudioCache.front();
				break;
			}
		}
		Sleep(10);
	}
	if (!FramePtr)
		return 0;
	if (pAudioDecoder->GetCodecType() == CODEC_UNKNOWN)
	{
		const IPCFrameHeaderEx *pHeader = FramePtr->FrameHeader();
		nDecodeSize = pHeader->nLength * 2;		//G711 ѹ����Ϊ2��
		switch (pHeader->nType)
		{
		case FRAME_G711A:			//711 A�ɱ���֡
		{
			pAudioDecoder->SetACodecType(CODEC_G711A, SampleBit16);
			pThis->m_nAudioCodec = CODEC_G711A;
			pThis->OutputMsg("%s Audio Codec:G711A.\n", __FUNCTION__);
			break;
		}
		case FRAME_G711U:			//711 U�ɱ���֡
		{
			pAudioDecoder->SetACodecType(CODEC_G711U, SampleBit16);
			pThis->m_nAudioCodec = CODEC_G711U;
			pThis->OutputMsg("%s Audio Codec:G711U.\n", __FUNCTION__);
			break;
		}

		case FRAME_G726:			//726����֡
		{
			// ��ΪĿǰIPC�����G726����,��Ȼ���õ���16λ��������ʹ��32λѹ�����룬��˽�ѹ��ʹ��SampleBit32
			pAudioDecoder->SetACodecType(CODEC_G726, SampleBit32);
			pThis->m_nAudioCodec = CODEC_G726;
			nDecodeSize = FramePtr->FrameHeader()->nLength * 8;		//G726���ѹ���ʿɴ�8��
			pThis->OutputMsg("%s Audio Codec:G726.\n", __FUNCTION__);
			break;
		}
		case FRAME_AAC:				//AAC����֡��
		{
			pAudioDecoder->SetACodecType(CODEC_AAC, SampleBit16);
			pThis->m_nAudioCodec = CODEC_AAC;
			nDecodeSize = FramePtr->FrameHeader()->nLength * 24;
			pThis->OutputMsg("%s Audio Codec:AAC.\n", __FUNCTION__);
			break;
		}
		default:
		{
			assert(false);
			pThis->OutputMsg("%s Unspported audio codec.\n", __FUNCTION__);
			return 0;
			break;
		}
		}
	}
	if (nPCMSize < nDecodeSize)
	{
		pPCM = new byte[nDecodeSize];
		nPCMSize = nDecodeSize;
	}
#ifdef _DEBUG
	TimeTrace TimeAudio("AudioTime", __FUNCTION__);
#endif
	double dfLastPlayTime = GetExactTime();
	double dfPlayTimeSpan = 0.0f;
	UINT nFramesPlayed = 0;
	WaitForSingleObject(pThis->m_hEventDecodeStart, 1000);

	pThis->m_csAudioCache.Lock();
	pThis->m_nAudioCache = pThis->m_listAudioCache.size();
	pThis->m_csAudioCache.Unlock();
	TraceMsgA("%s Audio Cache Size = %d.\n", __FUNCTION__, pThis->m_nAudioCache);
	time_t tLastFrameTime = 0;
	double dfDecodeStart = GetExactTime();
	DWORD dwOsMajorVersion = GetOsMajorVersion();
#ifdef _DEBUG
	int nSleepCount = 0;
	TimeTrace TraceSleepCount("SleepCount", __FUNCTION__, 25);
#endif
	while (pThis->m_bThreadPlayAudioRun)
	{
		if (pThis->m_bPause)
		{
			if (pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StopPlay();
			Sleep(100);
			continue;
		}

		nTimeSpan = (int)((GetExactTime() - dfLastPlayTime) * 1000);
		if (pThis->m_fPlayRate != 1.0f)
		{// ֻ���������ʲŲ�������
			if (pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StopPlay();
			pThis->m_csAudioCache.Lock();
			if (pThis->m_listAudioCache.size() > 0)
				pThis->m_listAudioCache.pop_front();
			pThis->m_csAudioCache.Unlock();
			Sleep(5);
			continue;
		}

		if (nTimeSpan > 1000 * 3 / pThis->m_nAudioPlayFPS)			// ����3*��Ƶ�������û����Ƶ���ݣ�����Ϊ��Ƶ��ͣ
			pThis->m_pDsBuffer->StopPlay();
		else if (!pThis->m_pDsBuffer->IsPlaying())
			pThis->m_pDsBuffer->StartPlay();
		bool bPopFrame = false;
		if (pThis->m_bIpcStream && pThis->m_nAudioPlayFPS == 8)
		{
			if (pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->WaitForPosNotify();
		}

		pThis->m_csAudioCache.Lock();
		if (pThis->m_listAudioCache.size() > 0)
		{
			FramePtr = pThis->m_listAudioCache.front();
			pThis->m_listAudioCache.pop_front();
			bPopFrame = true;
		}
		pThis->m_nAudioCache = pThis->m_listAudioCache.size();
		pThis->m_csAudioCache.Unlock();

		if (!bPopFrame)
		{
			if (!pThis->m_bIpcStream)
				Sleep(10);
			continue;
		}

		if (nFramesPlayed < 50 && dwOsMajorVersion < 6)
		{// ������XPϵͳ�У�ǰ50֡�ᱻ˲�䶪��������
			if (((TimeSpanEx(dfLastPlayTime) + dfPlayTimeSpan) * 1000) < nAudioFrameInterval)
				Sleep(nAudioFrameInterval - (TimeSpanEx(dfLastPlayTime) * 1000));
		}

		if (pThis->m_pDsBuffer->IsPlaying() //||
			/*pThis->m_pDsBuffer->WaitForPosNotify()*/)
		{
			if (pAudioDecoder->Decode(pPCM, nPCMSize, (byte *)FramePtr->Framedata(pThis->m_nSDKVersion), FramePtr->FrameHeader()->nLength) != 0)
			{
				if (!pThis->m_pDsBuffer->WritePCM(pPCM, nPCMSize, !pThis->m_bIpcStream))
					pThis->OutputMsg("%s Write PCM Failed.\n", __FUNCTION__);
				//SetEvent(pThis->m_hAudioFrameEvent[nFrameEvent++ % 2]);
			}
			else
				TraceMsgA("%s Audio Decode Failed Is.\n", __FUNCTION__);
		}
		nFramesPlayed++;
		if (pThis->m_nAudioPlayFPS == 8 && nFramesPlayed <= 8)
			Sleep(120);
		dfPlayTimeSpan = TimeSpanEx(dfLastPlayTime);
		dfLastPlayTime = GetExactTime();
		tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
	}
	if (pPCM)
		delete[]pPCM;
	return 0;
}

UINT __stdcall CIPCPlayer::ThreadPlayAudioIPC(void *p)
{
	CIPCPlayer *pThis = (CIPCPlayer *)p;
	int nAudioFrameInterval = pThis->m_fPlayInterval / 2;

	DWORD nResult = 0;
	int nTimeSpan = 0;
	StreamFramePtr FramePtr;
	int nAudioError = 0;
	byte *pPCM = nullptr;
	shared_ptr<CAudioDecoder> pAudioDecoder = make_shared<CAudioDecoder>();
	int nPCMSize = 0;
	int nDecodeSize = 0;
	__int64 nFrameEvent = 0;

	// Ԥ����һ֡���Գ�ʼ����Ƶ������
	while (pThis->m_bThreadPlayAudioRun)
	{
		if (!FramePtr)
		{
			CAutoLock lock(&pThis->m_csAudioCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (pThis->m_listAudioCache.size() > 0)
			{
				FramePtr = pThis->m_listAudioCache.front();
				break;
			}
		}
		Sleep(10);
	}
	if (!FramePtr)
		return 0;
	if (pAudioDecoder->GetCodecType() == CODEC_UNKNOWN)
	{
		const IPCFrameHeaderEx *pHeader = FramePtr->FrameHeader();
		nDecodeSize = pHeader->nLength * 2;		//G711 ѹ����Ϊ2��
		switch (pHeader->nType)
		{
		case FRAME_G711A:			//711 A�ɱ���֡
		{
			pAudioDecoder->SetACodecType(CODEC_G711A, SampleBit16);
			pThis->m_nAudioCodec = CODEC_G711A;
			pThis->OutputMsg("%s Audio Codec:G711A.\n", __FUNCTION__);
			break;
		}
		case FRAME_G711U:			//711 U�ɱ���֡
		{
			pAudioDecoder->SetACodecType(CODEC_G711U, SampleBit16);
			pThis->m_nAudioCodec = CODEC_G711U;
			pThis->OutputMsg("%s Audio Codec:G711U.\n", __FUNCTION__);
			break;
		}

		case FRAME_G726:			//726����֡
		{
			// ��ΪĿǰIPC�����G726����,��Ȼ���õ���16λ��������ʹ��32λѹ�����룬��˽�ѹ��ʹ��SampleBit32
			pAudioDecoder->SetACodecType(CODEC_G726, SampleBit32);
			pThis->m_nAudioCodec = CODEC_G726;
			nDecodeSize = FramePtr->FrameHeader()->nLength * 8;		//G726���ѹ���ʿɴ�8��
			pThis->OutputMsg("%s Audio Codec:G726.\n", __FUNCTION__);
			break;
		}
		case FRAME_AAC:				//AAC����֡��
		{
			pAudioDecoder->SetACodecType(CODEC_AAC, SampleBit16);
			pThis->m_nAudioCodec = CODEC_AAC;
			nDecodeSize = FramePtr->FrameHeader()->nLength * 24;
			pThis->OutputMsg("%s Audio Codec:AAC.\n", __FUNCTION__);
			break;
		}
		default:
		{
			assert(false);
			pThis->OutputMsg("%s Unspported audio codec.\n", __FUNCTION__);
			return 0;
			break;
		}
		}
	}
	if (nPCMSize < nDecodeSize)
	{
		pPCM = new byte[nDecodeSize];
		nPCMSize = nDecodeSize;
	}
	double dfLastPlayTime = 0.0f;
	double dfPlayTimeSpan = 0.0f;
	UINT nFramesPlayed = 0;
	WaitForSingleObject(pThis->m_hEventDecodeStart, 1000);

	pThis->m_csAudioCache.Lock();
	pThis->m_nAudioCache = pThis->m_listAudioCache.size();
	pThis->m_csAudioCache.Unlock();

	TraceMsgA("%s Audio Cache Size = %d.\n", __FUNCTION__, pThis->m_nAudioCache);
	time_t tLastFrameTime = 0;
	double dfDecodeStart = GetExactTime();
	DWORD dwOsMajorVersion = GetOsMajorVersion();
	while (pThis->m_bThreadPlayAudioRun)
	{
		if (pThis->m_bPause)
		{
			if (pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StopPlay();
			Sleep(20);
			continue;
		}

		nTimeSpan = (int)((GetExactTime() - dfLastPlayTime) * 1000);
		if (pThis->m_fPlayRate != 1.0f)
		{// ֻ���������ʲŲ�������
			if (pThis->m_pDsBuffer->IsPlaying())
				pThis->m_pDsBuffer->StopPlay();
			pThis->m_csAudioCache.Lock();
			if (pThis->m_listAudioCache.size() > 0)
				pThis->m_listAudioCache.pop_front();
			pThis->m_csAudioCache.Unlock();
			Sleep(5);
			continue;
		}

		if (nTimeSpan > 100)			// ����100msû����Ƶ���ݣ�����Ϊ��Ƶ��ͣ
			pThis->m_pDsBuffer->StopPlay();
		else if (!pThis->m_pDsBuffer->IsPlaying())
			pThis->m_pDsBuffer->StartPlay();
		bool bPopFrame = false;

		// 			if (!pThis->m_pAudioPlayEvent->GetNotify(1))
		// 			{
		// 				continue;
		// 			}
		pThis->m_csAudioCache.Lock();
		if (pThis->m_listAudioCache.size() > 0)
		{
			FramePtr = pThis->m_listAudioCache.front();
			pThis->m_listAudioCache.pop_front();
			bPopFrame = true;
		}
		pThis->m_nAudioCache = pThis->m_listAudioCache.size();
		pThis->m_csAudioCache.Unlock();

		if (!bPopFrame)
		{
			Sleep(10);
			continue;
		}
		nFramesPlayed++;

		if (nFramesPlayed < 50 && dwOsMajorVersion < 6)
		{// ������XPϵͳ�У�ǰ50֡�ᱻ˲�䶪��������
			if (((TimeSpanEx(dfLastPlayTime) + dfPlayTimeSpan) * 1000) < nAudioFrameInterval)
				Sleep(nAudioFrameInterval - (TimeSpanEx(dfLastPlayTime) * 1000));
		}

		dfPlayTimeSpan = GetExactTime();
		if (pThis->m_pDsBuffer->IsPlaying())
		{
			if (pAudioDecoder->Decode(pPCM, nPCMSize, (byte *)FramePtr->Framedata(pThis->m_nSDKVersion), FramePtr->FrameHeader()->nLength) != 0)
			{
				if (!pThis->m_pDsBuffer->WritePCM(pPCM, nPCMSize))
					pThis->OutputMsg("%s Write PCM Failed.\n", __FUNCTION__);
				SetEvent(pThis->m_hAudioFrameEvent[nFrameEvent++ % 2]);
			}
			else
				TraceMsgA("%s Audio Decode Failed Is.\n", __FUNCTION__);
		}
		dfPlayTimeSpan = TimeSpanEx(dfPlayTimeSpan);
		dfLastPlayTime = GetExactTime();
		tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
	}
	if (pPCM)
		delete[]pPCM;
	return 0;
}

bool CIPCPlayer::InitialziePlayer()
{
	if (m_nVideoCodec == CODEC_UNKNOWN ||		/// ����δ֪����̽����
		!m_nVideoWidth ||
		!m_nVideoHeight)
	{
		return false;
	}

	if (!m_pDecoder)
	{
		m_pDecoder = make_shared<CVideoDecoder>();
		if (!m_pDecoder)
		{
			OutputMsg("%s Failed in allocing memory for Decoder.\n", __FUNCTION__);
			return false;
		}
	}

	if (!InitizlizeDx())
	{
		assert(false);
		return false;
	}
	if (m_bD3dShared)
	{
		m_pDecoder->SetD3DShared(m_pDxSurface->GetD3D9(), m_pDxSurface->GetD3DDevice());
		m_pDxSurface->SetD3DShared(true);
	}

	// ʹ�õ��߳̽���,���߳̽�����ĳ�˱Ƚ�����CPU�Ͽ��ܻ����Ч����������I5 2GHZ���ϵ�CPU�ϵĶ��߳̽���Ч���������Է�����ռ�ø�����ڴ�
	m_pDecoder->SetDecodeThreads(1);
	// ��ʼ��������
	AVCodecID nCodecID = AV_CODEC_ID_NONE;
	switch (m_nVideoCodec)
	{
	case CODEC_H264:
		nCodecID = AV_CODEC_ID_H264;
		break;
	case CODEC_H265:
		nCodecID = AV_CODEC_ID_H265;
		break;
	default:
	{
		OutputMsg("%s You Input a unknown stream,Decode thread exit.\n", __FUNCTION__);
		return false;
		break;
	}
	}

	if (!m_pDecoder->InitDecoder(nCodecID, m_nVideoWidth, m_nVideoHeight, m_bEnableHaccel))
	{
		OutputMsg("%s Failed in Initializing Decoder.\n", __FUNCTION__);
#ifdef _DEBUG
		OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, m_nObjIndex, __LINE__, timeGetTime() - m_nLifeTime);
#endif
		return false;
	}
	return true;
}

UINT __stdcall CIPCPlayer::ThreadDecode(void *p)
{
	struct DxDeallocator
	{
		CDxSurfaceEx *&m_pDxSurface;
		CDirectDraw *&m_pDDraw;

	public:
		DxDeallocator(CDxSurfaceEx *&pDxSurface, CDirectDraw *&pDDraw)
			:m_pDxSurface(pDxSurface), m_pDDraw(pDDraw)
		{
		}
		~DxDeallocator()
		{
			TraceMsgA("%s pSurface = %08X\tpDDraw = %08X.\n", __FUNCTION__, m_pDxSurface, m_pDDraw);
			Safe_Delete(m_pDxSurface);
			Safe_Delete(m_pDDraw);
		}
	};
	DeclareRunTime(5);
	CIPCPlayer* pThis = (CIPCPlayer *)p;
#ifdef _DEBUG
	pThis->OutputMsg("%s \tObject:%d Enter ThreadPlayVideo m_nLifeTime = %d.\n", __FUNCTION__, pThis->m_nObjIndex, timeGetTime() - pThis->m_nLifeTime);
#endif
	int nAvError = 0;
	char szAvError[1024] = { 0 };

	if (!pThis->m_hRenderWnd)
		pThis->OutputMsg("%s Warning!!!A Windows handle is Needed otherwith the video Will not showed..\n", __FUNCTION__);
	// ������ý���¼�
	//TimerEvent PlayEvent(1000 / pThis->m_nVideoFPS);
	int nIPCPlayInterval = 1000 / pThis->m_nVideoFPS;
	shared_ptr<CMMEvent> pRenderTimer = make_shared<CMMEvent>(pThis->m_hRenderAsyncEvent, nIPCPlayInterval);

	// �ȴ���Ч����Ƶ֡����
	long tFirst = timeGetTime();
	int nTimeoutCount = 0;
	while (pThis->m_bThreadDecodeRun)
	{
		Autolock(&pThis->m_csVideoCache);
		// 			if ((timeGetTime() - tFirst) > 5000)
		// 			{// �ȴ���ʱ
		// 				//assert(false);
		// 				pThis->OutputMsg("%s Warning!!!Wait for frame timeout(5s),times %d.\n", __FUNCTION__,++nTimeoutCount);
		// 				break;
		// 			}
		if (pThis->m_listVideoCache.size() < 1)
		{
			lock.Unlock();
			Sleep(20);
			continue;
		}
		else
			break;
	}
	SaveRunTime();
	if (!pThis->m_bThreadDecodeRun)
	{
		//assert(false);
		return 0;
	}

	// �ȴ�I֡
	tFirst = timeGetTime();
	//		DWORD dfTimeout = 3000;
	// 		if (!pThis->m_bIpcStream)	// ֻ��IPC��������Ҫ��ʱ��ȴ�
	// 			dfTimeout = 1000;
	AVCodecID nCodecID = AV_CODEC_ID_NONE;
	int nDiscardFrames = 0;
	bool bProbeSucced = false;
	if (pThis->m_nVideoCodec == CODEC_UNKNOWN ||		/// ����δ֪����̽����
		!pThis->m_nVideoWidth ||
		!pThis->m_nVideoHeight)
	{
		bool bGovInput = false;
		while (pThis->m_bThreadDecodeRun)
		{
			if ((timeGetTime() - tFirst) >= pThis->m_nProbeStreamTimeout)
				break;
			CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (pThis->m_listVideoCache.size() > 1)
				break;
			Sleep(25);
		}
		if (!pThis->m_bThreadDecodeRun)
		{
			assert(false);
			return 0;
		}
		auto itStart = pThis->m_listVideoCache.begin();
		while (!bProbeSucced && pThis->m_bThreadDecodeRun)
		{
#ifndef _DEBUG
			if ((timeGetTime() - tFirst) < pThis->m_nProbeStreamTimeout)
#else
			if ((timeGetTime() - tFirst) < INFINITE)
#endif
			{
				Sleep(5);
				CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
				//auto it = find_if(itPos, pThis->m_listVideoCache.end(), StreamFrame::IsIFrame);
				auto it = itStart;
				if (it != pThis->m_listVideoCache.end())
				{// ̽����������
					itStart = it;
					itStart++;
					TraceMsgA("%s Probestream FrameType = %d\tFrameLength = %d.\n", __FUNCTION__, (*it)->FrameHeader()->nType, (*it)->FrameHeader()->nLength);
					if ((*it)->FrameHeader()->nType == FRAME_GOV)
					{
						if (bGovInput)
							continue;
						bGovInput = true;
						if (bProbeSucced = pThis->ProbeStream((byte *)(*it)->Framedata(pThis->m_nSDKVersion), (*it)->FrameHeader()->nLength))
							break;
					}
					else
						if (bProbeSucced = pThis->ProbeStream((byte *)(*it)->Framedata(pThis->m_nSDKVersion), (*it)->FrameHeader()->nLength))
							break;
				}
			}
			else
			{
#ifdef _DEBUG
				pThis->OutputMsg("%s Warning!!!\nThere is No an I frame in %d second.m_listVideoCache.size() = %d.\n", __FUNCTION__, (int)pThis->m_nProbeStreamTimeout / 1000, pThis->m_listVideoCache.size());
				pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
				if (pThis->m_hRenderWnd)
					::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_NOTRECVIFRAME, 0);
				assert(false);
				return 0;
			}
		}
		if (!pThis->m_bThreadDecodeRun)
		{
			assert(false);
			return 0;
		}

		if (!bProbeSucced)		// ̽��ʧ��
		{
			pThis->OutputMsg("%s Failed in ProbeStream,you may input a unknown stream.\n", __FUNCTION__);
#ifdef _DEBUG
			pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
			if (pThis->m_hRenderWnd)
				::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNKNOWNSTREAM, 0);
			assert(false);
			return 0;
		}
		// ��ffmpeg������IDתΪIPC������ID,����ֻ֧��H264��HEVC
		nCodecID = pThis->m_pStreamProbe->nProbeAvCodecID;
		if (nCodecID != AV_CODEC_ID_H264 &&
			nCodecID != AV_CODEC_ID_HEVC)
		{
			pThis->m_nVideoCodec = CODEC_UNKNOWN;
			pThis->OutputMsg("%s Probed a unknown stream,Decode thread exit.\n", __FUNCTION__);
			if (pThis->m_hRenderWnd)
				::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNKNOWNSTREAM, 0);
			assert(false);
			return 0;
		}
	}
	SaveRunTime();
	switch (pThis->m_nVideoCodec)
	{
	case CODEC_H264:
		nCodecID = AV_CODEC_ID_H264;
		break;
	case CODEC_H265:
		nCodecID = AV_CODEC_ID_H265;
		break;
	default:
	{
		pThis->OutputMsg("%s You Input a unknown stream,Decode thread exit.\n", __FUNCTION__);
		if (pThis->m_hRenderWnd)	// ���߳��о�������ʹ��SendMessage����Ϊ���ܻᵼ������
			::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNSURPPORTEDSTREAM, 0);
		assert(false);
		return 0;
		break;
	}
	}

	int nRetry = 0;

	shared_ptr<CVideoDecoder>pDecodec = make_shared<CVideoDecoder>();
	if (!pDecodec)
	{
		pThis->OutputMsg("%s Failed in allocing memory for Decoder.\n", __FUNCTION__);
		assert(false);
		return 0;
	}
	SaveRunTime();
	if (!pThis->InitizlizeDx())
	{
		assert(false);
		return 0;
	}

	shared_ptr<DxDeallocator> DxDeallocatorPtr = make_shared<DxDeallocator>(pThis->m_pDxSurface, pThis->m_pDDraw);
	SaveRunTime();
	if (pThis->m_bD3dShared)
	{
		pDecodec->SetD3DShared(pThis->m_pDxSurface->GetD3D9(), pThis->m_pDxSurface->GetD3DDevice());
		pThis->m_pDxSurface->SetD3DShared(true);
	}

	// ʹ�õ��߳̽���,���߳̽�����ĳ�˱Ƚ�����CPU�Ͽ��ܻ����Ч����������I5 2GHZ���ϵ�CPU�ϵĶ��߳̽���Ч���������Է�����ռ�ø�����ڴ�
	pDecodec->SetDecodeThreads(1);
	// ��ʼ��������

	while (pThis->m_bThreadDecodeRun)
	{// ĳ��ʱ����ܻ���Ϊ�ڴ����Դ�������³�ʼ�����������,��˿����ӳ�һ��ʱ����ٴγ�ʼ��������γ�ʼ���Բ��ɹ��������˳��߳�
		//DeclareRunTime(5);
		//SaveRunTime();
		if (!pDecodec->InitDecoder(nCodecID, pThis->m_nVideoWidth, pThis->m_nVideoHeight, pThis->m_bEnableHaccel))
		{
			pThis->OutputMsg("%s Failed in Initializing Decoder��CodeCID =%d,Width = %d,Height = %d,HWAccel = %d.\n", __FUNCTION__, nCodecID, pThis->m_nVideoWidth, pThis->m_nVideoHeight, pThis->m_bEnableHaccel);
#ifdef _DEBUG
			pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
			nRetry++;
			if (nRetry >= 3)
			{
				if (pThis->m_hRenderWnd)// ���߳��о�������ʹ��SendMessage����Ϊ���ܻᵼ������
					::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_INITDECODERFAILED, 0);
				return 0;
			}
			Delay(2500, pThis->m_bThreadDecodeRun);
		}
		else
			break;
		//SaveRunTime();
	}
	SaveRunTime();
	if (!pThis->m_bThreadDecodeRun)
	{
		//assert(false);
		return 0;
	}

	if (pThis->m_pStreamProbe)
		pThis->m_pStreamProbe = nullptr;

	AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
	av_init_packet(pAvPacket);
	AVFrame *pAvFrame = av_frame_alloc();
	shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);

	StreamFramePtr FramePtr;
	int nGot_picture = 0;
	DWORD nResult = 0;
	float fTimeSpan = 0;
	int nFrameInterval = pThis->m_nFileFrameInterval;
	pThis->m_dfTimesStart = GetExactTime();

	//		 ȡ�õ�ǰ��ʾ����ˢ����,�ڴ�ֱͬ��ģʽ��,��ʾ����ˢ���ʾ����ˣ���ʾͼ������֡��
	//		 ͨ��ͳ��ÿ��ʾһ֡ͼ��(���������ʾ)�ķѵ�ʱ��
	// 		DEVMODE   dm;
	// 		dm.dmSize = sizeof(DEVMODE);
	// 		::EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);		
	// 		int nRefreshInvertal = 1000 / dm.dmDisplayFrequency;	// ��ʾ��ˢ�¼��

	double dfDecodeStartTime = GetExactTime();
	double dfRenderTime = GetExactTime() - pThis->m_fPlayInterval;	// ͼ����ʾ��ʱ��
	double dfRenderStartTime = 0.0f;
	double dfRenderTimeSpan = 0.000f;
	double dfTimeSpan = 0.0f;

#ifdef _DEBUG
	pThis->m_csVideoCache.Lock();
	TraceMsgA("%s Video cache Size = %d .\n", __FUNCTION__, pThis->m_listVideoCache.size());
	pThis->m_csVideoCache.Unlock();
	pThis->OutputMsg("%s \tObject:%d Start Decoding.\n", __FUNCTION__, pThis->m_nObjIndex);
#endif
	//	    ���´������Բ��Խ������ʾռ��ʱ�䣬���鲻Ҫɾ��		
	// 		TimeTrace DecodeTimeTrace("DecodeTime", __FUNCTION__);
	// 		TimeTrace RenderTimeTrace("RenderTime", __FUNCTION__);

	int nIFrameTime = 0;
	CStat FrameStat(pThis->m_nObjIndex);		// ����ͳ��
	//CStat IFrameStat;		// I֡����ͳ��

	int nFramesAfterIFrame = 0;		// ���I֡�ı��,I֡��ĵ�һ֡Ϊ1���ڶ�֡Ϊ2��������
	int nSkipFrames = 0;
	bool bDecodeSucceed = false;
	double dfDecodeTimespan = 0.0f;	// �������ķ�ʱ��
	double dfDecodeITimespan = 0.0f; // I֡�������ʾ���ķ�ʱ��
	double dfTimeDecodeStart = 0.0f;
	pThis->m_nFirstFrameTime = 0;
	float fLastPlayRate = pThis->m_fPlayRate;		// ��¼��һ�εĲ������ʣ����������ʷ����仯ʱ����Ҫ����֡ͳ������

	if (pThis->m_dwStartTime)
	{
		TraceMsgA("%s %d Render Timespan = %d.\n", __FUNCTION__, __LINE__, timeGetTime() - pThis->m_dwStartTime);
		pThis->m_dwStartTime = 0;
	}

	int nFramesPlayed = 0;			// �����ܷ���
	double dfTimeStartPlay = GetExactTime();// ������ʼʱ��
	int nTimePlayFrame = 0;		// ����һ֡���ķ�ʱ�䣨MS��
	int nPlayCount = 0;
	int TPlayArray[100] = { 0 };
	double dfT1 = GetExactTime();
	int nVideoCacheSize = 0;
	LONG nTotalDecodeFrames = 0;
	dfDecodeStartTime = GetExactTime() - pThis->m_nPlayFrameInterval / 1000.0f;
	SaveRunTime();
	pThis->m_pDecoder = pDecodec;
	int nRenderTimes = 0;
	CStat  RenderInterval("RenderInterval", pThis->m_nObjIndex);
	while (pThis->m_bThreadDecodeRun)
	{
		if (!pThis->m_bIpcStream &&
			pThis->m_bPause)
		{// ֻ�з�IPC�����ſ�����ͣ
			Sleep(40);
			continue;
		}
		pThis->m_csVideoCache.Lock();
		nVideoCacheSize = pThis->m_listVideoCache.size();
		pThis->m_csVideoCache.Unlock();
// 		do 
// 		{
// // 		��Ϊ��֡�ʲ��Դ���,���鲻Ҫɾ��
// // 		xionggao.lee @2016.01.15 
#ifdef _DEBUG
// 		int nFPS = 25;
// 		int nTimespan2 = (int)(TimeSpanEx(pThis->m_dfTimesStart) * 1000);
// 		if (nTimespan2)
// 			nFPS = nFrames * 1000 / nTimespan2;
// 
// 		int nTimeSpan = (int)(TimeSpanEx(dfDecodeStartTime) * 1000);
// 		TPlayArray[nPlayCount++] = nTimeSpan;
// 		TPlayArray[0] = nFPS;
// 		if (nPlayCount >= 50)
// 		{
// 			pThis->OutputMsg("%sPlay Interval([0] is FPS):\n", __FUNCTION__);
// 			for (int i = 0; i < nPlayCount; i++)
// 			{
// 				pThis->OutputMsg("%02d\t", TPlayArray[i]);
// 				if ((i + 1) % 10 == 0)
// 					pThis->OutputMsg("\n");
// 			}
// 			pThis->OutputMsg(".\n");
// 			nPlayCount = 0;
// 			CAutoLock lock(&pThis->m_csVideoCache);
// 			TraceMsgA("%s Videocache size = %d.\n", __FUNCTION__, pThis->m_listVideoCache.size());
// 		}
// 		dfT1 = GetExactTime();		
#endif
		dfDecodeStartTime = GetExactTime();
		if (!pThis->m_bIpcStream)
		{// �ļ�����ý�岥�ţ��ɵ��ڲ����ٶ�
			int nTimeSpan1 = (int)(TimeSpanEx(dfDecodeStartTime) * 1000);
			if (nVideoCacheSize < 2 &&
				(pThis->m_nPlayFrameInterval - nTimeSpan1) > 5)
			{
				Sleep(5);
				continue;
			}
			bool bPopFrame = false;
			// ����ʱ������ƥ���֡,��ɾ����ƥ��ķ�I֡
			int nSkipFrames = 0;
			CAutoLock lock(&pThis->m_csVideoCache, false, __FILE__, __FUNCTION__, __LINE__);
			if (!pThis->m_nFirstFrameTime &&
				pThis->m_listVideoCache.size() > 0)
				pThis->m_nFirstFrameTime = pThis->m_listVideoCache.front()->FrameHeader()->nTimestamp;
			for (auto it = pThis->m_listVideoCache.begin(); it != pThis->m_listVideoCache.end();)
			{
				time_t tFrameSpan = ((*it)->FrameHeader()->nTimestamp - pThis->m_tLastFrameTime) / 1000;
				if (StreamFrame::IsIFrame(*it))
				{
					bPopFrame = true;
					break;
				}
				if (pThis->m_fPlayRate < 16.0 && // 16�������£��ſ��ǰ�ʱ��֡
					tFrameSpan / pThis->m_fPlayRate >= max(pThis->m_fPlayInterval, FrameStat.GetAvgValue() * 1000))
				{
					bPopFrame = true;
					break;
				}
				else
				{
					it = pThis->m_listVideoCache.erase(it);
					nSkipFrames++;
				}
			}
			if (nSkipFrames)
				pThis->OutputMsg("%s Skip Frames = %d bPopFrame = %s.\n", __FUNCTION__, nSkipFrames, bPopFrame ? "true" : "false");
			if (bPopFrame)
			{
				FramePtr = pThis->m_listVideoCache.front();
				pThis->m_listVideoCache.pop_front();
				//TraceMsgA("%s Pop a Frame ,FrameID = %d\tFrameTimestamp = %d.\n", __FUNCTION__, FramePtr->FrameHeader()->nFrameID, FramePtr->FrameHeader()->nTimestamp);
			}
			pThis->m_nVideoCache = pThis->m_listVideoCache.size();
			if (!bPopFrame)
			{
				lock.Unlock();	// ����ǰ��������ȻSleep��Ż���������������ط�����ס
				Sleep(10);
				continue;
			}
			lock.Unlock();
			pAvPacket->data = (uint8_t *)FramePtr->Framedata(pThis->m_nSDKVersion);
			pAvPacket->size = FramePtr->FrameHeader()->nLength;
			pThis->m_tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
			av_frame_unref(pAvFrame);

			nAvError = pDecodec->Decode(pAvFrame, nGot_picture, pAvPacket);
			nTotalDecodeFrames++;
			av_packet_unref(pAvPacket);
			if (nAvError < 0)
			{
				av_strerror(nAvError, szAvError, 1024);
				//dfDecodeStartTime = GetExactTime();
				continue;
			}
// 			avcodec_flush_buffers()			
// 			dfDecodeTimespan = TimeSpanEx(dfDecodeStartTime);
// 			if (StreamFrame::IsIFrame(FramePtr))			// ͳ��I֡����ʱ��
// 				IFrameStat.Stat(dfDecodeTimespan);
// 			FrameStat.Stat(dfDecodeTimespan);	// ͳ������֡����ʱ��
// 			if (fLastPlayRate != pThis->m_fPlayRate)
// 			{// �������ʷ����仯������ͳ������
// 				IFrameStat.Reset();
// 				FrameStat.Reset();
// 			}
			fLastPlayRate = pThis->m_fPlayRate;
			fTimeSpan = (TimeSpanEx(dfRenderTime) + dfRenderTimeSpan) * 1000;
			int nSleepTime = 0;
			if (fTimeSpan < pThis->m_fPlayInterval)
			{
				nSleepTime = (int)(pThis->m_fPlayInterval - fTimeSpan);
				if (pThis->m_nDecodeDelay == -1)
					Sleep(nSleepTime);
				else if (pThis->m_nDecodeDelay)
					Sleep(pThis->m_nDecodeDelay);
				else
					continue;
			}
		}
		else
		{// IPC ��������ֱ�Ӳ���
			WaitForSingleObject(pThis->m_hRenderAsyncEvent, nIPCPlayInterval);
			if (nVideoCacheSize >= 3)
			{
				if (pRenderTimer->nPeriod != (nIPCPlayInterval * 3 / 5))	// ���ż������40%,����Ѹ����ջ���֡
					pRenderTimer->UpdateInterval(25);
			}
			else if (pRenderTimer->nPeriod != nIPCPlayInterval)
				pRenderTimer->UpdateInterval(nIPCPlayInterval);
			bool bPopFrame = false;
			Autolock(&pThis->m_csVideoCache);
			if (pThis->m_listVideoCache.size() > 0)
			{
				FramePtr = pThis->m_listVideoCache.front();
				pThis->m_listVideoCache.pop_front();
				bPopFrame = true;
				nVideoCacheSize = pThis->m_listVideoCache.size();
			}
			lock.Unlock();
			if (!bPopFrame)
			{
				Sleep(10);
				continue;
			}

			pAvPacket->data = (uint8_t *)FramePtr->Framedata(pThis->m_nSDKVersion);
			pAvPacket->size = FramePtr->FrameHeader()->nLength;
			pThis->m_tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
			av_frame_unref(pAvFrame);
			nAvError = pDecodec->Decode(pAvFrame, nGot_picture, pAvPacket);
			nTotalDecodeFrames++;
			av_packet_unref(pAvPacket);
			if (nAvError < 0)
			{
				av_frame_unref(pAvFrame);
				av_strerror(nAvError, szAvError, 1024);
				TraceMsgA("%s AvError = %s.\n", __FUNCTION__, szAvError);
				continue;
			}
			dfDecodeTimespan = TimeSpanEx(dfDecodeStartTime);
		}
#ifdef _DEBUG
		if (pThis->m_bSeekSetDetected)
		{
			int nFrameID = FramePtr->FrameHeader()->nFrameID;
			int nTimeStamp = FramePtr->FrameHeader()->nTimestamp / 1000;
			pThis->OutputMsg("%s First Frame after SeekSet:ID = %d\tTimeStamp = %d.\n", __FUNCTION__, nFrameID, nTimeStamp);
			pThis->m_bSeekSetDetected = false;
		}
#endif	
		if (nGot_picture)
		{
			pThis->m_nDecodePixelFmt = (AVPixelFormat)pAvFrame->format;
			SetEvent(pThis->m_hEvnetYUVReady);
			SetEvent(pThis->m_hEventDecodeStart);
			pThis->m_nCurVideoFrame = FramePtr->FrameHeader()->nFrameID;
			pThis->m_tCurFrameTimeStamp = FramePtr->FrameHeader()->nTimestamp;
			pThis->ProcessYUVFilter(pAvFrame, (LONGLONG)pThis->m_nCurVideoFrame);
			if (!pThis->m_bIpcStream &&
				1.0f == pThis->m_fPlayRate  &&
				pThis->m_bEnableAudio &&
				pThis->m_hAudioFrameEvent[0] &&
				pThis->m_hAudioFrameEvent[1])
			{
				if (pThis->m_nDecodeDelay == -1)
					WaitForMultipleObjects(2, pThis->m_hAudioFrameEvent, TRUE, 40);
				else if (!pThis->m_nDecodeDelay)
					WaitForMultipleObjects(2, pThis->m_hAudioFrameEvent, TRUE, pThis->m_nDecodeDelay);
			}
			dfRenderStartTime = GetExactTime();
			pThis->RenderFrame(pAvFrame);
			float dfRenderTimespan = (float)(TimeSpanEx(dfRenderStartTime) * 1000);
			RenderInterval.Stat(dfRenderTimespan);
			if (RenderInterval.IsFull())
			{
				//RenderInterval.OutputStat();
				RenderInterval.Reset();
			}
			if (dfRenderTimeSpan > 60.0f)
			{// ��Ⱦʱ�䳬��60ms

			}
			nRenderTimes++;
			if (!bDecodeSucceed)
			{
				bDecodeSucceed = true;
#ifdef _DEBUG
				pThis->OutputMsg("%s \tObject:%d  SetEvent Snapshot  m_nLifeTime = %d.\n", __FUNCTION__, pThis->m_nObjIndex, timeGetTime() - pThis->m_nLifeTime);
#endif
			}
			pThis->ProcessSnapshotRequire(pAvFrame);
			pThis->ProcessYUVCapture(pAvFrame, (LONGLONG)pThis->m_nCurVideoFrame);
			Autolock(&pThis->m_csFilePlayCallBack);
			if (pThis->m_pFilePlayCallBack)
				pThis->m_pFilePlayCallBack(pThis, pThis->m_pUserFilePlayer);
		}
		else
		{
			TraceMsgA("%s \tObject:%d Decode Succeed but Not get a picture ,FrameType = %d\tFrameLength %d.\n", __FUNCTION__, pThis->m_nObjIndex, FramePtr->FrameHeader()->nType, FramePtr->FrameHeader()->nLength);
		}

		dfRenderTimeSpan = TimeSpanEx(dfRenderStartTime);
		nTimePlayFrame = (int)(TimeSpanEx(dfDecodeStartTime) * 1000);
		dfRenderTime = GetExactTime();
		// 			if ((nTotalDecodeFrames % 100) == 0)
		// 			{
		// 				TraceMsgA("%s nTotalDecodeFrames = %d\tnRenderTimes = %d.\n", __FUNCTION__,nTotalDecodeFrames, nRenderTimes);
		// 			}
	}
	av_frame_unref(pAvFrame);
	SaveRunTime();
	pThis->m_pDecoder = nullptr;
	return 0;
}

UINT __stdcall CIPCPlayer::ThreadAsyncRender(void *p)
{
	CIPCPlayer *pThis = (CIPCPlayer*)p;
	struct DxDeallocator
	{
		CDxSurfaceEx *&m_pDxSurface;
		CDirectDraw *&m_pDDraw;

	public:
		DxDeallocator(CDxSurfaceEx *&pDxSurface, CDirectDraw *&pDDraw)
			:m_pDxSurface(pDxSurface), m_pDDraw(pDDraw)
		{
		}
		~DxDeallocator()
		{
			TraceMsgA("%s pSurface = %08X\tpDDraw = %08X.\n", __FUNCTION__, m_pDxSurface, m_pDDraw);
			Safe_Delete(m_pDxSurface);
			Safe_Delete(m_pDDraw);
		}
	};
	if (!pThis->m_hRenderWnd)
		pThis->OutputMsg("%s Warning!!!A Windows handle is Needed otherwise the video Will not showed..\n", __FUNCTION__);

	int nIPCPlayInterval = 1000 / pThis->m_nVideoFPS;
	
	if (!pThis->InitizlizeDx())
	{
		assert(false);
		return 0;
	}
	shared_ptr<DxDeallocator> DxDeallocatorPtr = make_shared<DxDeallocator>(pThis->m_pDxSurface, pThis->m_pDDraw);
	int nFameListSize = 0;
	while (pThis->m_bThreadDecodeRun)
	{
		pThis->m_cslistAVFrame.Lock();
		nFameListSize = pThis->m_listAVFrame.size();
		pThis->m_cslistAVFrame.Unlock();
		if (nFameListSize < 1)
		{
			Sleep(20);
			continue;
		}
			
		if (pThis->m_pSyncPlayer)///��ͬ����������
		{
			CAVFramePtr pFrame;
			CAutoLock lock(pThis->m_cslistAVFrame.Get(),false, __FILE__, __FUNCTION__, __LINE__);
			do 
			{
				pFrame = pThis->m_listAVFrame.front();
				time_t nTimeSpan = pFrame->tFrame - pThis->m_pSyncPlayer->m_tSyncTimeBase;
				if (nTimeSpan >= 60)	 // �Ѿ�Խ��ʱ�������
				{
					Sleep(20);
					continue;
				}
				else if (nTimeSpan <= -60)	// ���ʱ�������̫Զ
				{
					pThis->m_listAVFrame.pop_front();
					continue;
				}
				pThis->m_listAVFrame.pop_front();
				break;
			} while (true);
			lock.Unlock();
			if (WaitForSingleObject(pThis->m_pSyncPlayer->m_hRenderAsyncEvent, INFINITE) == WAIT_TIMEOUT)
				continue;
		}
		else if (WaitForSingleObject(pThis->m_hRenderAsyncEvent, INFINITE) == WAIT_TIMEOUT)
			continue;
		
		CAutoLock lock(pThis->m_cslistAVFrame.Get(), false, __FILE__, __FUNCTION__, __LINE__);
		CAVFramePtr pFrame = pThis->m_listAVFrame.front();
		pThis->m_listAVFrame.pop_front();
		lock.Unlock();
		if (!pThis->m_pSyncPlayer)
			pThis->m_tSyncTimeBase = pFrame->tFrame;
		pThis->RenderFrame(pFrame->pFrame);
	}
	return 0;
}

UINT __stdcall CIPCPlayer::ThreadAsyncDecode(void *p)
{
	CIPCPlayer *pThis = (CIPCPlayer*)p;

#ifdef _DEBUG
	pThis->OutputMsg("%s \tObject:%d  m_nLifeTime = %d.\n", __FUNCTION__, pThis->m_nObjIndex, timeGetTime() - pThis->m_nLifeTime);
#endif
	int nAvError = 0;
	char szAvError[1024] = { 0 };
	
	// �ȴ���Ч����Ƶ֡����
	long tFirst = timeGetTime();
// 	int nTimeoutCount = 0;
// 	while (pThis->m_bThreadDecodeRun)
// 	{
// 		Autolock(&pThis->m_csVideoCache);
// 		if (pThis->m_listVideoCache.size() < 1)
// 		{
// 			lock.Unlock();
// 			Sleep(20);
// 			continue;
// 		}
// 		else
// 			break;
// 	}
	SaveRunTime();
	if (!pThis->m_bThreadDecodeRun)
	{
		return 0;
	}

	// �ȴ�I֡
	tFirst = timeGetTime();
	//		DWORD dfTimeout = 3000;
	// 		if (!pThis->m_bIpcStream)	// ֻ��IPC��������Ҫ��ʱ��ȴ�
	// 			dfTimeout = 1000;
	AVCodecID nCodecID = AV_CODEC_ID_NONE;
	int nDiscardFrames = 0;

	SaveRunTime();
	switch (pThis->m_nVideoCodec)
	{
	case CODEC_H264:
		nCodecID = AV_CODEC_ID_H264;
		break;
	case CODEC_H265:
		nCodecID = AV_CODEC_ID_H265;
		break;
	default:
	{
		pThis->OutputMsg("%s You Input a unknown stream,Decode thread exit.\n", __FUNCTION__);
		if (pThis->m_hRenderWnd)	// ���߳��о�������ʹ��SendMessage����Ϊ���ܻᵼ������
			::PostMessage(pThis->m_hRenderWnd, WM_IPCPLAYER_MESSAGE, IPCPLAYER_UNSURPPORTEDSTREAM, 0);
		assert(false);
		return 0;
		break;
	}
	}

	int nRetry = 0;

	shared_ptr<CVideoDecoder>pDecodec = make_shared<CVideoDecoder>();
	if (!pDecodec)
	{
		pThis->OutputMsg("%s Failed in allocing memory for Decoder.\n", __FUNCTION__);
		assert(false);
		return 0;
	}
	SaveRunTime();
	struct DxDeallocator
	{
		CDxSurfaceEx *&m_pDxSurface;
		CDirectDraw *&m_pDDraw;

	public:
		DxDeallocator(CDxSurfaceEx *&pDxSurface, CDirectDraw *&pDDraw)
			:m_pDxSurface(pDxSurface), m_pDDraw(pDDraw)
		{
		}
		~DxDeallocator()
		{
			TraceMsgA("%s pSurface = %08X\tpDDraw = %08X.\n", __FUNCTION__, m_pDxSurface, m_pDDraw);
			Safe_Delete(m_pDxSurface);
			Safe_Delete(m_pDDraw);
		}
	};
	if (!pThis->m_hRenderWnd)
		pThis->OutputMsg("%s Warning!!!A Windows handle is Needed otherwise the video Will not showed..\n", __FUNCTION__);

	int nIPCPlayInterval = 1000 / pThis->m_nVideoFPS;

	if (!pThis->InitizlizeDx())
	{
		assert(false);
		return 0;
	}
	shared_ptr<DxDeallocator> DxDeallocatorPtr = make_shared<DxDeallocator>(pThis->m_pDxSurface, pThis->m_pDDraw);

	// ʹ�õ��߳̽���,���߳̽�����ĳ�˱Ƚ�����CPU�Ͽ��ܻ����Ч����������I5 2GHZ���ϵ�CPU�ϵĶ��߳̽���Ч���������Է�����ռ�ø�����ڴ�
	pDecodec->SetDecodeThreads(1);
	// ��ʼ��������

	while (pThis->m_bThreadDecodeRun)
	{// ĳ��ʱ����ܻ���Ϊ�ڴ����Դ�������³�ʼ�����������,��˿����ӳ�һ��ʱ����ٴγ�ʼ��������γ�ʼ���Բ��ɹ��������˳��߳�
		//DeclareRunTime(5);
		//SaveRunTime();
		if (!pDecodec->InitDecoder(nCodecID, pThis->m_nVideoWidth, pThis->m_nVideoHeight, false))
		{
			pThis->OutputMsg("%s Failed in Initializing Decoder��CodeCID =%d,Width = %d,Height = %d,HWAccel = %d.\n", __FUNCTION__, nCodecID, pThis->m_nVideoWidth, pThis->m_nVideoHeight, pThis->m_bEnableHaccel);
#ifdef _DEBUG
			pThis->OutputMsg("%s \tObject:%d Line %d Time = %d.\n", __FUNCTION__, pThis->m_nObjIndex, __LINE__, timeGetTime() - pThis->m_nLifeTime);
#endif
			return 0;
		}
		else
			break;
		//SaveRunTime();
	}
	SaveRunTime();
	if (!pThis->m_bThreadDecodeRun)
	{
		//assert(false);
		return 0;
	}

	AVPacket *pAvPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	shared_ptr<AVPacket>AvPacketPtr(pAvPacket, av_free);
	av_init_packet(pAvPacket);
	AVFrame *pAvFrame = av_frame_alloc();
	shared_ptr<AVFrame>AvFramePtr(pAvFrame, av_free);

	StreamFramePtr FramePtr;
	int nGot_picture = 0;
	DWORD nResult = 0;
	float fTimeSpan = 0;
	int nFrameInterval = pThis->m_nFileFrameInterval;
	pThis->m_dfTimesStart = GetExactTime();

	double dfDecodeStartTime = GetExactTime();
	double dfRenderTime = GetExactTime() - pThis->m_fPlayInterval;	// ͼ����ʾ��ʱ��
	double dfRenderStartTime = 0.0f;
	double dfRenderTimeSpan = 0.000f;
	double dfTimeSpan = 0.0f;

// #ifdef _DEBUG
// 	pThis->m_csVideoCache.Lock();
// 	TraceMsgA("%s Video cache Size = %d .\n", __FUNCTION__, pThis->m_listVideoCache.size());
// 	pThis->m_csVideoCache.Unlock();
// 	pThis->OutputMsg("%s \tObject:%d Start Decoding.\n", __FUNCTION__, pThis->m_nObjIndex);
// #endif

	int nIFrameTime = 0;
	bool bDecodeSucceed = false;
	pThis->m_nFirstFrameTime = 0;
	float fLastPlayRate = pThis->m_fPlayRate;		// ��¼��һ�εĲ������ʣ����������ʷ����仯ʱ����Ҫ����֡ͳ������

	if (pThis->m_dwStartTime)
	{
		TraceMsgA("%s %d Render Timespan = %d.\n", __FUNCTION__, __LINE__, timeGetTime() - pThis->m_dwStartTime);
		pThis->m_dwStartTime = 0;
	}

	int nFramesPlayed = 0;			// �����ܷ���
	double dfTimeStartPlay = GetExactTime();// ������ʼʱ��
	int nTimePlayFrame = 0;		// ����һ֡���ķ�ʱ�䣨MS��
	int nPlayCount = 0;
	int TPlayArray[100] = { 0 };
	double dfT1 = GetExactTime();
	int nVideoCacheSize = 0;
	LONG nTotalDecodeFrames = 0;
	dfDecodeStartTime = GetExactTime() - pThis->m_nPlayFrameInterval / 1000.0f;
	SaveRunTime();
	pThis->m_pDecoder = pDecodec;
	int nRenderTimes = 0;
	CStat  RenderInterval("RenderInterval", pThis->m_nObjIndex);
	DHFrame *pDHFrame = nullptr;
	while (pThis->m_bThreadDecodeRun)
	{
		while (pThis->m_bThreadDecodeRun && pThis->m_pDHStreamParser)
		{
			if (!pDHFrame)
				pDHFrame = pThis->m_pDHStreamParser->ParserFrame();
			if (pDHFrame)
			{
				if (pThis->InputStream(pDHFrame->pContent, pDHFrame->bKeyFrame ? IPC_I_FRAME : IPC_P_FRAME, pDHFrame->nFrameSize, pDHFrame->nFrameSeq, pDHFrame->tTimeStamp) == IPC_Succeed)
				{
					delete pDHFrame;
					pDHFrame = nullptr;
					break;
				}
				else
					break;
			}
			else
				break;
		} 

		pThis->m_csVideoCache.Lock();
		nVideoCacheSize = pThis->m_listVideoCache.size();
		pThis->m_csVideoCache.Unlock();

		if (nVideoCacheSize < 1)
		{
			Sleep(20);
			continue;
		}
		//////////////////////////////////////////////////////////////////////////

		if (pThis->m_pSyncPlayer)///��ͬ����������
		{
			while (pThis->m_bThreadDecodeRun)
			{
				CAutoLock lock(pThis->m_csVideoCache.Get());
				FramePtr = pThis->m_listVideoCache.front();
				time_t nTimeSpan = FramePtr->FrameHeader()->nTimestamp - pThis->m_pSyncPlayer->m_tSyncTimeBase;
				if (nTimeSpan >= 60)	 // �Ѿ�Խ��ʱ�������
				{
					lock.Unlock();
					Sleep(20);
					continue;
				}
				else if (nTimeSpan <= -60)	// ���ʱ�������̫Զ
				{
					if (!StreamFrame::IsIFrame(FramePtr))
					{
						pThis->m_listVideoCache.pop_front();
						if (pThis->m_listVideoCache.size() < 1)
							break;
						continue;
					}
					else
						break;
				}
				pThis->m_listVideoCache.pop_front();
				break;
			}
			if (!FramePtr)
				continue;
		}
		else
		{
			if (WaitForSingleObject(pThis->m_hRenderAsyncEvent, INFINITE) == WAIT_TIMEOUT)
				continue;
			CAutoLock lock(pThis->m_csVideoCache.Get());
			if (pThis->m_listVideoCache.size() < 1)
				continue;
			FramePtr = pThis->m_listVideoCache.front();
			pThis->m_listVideoCache.pop_front();
		}
		//////////////////////////////////////////////////////////////////////////
		
		pThis->m_csVideoCache.Lock();
		pThis->m_nVideoCache = pThis->m_listVideoCache.size();
		pThis->m_csVideoCache.Unlock();

		pAvPacket->data = (uint8_t *)FramePtr->Framedata(pThis->m_nSDKVersion);
		pAvPacket->size = FramePtr->FrameHeader()->nLength;
		pThis->m_tLastFrameTime = FramePtr->FrameHeader()->nTimestamp;
		av_frame_unref(pAvFrame);

		if (!pThis->m_pSyncPlayer)
			pThis->m_tSyncTimeBase = FramePtr->FrameHeader()->nTimestamp;
		nAvError = pDecodec->Decode(pAvFrame, nGot_picture, pAvPacket);
		nTotalDecodeFrames++;
		av_packet_unref(pAvPacket);
		if (nAvError < 0)
		{
			av_strerror(nAvError, szAvError, 1024);
			continue;
		}

		if (nGot_picture)
		{
			pThis->m_nDecodePixelFmt = (AVPixelFormat)pAvFrame->format;
			pThis->RenderFrame(pAvFrame);
		}
		else
		{
			TraceMsgA("%s \tObject:%d Decode Succeed but Not get a picture ,FrameType = %d\tFrameLength %d.\n", __FUNCTION__, pThis->m_nObjIndex, FramePtr->FrameHeader()->nType, FramePtr->FrameHeader()->nLength);
		}
	}
	av_frame_unref(pAvFrame);
	SaveRunTime();
	pThis->m_pDecoder = nullptr;
	return 0;
}