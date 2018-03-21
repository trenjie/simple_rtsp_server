#include<stdio.h>
#include<stdbool.h>
#include<unistd.h>
#include <netinet/in.h>

enum RequestType
{
	requestOptions		= 0x001,
	requestDescribe		= 0x002,
	requestAnnounce		= 0x004,
	requestSetup		= 0x008,
	requestGetParameter = 0x010,
	requestSetParameter = 0x020,
	requestTeardown		= 0x040,
	requestPlay			= 0x080,
	requestPause		= 0x100,
	requestRecord       = 0x200
};

struct RtspSession
{
	char* m_SessionName;

	char* m_SetupUrl;

	int Rtp_Port_Off;
};

struct RtspTransport
{
	char* m_BindIp;
	unsigned int	m_BindPort;
	unsigned int	m_MaxConnects;
	int	m_Socket;
	struct sockaddr_in m_BindAddr;

	bool	m_isOpen;

	int	 m_hListenThread;
	int	 m_isStopListenThread;
	struct RtspSession* m_RtspSession;
};

extern int rtsp_session_open();
extern int rtsp_session_process();

extern struct RtspTransport* m_pRtspTransport;
extern int g_connectSocket;

extern int g_RtpRun;

extern int rtsp_TransportH264Nal(const char* pNal, unsigned int nalSize, int pts, bool isLast);
