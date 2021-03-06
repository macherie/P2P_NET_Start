#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib> 
#include <list>
#include <pthread.h>
#include <stdio.h>
#include <udt.h>

#ifndef WIN32
#include <cstdlib>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/epoll.h>            /* epoll头文件 */   
#include <fcntl.h>                    /* nonblocking需要 */   
#include <sys/resource.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#endif

#include "udp.h"
#include "stun.h"
#include "test_util.h"
#include "debug.h"
#include "nettool.h"
#include "net_def.h"

using namespace std;

int gRunning=1;
UserList ClientList;	//客户节点列表
int sockSrv;


//******************************************************************************************************************
stUserListNode GetUser(const char *userName)
{
	for (UserList::iterator ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
	{
		if (strcmp((*ClientList_iter)->userName, userName) == 0)
			return *(*ClientList_iter);
	}
	
	std::cout << "can not find user: " << userName << std::endl;
}


bool checkUserList(const char *userName)
{
	bool isExist = false;

	for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
	{
		if (strcmp((*ClientList_iter)->userName, userName) == 0)
		{
			isExist = true;
			break;
		}
	}

	return isExist;
}

void removeUser(const char *userName)
{
	for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
	{
		if (strcmp((*ClientList_iter)->userName, userName) == 0)
		{
			ClientList_iter = ClientList.erase(ClientList_iter);
		}
	}
}

void *p2p_srvProc(void *arg)
{
	pthread_detach(pthread_self());
	
	sockaddr_in sender;
	char buf[MAX_PACKET_SIZE] = {0};

	while (1)
	{
		stCommMsg* mRecvMsg = (stCommMsg*)buf;
		socklen_t nLen = sizeof (sender);
		int ret = recvfrom(sockSrv, (char*)mRecvMsg , MAX_PACKET_SIZE, 0, (sockaddr*)&sender, &nLen);

		if (ret < 0)
		{
			std::cout << "recv error " << std::endl;
			continue;
		}
		else
		{
			unsigned int mMsgType = mRecvMsg->uiMsgType;
			switch (mMsgType)
			{
				case MSG_LOGIN_REQ:
					{
						std::cout << "has a client login, user name: " << mRecvMsg->cMyName << std::endl;

						//insert this node
						char sendBuf[MAX_PACKET_SIZE] = {0};
						stCommMsg* sendbuf = (stCommMsg*) sendBuf;
						
						stUserListNode* currentUser = new stUserListNode;
						memcpy(currentUser->userName, mRecvMsg->cMyName, MAX_NAME_SIZE);
						currentUser->uiIP = ntohl(sender.sin_addr.s_addr);
						currentUser->usPORT = ntohs(sender.sin_port);

						if(ClientList.size() > 0)
						{
							//check if the same
							bool isExist = false;
							isExist = checkUserList(mRecvMsg->cMyName);

							if(!isExist)
							{
								ClientList.push_back(currentUser);			//  do not exclude same name user
							}
							else
							{
								ClientList.remove(currentUser);

								sendbuf->uiMsgType = MSG_LOGIN_RESP;
								sendbuf->result = 1;
								
								sendto(sockSrv, (const char*)sendbuf, sendbuf->getSize(),0, 
								(const sockaddr*)&sender, sizeof(sender));
								
								break;
							}
						}
						else
						{
							ClientList.push_back(currentUser);			//  do not exclude same name user
						}

						//send user list info to server
						sendbuf->uiMsgType = MSG_LOGIN_RESP;
						sendbuf->result = 0;
						
						for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
						{
							memcpy(sendbuf->userList[sendbuf->userNums].userName, (*ClientList_iter)->userName,MAX_NAME_SIZE);
							sendbuf->userList[sendbuf->userNums].uiIP = (*ClientList_iter)->uiIP;
							sendbuf->userList[sendbuf->userNums].usPORT = (*ClientList_iter)->usPORT;
							++sendbuf->userNums;
						}

						sendto(sockSrv, (const char*)sendbuf, sendbuf->getSize(),0, 
								(const sockaddr*)&sender, sizeof(sender));
						
						break;
					}
				
				case MSG_LOGOUT_REQ:
					{
						std::cout << "has a client logout, name:" << mRecvMsg->cMyName << std::endl;

						removeUser(mRecvMsg->cMyName);
						
					/*	for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
						{
							if (strcmp((*ClientList_iter)->userName, mRecvMsg->cMyName) == 0)
							{
								ClientList_iter = ClientList.erase(ClientList_iter);
							}
						}*/

						break;
					}
				
				case MSG_GETALLUSER_REQ:
					{
						char sendBuf[MAX_PACKET_SIZE] = {0};
						stCommMsg* sendbuf = (stCommMsg*) sendBuf;
						
						sendbuf->uiMsgType = MSG_GETALLUSER_RESP;
						for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
						{
							memcpy(sendbuf->userList[sendbuf->userNums].userName, (*ClientList_iter)->userName,MAX_NAME_SIZE);
							sendbuf->userList[sendbuf->userNums].uiIP = (*ClientList_iter)->uiIP;
							sendbuf->userList[sendbuf->userNums].usPORT = (*ClientList_iter)->usPORT;
							++sendbuf->userNums;
						}

						sendto(sockSrv, (const char*)sendbuf, sendbuf->getSize(),0, 
								(const sockaddr*)&sender, sizeof(sender));
						unsigned int nodecount = ClientList.size();
						
						std::cout << "want get all user list" << nodecount << std::endl;

						break;
					}

				case MSG_HEART_CHECK_REQ:
					{
						char sendBuf[MAX_PACKET_SIZE] = {0};
						stCommMsg* sendbuf = (stCommMsg*) sendBuf;
						
						sendbuf->uiMsgType = MSG_HEART_CHECK_RESP;
						for (UserList::iterator  ClientList_iter = ClientList.begin(); ClientList_iter != ClientList.end(); ++ClientList_iter)
						{
							memcpy(sendbuf->userList[sendbuf->userNums].userName, (*ClientList_iter)->userName,MAX_NAME_SIZE);
							sendbuf->userList[sendbuf->userNums].uiIP = (*ClientList_iter)->uiIP;
							sendbuf->userList[sendbuf->userNums].usPORT = (*ClientList_iter)->usPORT;
							++sendbuf->userNums;
						}

						sendto(sockSrv, (const char*)sendbuf, sendbuf->getSize(),0, 
								(const sockaddr*)&sender, sizeof(sender));
						unsigned int nodecount = ClientList.size();
						
						std::cout << "recv heart check msg" << nodecount << std::endl;
						
						break;
					}
				
				case MSG_CONNECT_REMOTE_REQ:
					{
						//recv connet remote request.
						bool isExist = checkUserList(mRecvMsg->cToName);
						if(!isExist)
						{
							printf("User does not exist!!!\n");
							
							char sendBuf[MAX_PACKET_SIZE] = {0};
							stCommMsg* sendbuf = (stCommMsg*) sendBuf;
						
							sendbuf->uiMsgType = MSG_CONNECT_REMOTE_RESP;
							sendbuf->result = 1;

							sendto(sockSrv, (const char*)sendbuf, sendbuf->getSize(),0, 
								(const sockaddr*)&sender, sizeof(sender));
						}
						else
						{
							stUserListNode toPeer=GetUser(mRecvMsg->cToName);

							sockaddr_in remote;
							remote.sin_family=AF_INET;
							remote.sin_port=htons(toPeer.usPORT);
							remote.sin_addr.s_addr=htonl(toPeer.uiIP);

							in_addr temp;
							temp.s_addr=htonl(toPeer.uiIP);
							
 							printf("%s [%s:%d] want to connect to %s [%s:%d]\n", mRecvMsg->cMyName, inet_ntoa(sender.sin_addr), 
								htons(sender.sin_port), toPeer.userName, inet_ntoa(temp), toPeer.usPORT);

							stCommMsg mTransMsg;
							mTransMsg.uiMsgType = P2PSOMEONEWANTTOCALLYOU;
							mTransMsg.transMsg.uiIP = ntohl(sender.sin_addr.s_addr); 
							mTransMsg.transMsg.usPORT = ntohs(sender.sin_port);
							strcpy(mTransMsg.transMsg.userName, mRecvMsg->cMyName);

							sendto(sockSrv, (const char*)&mTransMsg, sizeof(stCommMsg),0,(const sockaddr*)&remote,sizeof(remote));
						}
						break;
					}
				
				case P2PHAVECONNECT1:
				{
					stUserListNode toPeer=GetUser(mRecvMsg->cToName);

					sockaddr_in remote;
					remote.sin_family=AF_INET;
					remote.sin_port=htons(toPeer.usPORT);
					remote.sin_addr.s_addr=htonl(toPeer.uiIP);

					stCommMsg mTransMsg;
					mTransMsg.uiMsgType = P2PCANSTART;
					mTransMsg.transMsg.uiIP = ntohl(sender.sin_addr.s_addr); 
					mTransMsg.transMsg.usPORT = ntohs(sender.sin_port);
					strcpy(mTransMsg.transMsg.userName, mRecvMsg->cMyName);

					sendto(sockSrv, (const char*)&mTransMsg, sizeof(stCommMsg),0,(const sockaddr*)&remote,sizeof(remote));
							
					break;
				}

				case RELAYMESSAGE:
				{
					stUserListNode toPeer = GetUser(mRecvMsg->cToName);

					sockaddr_in remote;
					remote.sin_family = AF_INET;
					remote.sin_port = htons(toPeer.usPORT);
					remote.sin_addr.s_addr = htonl(toPeer.uiIP);
					
					stCommMsg MessageHead;
					MessageHead.uiMsgType = RELAYMESSAGE2;
					strcpy(MessageHead.cMyName, mRecvMsg->cMyName);
					strcpy(MessageHead.cToName, mRecvMsg->cToName);
				//	MessageHead.p2pMsg.uiSendLen = (int)strlen(recvmessage)+1;
				//	strcpy(MessageHead.p2pMsg.cP2PCommUserName, mRecvMsg->cMyName);
					printf("%s -> %s, %d\n", mRecvMsg->cMyName, mRecvMsg->cToName, mRecvMsg->rlyMsg.uiSendLen);
						
					char *recvmessage = new char[mRecvMsg->rlyMsg.uiSendLen];
					int recv = recvfrom(sockSrv, recvmessage, MAX_PACKET_SIZE, 0, (sockaddr*)&sender, &nLen);
					printf("\r\nrecv data len = %d\r\n", recv);
					recvmessage[recv - 1] = '\0';
					if (recv <= 0)
					{
						printf("recv message error!\n");
					}
					else
					{
						if(mRecvMsg->rlyMsg.uiSendLen == recv)
						{
							MessageHead.rlyMsg.uiSendLen = (int)strlen(recvmessage)+1;
							strcpy(MessageHead.rlyMsg.sUserName, mRecvMsg->cMyName);
							
							//����p2p��Ϣͷ
							int send_count = sendto(sockSrv, (const char*)&MessageHead, sizeof(MessageHead), 0, 
							(const sockaddr*)&remote, sizeof(remote));

							printf("recv relay message : %s from : %s to : %s\n", recvmessage, mRecvMsg->cMyName, 
							mRecvMsg->cToName);
							
							//����p2p��Ϣ��
							send_count = sendto(sockSrv, (const char*)&recvmessage, MessageHead.rlyMsg.uiSendLen, 0, 
							(const sockaddr*)&remote, sizeof(remote));
						}
					}

					delete[] recvmessage;
						
					
				//	sendto(PrimaryUDP, (const char*)&mTransMsg, sizeof(stCommMsg),0,(const sockaddr*)&remote,sizeof(remote));
					break;
				}
			}
			
		}
	}

	pthread_exit((void*)pthread_self());
}

int main(int argc, char* argv[])
{
	int ret;
	pthread_t th_p2p_proc;
	
	sockSrv = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockSrv < 0)
	{
		std::cout << "Create socket error!!!" << std::endl;
		return 0;
	}

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if (bind(sockSrv, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		std::cout << "bind socket failed!" << std::endl;
		return 0;
	}

	ret = pthread_create(&th_p2p_proc, NULL, p2p_srvProc, (void *)NULL);
	if ( 0 != ret )
	{
		cerr << "Create stun server pthread fail" <<endl;
	    return -1;
	}

	printf("***** P2P server started. *****\n");

	while(1)
	{
	
	}

	close(sockSrv);

   	return 0;
}


#if 0 //old
int setSrvNetInfo(char *ips, SERVER_INFO *srvInfo)
{
	const char * split = ",";
	char *p;
	p = strtok (ips,split);
	int i=0;
	char addr[10][16];

	while(p!=NULL) {
	//	printf ("%s\n",p);
		if(strcmp(p, "127.0.0.1") != 0)
		{
			strcpy(addr[i], p);
		//	printf("%s\n", addr[i]);
			i++;
		}
		
		p = strtok(NULL,split);
	}

//	printf("num=%d\n", i);
	if(i < 2)
		return -1;

	strcpy(srvInfo->srvIpAddr1, addr[0]);
	strcpy(srvInfo->srvIpAddr2, addr[1]);

//	printf("server ip addr1:%s\n", srvInfo->srvIpAddr1);
//	printf("server ip addr2:%s\n", srvInfo->srvIpAddr2);

	return 0;
}

int setnonblocking( int fd )  
{  
    if( fcntl( fd, F_SETFL, fcntl( fd, F_GETFD, 0 )|O_NONBLOCK ) == -1 )  
    {  
        printf("Set blocking error : %d\n", errno);  
        return -1;  
    }  
    return 0;  
} 

void* pthread_handle_message(void* para)
{
//    char recvBuf[1024] = {0}; 
    int ret = 0;
    int rs = 1;
    PEER_INFO peer = *(PEER_INFO *)para;
    char *buffer;
    buffer = (char *)malloc(NET_SEND_IO_BUFFER_SIZE*2);

    while(rs)
    {
        ret = recv(peer.peerSkt, (char *)&buffer, sizeof(buffer), 0);

        if(ret < 0)
        {
            if(errno == EAGAIN)
            {
                printf("EAGAIN\n");
                break;
            }
            else{
                printf("recv error! errno:%d\n", errno);
        
                close(peer.peerSkt);
                break;
            }
        }
        else if(ret == 0)
        {
            rs = 0;
        }
	  else
        {
            rs = 1;
		
		P2P_STREAM_IO_HEAD * pStreamHead = NULL;
    		pStreamHead = (P2P_STREAM_IO_HEAD *)buffer;
	
		if( pStreamHead->u8StreamIOType == SIO_TYPE_IOCTRL)
		{
			P2P_IO_CTRL_HEAD * pstIoCtrlHead = NULL;
			pstIoCtrlHead = (P2P_IO_CTRL_HEAD *)(buffer + sizeof(P2P_STREAM_IO_HEAD));
			
			switch(pstIoCtrlHead->u16IOCtrlType)
			{
				case IOCTRL_TYPE_LOGIN_REQ:
					printf(YELLOW "Receive IOCTRL_TYPE_LOGIN_REQ message from %s:%d\n" NONE, inet_ntoa(peer.peer_addr.sin_addr)
				, htons(peer.peer_addr.sin_port));
					break;

				default:
					break;
			}
			//	logResp.status = 1;
			//	send(peer.peerSkt, (char *)&logResp, sizeof(logResp), 0);
		}
        }
    }
    free(buffer);
    close(peer.peerSkt);
}
#endif //old

#if 0 //for stun server
void usage()
{
   cerr << "Usage: " << endl
        << " ./server [-v] [-h] [-h IP_Address] [-a IP_Address] [-p port] [-o port] [-m mediaport]" << endl
        << " " << endl
        << " If the IP addresses of your NIC are 10.0.1.150 and 10.0.1.151, run this program with" << endl
        << "    ./server -v  -h 10.0.1.150 -a 10.0.1.151" << endl
        << " STUN servers need two IP addresses and two ports, these can be specified with:" << endl
        << "  -h sets the primary IP" << endl
        << "  -a sets the secondary IP" << endl
        << "  -p sets the primary port and defaults to 3478" << endl
        << "  -o sets the secondary port and defaults to 3479" << endl
        << "  -b makes the program run in the backgroud" << endl
        << "  -m sets up a STERN server starting at port m" << endl
        << "  -v runs in verbose mode" << endl
      // in makefile too
        << endl;
}

#ifndef WIN32
void *stunServerThreadProc(void *arg)
#else
DWORD WINAPI stunServerThreadProc(LPVOID);
#endif
{
	printf("====>startSTUNserver thread start!!!\n"); 
	
	SERVER_INFO *st = (SERVER_INFO*)arg;
	
	assert( sizeof(UInt8 ) == 1 );
	assert( sizeof(UInt16) == 2 );
	assert( sizeof(UInt32) == 4 );

	initNetwork(); //just for windows os

//	clog << "STUN server version "  <<  STUN_VERSION << endl;
	  
	StunAddress4 myAddr;
	StunAddress4 altAddr;
	bool verbose=false;

	myAddr.addr = 0;
	altAddr.addr = 0;
	myAddr.port = STUN_PORT;
	altAddr.port = STUN_PORT+1;
//	int myPort = 0;
//	int altPort = 0;
	int myMediaPort = 0;

	stunParseServerName(st->srvIpAddr1, myAddr);
	stunParseServerName(st->srvIpAddr2, altAddr);

	if ( myAddr.port == altAddr.port )
	{
		altAddr.port = myAddr.port+1;
	}

	StunServerInfo info;
	bool ok = stunInitServer(info, myAddr, altAddr, myMediaPort, verbose);

	int c=0;
	while (ok)
	{
		ok = stunServerProcess(info, verbose);
		c++;
		if ( verbose && (c%1000 == 0) ) 
		{
			clog << "*";
		}
	}
}
#endif //for stun server

