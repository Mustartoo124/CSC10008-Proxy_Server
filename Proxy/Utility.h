#pragma once
#pragma comment(lib, "Ws2_32.lib")
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include<iostream>
#include<winsock2.h>
#include<vector>
#include<string>
#include<queue>
#include<process.h>
#include<fstream>
#include<ctime>
#include<thread>
#include<chrono>
#include<fstream>
#include<sstream>
#include<map>


#define PROXY_PORT 8888// Port của proxy
#define MAX_THREADS 30 // Số lượng thread tối đa
#define BUFFSIZE 4096// Kích cỡ mảng buffer
using namespace std;


struct ImageCacheInfo {
	string response;
	time_t expirationTime;
};

void cacheAdd(const string& requestLine, const string& response);  
void removeFromCache();                                            
void clearCacheThread();                                           
void InitThreadMutex();                                            


string getRequestLine(const string& request);  

bool isImage(const string& request);

int getMethod(const string& request);

void request_data(const string& request, string& hostName, string& port);

string getTDL(const string& hostName);

int responseType(const string& response);

DWORD getContentLength(const string& response);

bool validAccess(vector<string> whitelisting, string hostName, int method);

void sendForbiddenResponse(SOCKET clientSocket);

int getRequestFromClient(SOCKET& clientSocket, char* buffer, string& request);

DWORD Receive(SOCKET& serverSocket, WSABUF& responseBuffer, WSAOVERLAPPED& receive_Response_Overlapped, DWORD& Flag, const BOOL& fwait, DWORD lenToReceive);

void settleResponse(const string& hostName, const string& request, SOCKET& clientProxy);

unsigned __stdcall requestThread(void*);

void setup_whitelist(const string& fileName);

bool isInTime();

void timeThread();

void initProxy();
