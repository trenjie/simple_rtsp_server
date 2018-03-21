//#include<stdlib.h>
//#include<unistd.h>
//#include<string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/file.h>
#include<malloc.h>

#include"../inc/rtsp_session.h"
struct RtspTransport* m_pRtspTransport;

#define SOCKET_ERROR -1
static char m_Requests[18][256];
static char m_Response[18][256];
static int g_writeCount = 0;
static int g_CSeq = 0;
static char g_Sdp[1024];
static int g_SdpLen = 0;

static int rtsp_session_ResponseOptions();
static int rtsp_session_ResponseDescribe(char* sdp, unsigned int sdpLength);
static int rtsp_session_GetRequestMrl(char** pMrl);
static int rtsp_session_GenerateMediaSdp();
static char* rtsp_media_GenerateMediaSdp();
static int rtsp_media_TransportSetup(char* serverIp , unsigned int serverPort ,char* clientIp , unsigned int clientPort);
int rtsp_TransportH264Nal(const char* pNal, unsigned int nalSize, int pts, bool isLast);
int g_RtpSocket = -1;
static long long g_m_Session = 0;

//static int lineCount = 0;

static int g_lineTest = 0;

int g_RtpRun = 0;

char *myitoa(int value,char *string,int radix)    
{    
	printf("trj test enter myitoa function now !!!!!!!!!!!!!!!!!!!!!!!\n");
	 int rt=0;    
	 if(string==NULL)    
		return NULL;    
	 if(radix<=0||radix>30)    
		return NULL;    
	 rt=snprintf(string,radix,"%d",value);    
	 if(rt>radix)    
		  return NULL; 
	 printf("trj test in myitoa func string = %s\n",string);
	   string[rt]='\0';    
	   return string;    
}

static long long rtsp_GenerateOneNumber() 
{
	///////////////////trj///////////////////////////////
	struct timeval tv;
	ulong time_val;
	gettimeofday(&tv, NULL);
	time_val = tv.tv_sec*1000L + tv.tv_usec/1000L;
	return time_val;
}

static int rtsp_select_read(unsigned char* pBuffer, int readSize)
{
	int selectState;
	int recvSize;

	if (!pBuffer)
		return -1;

	fd_set fdset;

	struct timeval selectTimeout;
	int ret;

	selectTimeout.tv_sec  = 0;
	selectTimeout.tv_usec = 50 * 1000;

	FD_ZERO (&fdset);
	FD_SET  (g_connectSocket, &fdset);

	ret = select ( (int)g_connectSocket + 1, &fdset, NULL, NULL, &selectTimeout);

	if (ret == 1) 
	{
			recvSize = recv(g_connectSocket, (char*)pBuffer, 1, 0);

			if (recvSize <= 0)
				return -1;
			return recvSize;
	}
	else if(ret == 3)
	{
		return 0;
	}

	return -1;
}

int readCount = 0;
static int rtsp_tcp_read(char str[256])/////read a line///trj note:
{
	char c;
	int iRead;

	readCount = 0;

	do 
	{
		iRead = rtsp_select_read((unsigned char*)&c, 1);

		if (iRead == 1)
		{
			if ( c == '\r' || c == '\n')
				break;

			str[readCount] = c;

			readCount++;
		}
	} while (iRead == 1);


	if (c == '\r')
		iRead = rtsp_select_read((unsigned char*)&c, 1);

	if (iRead == -1)
		return -1;

	if (readCount == 0 && (c == '\r' || c == '\n') )
	{
//		printf("<< ''\n\n");
		return 0;
	}

//	if (readCount)
//		printf("<< '%s'\n", str);
	
	return readCount;
}


static int rtsp_session_GetRequests()
{
	char str_line[256];

	int lineCount = 0;

	int iRead = 1;

	int i = 0;

	char* pSeq = NULL;

	for(; i < 18 ; i++)
	{
		memset((unsigned char*)m_Requests[i],0,256);
	}

	while(iRead > 0)
	{
		memset(str_line , 0 , 256);

		iRead = rtsp_tcp_read(str_line);

		if(iRead > 0)
		{
			memcpy(m_Requests[lineCount] , str_line , readCount);

			printf("trj test rtsp_session_GetRequests strLine = %s\n",m_Requests[lineCount]);

			lineCount++;
		}
	}

	g_lineTest = lineCount;

	if(iRead < 0)
		return false;


	/////seach for CSeq child string////trj note:////

	for(i = 0 ; i < lineCount ;i++)
	{
		pSeq = strstr(m_Requests[i],"CSeq");
		if ( pSeq )
		{
			g_CSeq = pSeq[6] - 48;

//			printf("trj test rtsp_session_GetRequests g_CSeq = %d \n", g_CSeq);

			break;
		}
	}

	return true;
}

static void* StartRtspSessionThread(void* pParam)
{
	struct RtspSession* pThis = (struct RtspSession*)pParam;

	while(1)
	{
		rtsp_session_process();
	}
	return 0;
}

int g_connectSocket = -1;

static void* StartListenThread(void* pUser)
{
	struct RtspTransport* pThis = (struct RtspTransport*)pUser;

	int	 connect;

	struct sockaddr_in connectAddr;

	int	 addrSize = sizeof(connectAddr);

	socklen_t tmpLen = (socklen_t)addrSize;

	printf("trj test ready to get client connection\n");

	while(1)
	{
		connect = accept(pThis->m_Socket, (struct sockaddr*)&connectAddr, &tmpLen);

		if(connect == SOCKET_ERROR)
			continue;

		g_connectSocket = connect;

		m_pRtspTransport->m_RtspSession = (struct RtspSession*)malloc(sizeof(struct RtspSession));

		begin_thread(StartRtspSessionThread,m_pRtspTransport->m_RtspSession);
	}
	return 0;
}

int rtsp_session_open()
{
	m_pRtspTransport->m_Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (m_pRtspTransport->m_Socket == -1) 
	{
		printf("failed to create socket");
//		Close();
		return false;
	}

////////////////////trj changed for linux////////////////////////
	int flags = fcntl(m_pRtspTransport->m_Socket, F_GETFL, 0);/////trj////////////////////

	int error = fcntl(m_pRtspTransport->m_Socket, F_SETFL,flags | O_NONBLOCK);

	if (error == SOCKET_ERROR)
	{
//		ReportError();
		return false;
	}

	m_pRtspTransport->m_BindAddr.sin_family = AF_INET;
	m_pRtspTransport->m_BindAddr.sin_port = htons(554);
	m_pRtspTransport->m_BindAddr.sin_addr.s_addr = inet_addr("192.168.133.128");

	 int opt = 1;
	 int len = sizeof(opt);
	 setsockopt( m_pRtspTransport->m_Socket, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)len );

	if (bind(m_pRtspTransport->m_Socket, (struct sockaddr*)&(m_pRtspTransport->m_BindAddr), sizeof(m_pRtspTransport->m_BindAddr)) < 0)
	{
//		printf("bind error. WSAGetLastError() = %d\n", errno);
//		Close();
		return false;
	}

	if (listen(m_pRtspTransport->m_Socket, SOMAXCONN) < 0)
	{
//		printf("listen error. WSAGetLastError() = %d\n", errno);
//		Close();
		return false;
	}

	begin_thread(StartListenThread,m_pRtspTransport);

//	m_isOpen = true;
	return true;
}

static int rtsp_session_GetRequestType(int* pRequestType)
{
	int ret = 1;

	int requestType = 0;

	if ( strstr(m_Requests[0],"OPTIONS"))
	{
		requestType = requestOptions;
	}
	else if(strstr(m_Requests[0],"DESCRIBE"))
	{
		requestType = requestDescribe;
	}
	else if(strstr(m_Requests[0],"SETUP"))
	{
		requestType = requestSetup;
	}
	else if(strstr(m_Requests[0],"PLAY"))
	{
		requestType = requestPlay;
	}
	else if(strstr(m_Requests[0],"TEARDOWN"))
	{
		requestType = requestTeardown;
	}
	
	if(0 == requestType)
	{
		ret = 0;
	}

	*pRequestType = requestType;

	return ret;
}

static int rtsp_addFiled(char* field)
{
	int ret = 0;

	strcpy(m_Response[g_writeCount] , field);

	return ret;
}

int rtsp_tcp_send(char* sendBuffer , int len)
{
	int ret = 0;

	printf(">> '%s'", sendBuffer);

	ret = send(g_connectSocket,sendBuffer,len,0);/////trj

	send(g_connectSocket,(char*)"\r\n", 2,0);

	printf("done.\n");

	return ret;
}

bool rtsp_SendResponse(char* responseType)
{
	
	char responseCmd[256];
	char cseq[256];
	char session[256];

	int i = 0;

	if (responseType)
		strcpy(responseCmd , responseType);

	int len = 0;

	len = strlen("RTSP/1.0 200 OK");

//	if (!responseCmd)
	strcpy(responseCmd , "RTSP/1.0 200 OK");

	snprintf(cseq, 256, "CSeq: %u", g_CSeq);

	snprintf(session, 256, "Session: %I64u", g_m_Session);

	rtsp_tcp_send(responseCmd,len);

	rtsp_tcp_send(cseq,6);

	if (g_m_Session > 0)
		rtsp_tcp_send(session,strlen(session));

	for(i = 0; i < g_writeCount; i ++)
	{
		rtsp_tcp_send(m_Response[i] ,strlen(m_Response[i]));	
	}

	rtsp_tcp_send("",0);

	return true;
}

static int rtsp_session_ResponseOptions()
{
	int ret = 0;

	rtsp_addFiled("Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE");

	g_writeCount++;

	rtsp_SendResponse("");

	printf("\n");
	return ret;
}

static int rtsp_session_ResponseSetup()
{
	int ret = 0;

	int i = 0;
	
	char* pTargetPort ;//= strstr(m_Requests[4] , "-");

	char* pTarTarget;

	char* pTmp;

	char* pRtpTmp;

	int port = 0;

	for( i = 0; i < g_lineTest ; i++)
	{
		pTargetPort = strstr(m_Requests[i] , "client_port=");
		if(pTargetPort)
		{
			printf("trj test find targetPort = %s\n",pTargetPort);

			break;
		}
	}
	
	pTarTarget = strstr(pTargetPort , "=");

	if(pTarTarget)
	{
//		printf("trj test TarTarget = %s\n",pTarTarget);
	}

	pTmp = strstr(pTarTarget , "-");

	if(pTmp)
	{
//			printf("trj test pTmp = %s\n",pTmp);
	}

	pTmp++;

//	printf("%s",pTmp);

	port = atoi(pTmp);

	printf("trj test port = %d\n",port);

	unsigned int bindPort = 8000;

	while(!rtsp_media_TransportSetup("192.168.133.128" , bindPort , "192.168.133.1" ,port - 1))
	{
		bindPort += 2;

		if(bindPort > 65536)
			break;
	}

	char transport[1024];
	char client_port[100];
	char server_port[100];
	char ssrc_[100];
	char	temp[100];

	g_m_Session = rtsp_GenerateOneNumber();

	snprintf(temp, 100, "server_port=%u-%u", bindPort, bindPort+1);
	strcpy(server_port , temp);

	snprintf(temp, 100, "client_port=%u-%u", port - 1, port);
	strcpy(client_port , temp);

	snprintf(temp, 100, "ssrc=%u", 0);
	strcpy(ssrc_ , temp);

	strcpy(transport , "Transport: RTP/AVP;unicast;");

	strncat(transport , "source=" , strlen("source="));

	strncat(transport , "192.168.133.128",15);

	strncat(transport , ";",1);

	strncat(transport , server_port ,strlen(server_port));

	strncat(transport , ";",1);

	strncat(transport , client_port ,strlen(client_port));

	strncat(transport , ";",1);
	
	strncat(transport , ssrc_ ,strlen(ssrc_));

	printf("trj test bbbbbbbbbbbbbbbbbbbbbbbb\n");

	for(i = 0; i < 18 ; i++)
	{
		memset(m_Response[i] , 0 , 256);
	}

	g_writeCount = 0;
	
	rtsp_addFiled(transport);

	g_writeCount++;

	rtsp_SendResponse("");

	printf("\n");

	return ret;
}

static int rtsp_session_ResponsePlay()
{
	int ret = 0;
	int i = 0;

	printf("trj test enter rtsp_session_ResponsePlay now !!!!!!!!!!!!!!!\n");

	char ResString[64];

	strcpy(ResString , "RTP-Info: url=rtsp://192.168.60.41/test/trackID=1");

	for(i = 0; i < 18 ; i++)
	{
		memset(m_Response[i] , 0 , 256);
	}

	g_writeCount = 0;
	
	rtsp_addFiled(ResString);

	g_writeCount++;

	rtsp_SendResponse("");

	g_RtpRun = 1;

	return ret;
}

static int rtsp_session_ResponseTeardown()
{
	int ret = 0;

	return ret;
}

int rtsp_session_process()
{
	int ret = 0;
		
	int requestType = 0;
	
	if ( !rtsp_session_GetRequests() )
		return ret;

	if ( !rtsp_session_GetRequestType(&requestType) )
		return ret;

		switch(requestType)
		{
		case requestOptions:
			{
				rtsp_session_ResponseOptions();

				break;
			}
		case requestDescribe:
			{
				rtsp_session_ResponseDescribe(g_Sdp , strlen(g_Sdp));

				break;
			}
		case requestSetup:
			rtsp_session_ResponseSetup();
			break;
		case requestPlay:
			rtsp_session_ResponsePlay();
			break;
//		case requestPause:
//			rtsp_session_ResponsePause();
//			break;
		case requestTeardown:
			rtsp_session_ResponseTeardown();
			break;
//		case requestGetParameter:
//			ResponseGetParameter();
//			break;
		default:
			break;
		}
	return ret;
}


static int rtsp_session_ResponseDescribe(char* sdp, unsigned int sdpLength)
{
	int ret = 0;

	rtsp_session_GenerateMediaSdp();

	char contentBase[256];
	char contentType[256];
	char contentLength[256];
	int len = 0;
	char server[256];

	char temp[20];
	char* requestMrl;

	int i = 0;

	strcpy(server ,"Server: RTSP Service");

	strcpy(contentBase ,"Content-Base: ");

	rtsp_session_GetRequestMrl(&requestMrl);

//	strcpy(requestMrl,"rtsp://192.168.133.128/test");

	strncat(contentBase , requestMrl , 256);

	strcpy(contentType , "Content-Type: application/sdp");

	snprintf(temp, 20, "%lu", sdpLength);

	strcpy(contentLength , "Content-Length: ");////trj

	strncat(contentLength , temp , 20);/////trj

	for(i = 0; i < 18 ; i++)
	{
		memset(m_Response[i] , 0 , 256);
	}

	g_writeCount = 0;

	rtsp_addFiled(server);
	g_writeCount++;

	rtsp_addFiled(contentBase);
	g_writeCount++;

	rtsp_addFiled(contentType);
	g_writeCount++;

	rtsp_addFiled(contentLength);
	g_writeCount++;

	rtsp_SendResponse("");

	printf("\n");

	send(g_connectSocket , sdp , sdpLength , 0 );

	printf("Content:\n");

	printf(sdp);

	printf("\n\n");

	return ret;
}

int rtsp_session_GetRequestMrl(char** pMrl)
{
	int ret = 0;

	char* url = strstr(m_Requests[0] ,"rtsp:/");

	int len = strlen(url);
	
	char tmpBuffer[256];

	strncpy(tmpBuffer , url , len - 9);

	*pMrl = tmpBuffer;

//	 printf("trj test rtsp_session_GetRequestMrl = %s\n",*pMrl);
	
	return ret;
}

int rtsp_session_GenerateMediaSdp()
{
	int ret = 0;

	char	numberStr[100];

	snprintf(numberStr, 100, "%I64d", rtsp_GenerateOneNumber());

	char sessionId[256] ;

	strcpy(sessionId ,numberStr);

	char sessionName[256];

	char sessionVersion[256] ;

	strcpy(sessionVersion ,numberStr);
	
	memset(g_Sdp , 0  , 1024);

	strcpy(g_Sdp , "v=0\r\n");

	strncat(g_Sdp , "o=- ",4);

	strncat(g_Sdp , sessionId , strlen(sessionId));

	strncat(g_Sdp , " " , 1);

	strncat(g_Sdp , sessionVersion , strlen(sessionVersion));

	strncat(g_Sdp , " "  , 1);

	strncat(g_Sdp , "IN IP4 " , 7);

	strncat(g_Sdp , "192.168.133.128" , 15);

	strncat(g_Sdp , "\r\n" , 2);


	strncat(g_Sdp , "s=" , 2);

//	strncat(g_Sdp , sessionName , strlen(sessionName));
	
	strncat(g_Sdp , "test" , strlen("test"));

	strncat(g_Sdp , "\r\n" , 2);


	strncat(g_Sdp , "c=IN IP4 " , 9);

	strncat(g_Sdp , "192.168.133.1" , 13);

	strncat(g_Sdp , "\r\n" , 2);


	strncat(g_Sdp , "t=" , 2);

	strncat(g_Sdp , "0" , 1);

	strncat(g_Sdp , " 0" , 2);

	strncat(g_Sdp , "\r\n" , 2);

	strncat(g_Sdp , "a=control:*\r\n" , strlen("a=control:*\r\n"));

	strncat(g_Sdp , rtsp_media_GenerateMediaSdp() , strlen(rtsp_media_GenerateMediaSdp()));

	return ret;
}

extern char Sps_Pps_Param_Sets[1024];
char* rtsp_media_GenerateMediaSdp()
{
	char	mediaSdp[1024];
	
	char 	port[32];
	char	payloadType[32];
	char	bs[32];
	char	fmtp[256];
	char	streamid[32];
	char	esid[32];
	char	cliprect[32];

	char	temp[500];
	
//	m_nRtpPayloadType = nRtpPayloadType;


	// 生成各字段的内容
	snprintf(temp, 500, "%u", 0);
//	port			= temp;
	strcpy(port , temp);

	snprintf(temp, 500, "%u", 96);
//	payloadType	 = temp;

	strcpy(payloadType , temp);

	snprintf(temp, 500, "profile-level-id=%06X; sprop-parameter-sets=%s; packetization-mode=1;", 100, "AAABZ2QADKw07BQfsBEAAAMAAQAAAwAejxQpOA==,AAABaO6csiw");//Sps_Pps_Param_Sets);//m_SpropParameterSets.c_str());
//	fmtp	= temp;
	strcpy(fmtp , temp);

	snprintf(temp, 500, "0,0,%u,%u", 240, 320);
//	cliprect = temp;
	strcpy(cliprect , temp);

	snprintf(temp, 500, "%u", 0);
//	bs = temp;
	strcpy(bs , temp);
	
	snprintf(temp, 500, "%u", 1);
//	streamid = temp;
	strcpy(streamid , temp);

	snprintf(temp, 500, "%u", 201);
//	esid = temp;
	strcpy(esid , temp);

	// 生成sdp内容
//	mediaSdp += "m=video "+port+" RTP/AVP "+payloadType+"\r\n";	//m
	strcpy(mediaSdp , "m=video ");
	strncat(mediaSdp ,port , strlen(port));
	strncat(mediaSdp ," RTP/AVP " , strlen(" RTP/AVP "));
	strncat(mediaSdp ,payloadType , strlen(payloadType));
	strncat(mediaSdp , "\r\n" , 2);

//	mediaSdp += "b=AS:"+bs+"\r\n";													//b
	strncat(mediaSdp , "b=AS:" , strlen("b=AS:"));
	strncat(mediaSdp ,bs , strlen(bs));
	strncat(mediaSdp , "\r\n" , 2);

//	mediaSdp += "a=rtpmap:"+payloadType+" H264/90000\r\n";							//a=rtpmap
	strncat(mediaSdp , "a=rtpmap:" , strlen("a=rtpmap:"));
	strncat(mediaSdp ,payloadType , strlen(payloadType));
	strncat(mediaSdp , " H264/90000\r\n" , strlen(" H264/90000\r\n"));

//	mediaSdp += "a=fmtp:"+payloadType+" "+fmtp+"\r\n";								//a=fmtp
	strncat(mediaSdp , "a=fmtp:" , strlen("a=fmtp:"));
	strncat(mediaSdp , payloadType , strlen(payloadType));
	strncat(mediaSdp , " " , strlen(" "));
	strncat(mediaSdp , fmtp , strlen(fmtp));
	strncat(mediaSdp , "\r\n" , 2);

//	mediaSdp += "a=cliprect:"+cliprect+"\r\n";										//a=cliprect
	strncat(mediaSdp , "a=cliprect:" , strlen( "a=cliprect:"));
	strncat(mediaSdp , cliprect , strlen(cliprect));
	strncat(mediaSdp , "\r\n" , 2);

//	mediaSdp += "a=mpeg4-esid:"+esid+"\r\n";
	strncat(mediaSdp ,"a=mpeg4-esid:" , strlen( "a=mpeg4-esid:"));
	strncat(mediaSdp , esid , strlen(esid));
	strncat(mediaSdp , "\r\n" , 2);

//	if (bUseRTSP)
//		mediaSdp += "a=control:trackID="+streamid+"\r\n";
	strncat(mediaSdp ,"a=control:trackID=" , strlen("a=control:trackID="));
	strncat(mediaSdp , streamid , strlen(streamid));
	strncat(mediaSdp , "\r\n" , 2);
	
	return mediaSdp;
}

int rtsp_media_TransportSetup(char* serverIp , unsigned int serverPort ,char* clientIp , unsigned int clientPort)
{
	int ret = 0;

/*
	if (m_isOpen)
	return FALSE;

	m_isOpen = FALSE;
	m_isConnect = FALSE;
*/

	int error = 0;
	int i_val = 0;
	
//	if (m_Socket)
//		close(m_Socket);

	g_RtpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	printf("trj test g_RtpSocket = %d\n",g_RtpSocket);

//	return 1;
	
	if ( g_RtpSocket == -1 )
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 111111111111111111111\n");
		return false;
	}

//	i_val = 1;	// 非阻塞方式

	int flags = fcntl(g_RtpSocket, F_GETFL, 0);/////trj////////////////////

	error = fcntl(g_RtpSocket, F_SETFL,flags | O_NONBLOCK);

	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 2222222222222222222222222\n");
		return false;
	}

	i_val = (int)(1024 * 1024 * 1.25);//2M Byte 1000Mbps的network在0.01秒内最高可以接收到1.25MB数据
	error = setsockopt( g_RtpSocket, SOL_SOCKET, SO_RCVBUF, (char*)&i_val, sizeof(i_val) );
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 333333333333333333333333\n");
		return false;
	}

	error = setsockopt( g_RtpSocket, SOL_SOCKET, SO_SNDBUF, (char*)&i_val, sizeof(i_val) );
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 444444444444444444444444444\n");
		return false;
	}	

	// 可重用
	i_val = 1;
	error = setsockopt(g_RtpSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&i_val, sizeof(i_val));
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 5555555555555555555555555\n");
		return false;
	}

	// 设置ttl
	i_val = 5;
	error = setsockopt(g_RtpSocket,IPPROTO_IP,IP_MULTICAST_TTL,(char*)&i_val, sizeof(i_val));
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 66666666666666666666666666\n");
		return false;
	}
	

	// 绑定套接字
	struct sockaddr_in m_BindAddr;
	memset((void*)&m_BindAddr, 0, sizeof(m_BindAddr));

	m_BindAddr.sin_family = AF_INET;   
	m_BindAddr.sin_port = htons(serverPort);
	m_BindAddr.sin_addr.s_addr = inet_addr(serverIp);
	
	if ( IN_MULTICAST(ntohl(m_BindAddr.sin_addr.s_addr))  || m_BindAddr.sin_addr.s_addr == INADDR_BROADCAST )
		m_BindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	error = bind(g_RtpSocket, (struct sockaddr*)&m_BindAddr, sizeof(m_BindAddr));
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 777777777777777777777\n");
		return false;
	}
	
	i_val = sizeof(m_BindAddr);
	socklen_t len;
	error = getsockname(g_RtpSocket, (struct sockaddr*)&m_BindAddr, &len);
	if (error == SOCKET_ERROR)
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 88888888888888888888\n");
		return false;
	}

//	if ( ! SetMulticast(bindIp.c_str()))
//		return FALSE;

//	m_isOpen = TRUE;


	struct sockaddr_in m_ConnectAddr;
	memset((void*)&m_ConnectAddr, 0, sizeof(m_ConnectAddr));

	m_ConnectAddr.sin_family = AF_INET;
	m_ConnectAddr.sin_port = htons(clientPort);
	m_ConnectAddr.sin_addr.s_addr = inet_addr(clientIp);

	if (connect(g_RtpSocket, (struct sockaddr*)&m_ConnectAddr, sizeof(m_ConnectAddr)) == SOCKET_ERROR )
	{
//		ReportError();
		printf("trj test g_RtpSocket create error 999999999999999999999999999\n");
		return false;
	}
	return true;
}

static unsigned short m_SequenceNumber = 0;

int rtsp_rtp_send_data(const char* pNal, unsigned int nalSize, int pts, bool isLast)
{
	int ret = 0;

	unsigned char Rtp_Packet_Data[1500];
	int Rtp_Packet_Len;

	Rtp_Packet_Data[0] = 0x80;
	Rtp_Packet_Data[1] = (isLast?0x80:0x00)|96;

	Rtp_Packet_Data[2] = ( m_SequenceNumber >> 8 )&0xff;
	Rtp_Packet_Data[3] = m_SequenceNumber&0xff;

	Rtp_Packet_Data[4] = (unsigned char)( pts >> 24 )&0xff;
	Rtp_Packet_Data[5] = (unsigned char)( pts >> 16 )&0xff;
	Rtp_Packet_Data[6] = (unsigned char)( pts >>  8 )&0xff;
	Rtp_Packet_Data[7] = (unsigned char)pts&0xff;

	Rtp_Packet_Data[ 8] = ( 0 >> 24 )&0xff;
	Rtp_Packet_Data[ 9] = ( 0 >> 16 )&0xff;
	Rtp_Packet_Data[10] = ( 0 >>  8 )&0xff;
	Rtp_Packet_Data[11] = 0&0xff;

	m_SequenceNumber++;

	memcpy(&Rtp_Packet_Data[12],pNal,nalSize);

	Rtp_Packet_Len = nalSize + 12;

	int sendSize = 0;

	sendSize = send(g_RtpSocket, (char*)Rtp_Packet_Data, Rtp_Packet_Len, 0);

	if (sendSize <= 0)
		ret = -1;

	return ret;
}

int rtsp_TransportH264Nal(const char* pNal, unsigned int nalSize, int pts, bool isLast)
{
	if( nalSize < 5 )
		return 0;

	const int i_max = 1500 - 12;

	int i_nal_hdr;
	int i_nal_type;

	i_nal_hdr = pNal[3];
	i_nal_type = i_nal_hdr&0x1f;

	if( i_nal_type == 7 || i_nal_type == 8 )
	{
		return 0;
	}

	char* p_data = pNal;
	int	i_data = nalSize;

	p_data += 3;
	i_data -= 3;

	int writeSize = 0;

	if( i_data <= i_max )
	{
		writeSize = rtsp_rtp_send_data(p_data, i_data, pts, isLast);
		if (writeSize <= 0)
			return 0;
		return writeSize;
	}
	else
	{
		const int i_count = ( i_data - 1 + i_max - 2 - 1 ) / ( i_max - 2);
		int i;

		///trj:note//because of Fragment////Skip Nalu Header Byte
		p_data++;
		i_data--;

		char FU_Payload_Data[2000];

		for( i = 0; i < i_count; i++ )
		{
			const int i_payload =  (i_data < (i_max - 2)) ? i_data : (i_max - 2);
			const int nalSize = 2 + i_payload;

			//FU indicator
			FU_Payload_Data[0] = 0x00 | ( i_nal_hdr & 0x60) | 28;
			
			//FU header
			FU_Payload_Data[1] = ( i == 0 ? 0x80 : 0x00 ) | ( (i == i_count-1) ? 0x40 : 0x00 )  | i_nal_type;

			//FU payload
			memcpy( &FU_Payload_Data[2], p_data, i_payload );

			int iWrite = rtsp_rtp_send_data(FU_Payload_Data, nalSize, pts, isLast && (i == i_count - 1));
			if (iWrite > 0)
				writeSize += iWrite;

			i_data -= i_payload;

			p_data += i_payload;
		}
	}
	return writeSize;
}