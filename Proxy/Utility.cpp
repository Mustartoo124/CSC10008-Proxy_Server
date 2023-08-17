#include"Utility.h"

// Khai báo biến toàn cục
int cacheTime = 0;
vector<string> whitelisting;
int timeStart = 0, timeEnd = 0;

// Hàng đợi các yêu cầu từ client
queue<SOCKET> QUEUE_REQUEST_METHODS;

// Lưu trữ cache cho hình ảnh
map<string, ImageCacheInfo> imageCache;

// Mutex cho hàng đợi yêu cầu và cache hình ảnh
HANDLE Queue_Mutex;
HANDLE imageCacheMutex;

// Thêm dữ liệu vào cache
void cacheAdd(const string& requestLine, const string& response) {
	time_t expirationTime = time(nullptr) + cacheTime; // Thời gian hết hạn của cache
	WaitForSingleObject(imageCacheMutex, INFINITE); // Khóa mutex trước khi thêm vào imageCache
	imageCache[requestLine] = { response, expirationTime };
	ReleaseMutex(imageCacheMutex); 	// Mở khóa mutex sau khi thêm vào imageCache
}

// Xóa các mục hết hạn khỏi cache
void removeFromCache() {
	time_t currentTime = time(nullptr);
	WaitForSingleObject(imageCacheMutex, INFINITE); // Khóa mutex trước khi xóa các ảnh quá hạn khỏi imageCache
	for (auto it = imageCache.begin(); it != imageCache.end();) {
		if (currentTime >= it->second.expirationTime)
			it = imageCache.erase(it);
		else
			++it;
	}
	ReleaseMutex(imageCacheMutex); // Mở khóa mutex sau khi xóa các ảnh quá hạn khỏi imageCache
}

// Tạo thread cho xóa các ảnh đã quá cacheTime
void clearCacheThread() {
	while (true) {
		this_thread::sleep_for(chrono::minutes(5)); // kiểm tra cache sau mỗi 5p
		removeFromCache();
	}
}

// Hàm khởi tạo mutex và các luồng
void InitThreadMutex() {
	imageCacheMutex = CreateMutex(NULL, FALSE, NULL);
	thread cacheCleaner(clearCacheThread); // Tạo thread cho clearCacheThread
	cacheCleaner.detach();
}

// Lấy request line từ yêu cầu 
string getRequestLine(const string& request) { // OKE
	size_t getPos = request.find("\r\n");
	string requestLine = request.substr(0, getPos);
	return requestLine;
}

// Tìm kiếm trong request line có chứa các request cho hình ảnh hay không
bool isImage(const string& request) {
	string requestLine = getRequestLine(request);

	if (requestLine.find(".jpg") != string::npos) return true;
	if (requestLine.find(".jpeg") != string::npos) return true;
	if (requestLine.find(".png") != string::npos) return true;
	if (requestLine.find(".raw") != string::npos) return true;
	if (requestLine.find(".gif") != string::npos) return true;
	if (requestLine.find(".eps") != string::npos) return true;
	if (requestLine.find(".ico") != string::npos) return true;

	return false;
}
// Lấy phương thức từ yêu cầu 
int getMethod(const string& request) {
	if (request.size() < 3)
		return -1;
	if (request.substr(0, 3) == "GET")
		return 1;
	else if (request.substr(0, 4) == "POST")
		return 2;
	else if (request.substr(0, 4) == "HEAD")
		return 3;
	return -1;
}

// Trích xuất thông tin hostName và port từ yêu cầu
void request_data(const string& request, string& hostName, string& port) {
	// Tìm vị trí bắt đầu và kết thúc của "Host:"
	size_t start = request.find("Host:") + 6;
	size_t end = request.substr(start).find("\r\n");

	size_t k = (request.substr(start, end)).find(':');

	// Tách hostName và port dựa trên có hay không ký tự ":"
	if (k == string::npos || k == -1) {
		hostName = request.substr(start, end);
		port = "80"; // Nếu không có ":" thì sử dụng cổng mặc định 80
	}
	else {
		hostName = request.substr(start, k);
		port = request.substr(start + k + 1, end - (k + 1));
	}
}

// lấy tên miền chính từ (top-level domain) URL
string getTDL(const string& hostName) {
	if (hostName.substr(0, 4) == "www.")
		return hostName.substr(4);
	else
		return hostName;
}

// Tìm kiếm trong request line có chứa các request cho hình ảnh hay không
int responseType(const string& response) {
	if (response.find("Content-Length") || response.find("content-length"))
		return 1;
	else if (response.find("transfer-encoding") || response.find("Transfer-Encoding"))
		return 2;
	return -1;
}

// Tìm độ dài chứa trong content-length
DWORD getContentLength(const string& response) {
	int contentLength = 0;
	if (response.find("Content-Length:") != string::npos)
	{
		size_t start = response.find("Content-Length:") + 16;
		size_t end = response.substr(start).find("\r\n");
		contentLength = stoi(response.substr(start, end));
	}
	else if (response.find("content-length:") != string::npos)
	{
		size_t start = response.find("content-length:") + 16;
		size_t end = response.substr(start).find("\r\n");
		contentLength = stoi(response.substr(start, end));
	}

	return contentLength;
}


// Hàm nhận dữ liệu từ server đa luồng
DWORD Receive(SOCKET& serverSocket, WSABUF& responseBuffer, WSAOVERLAPPED& receive_Response_Overlapped, DWORD& Flag, const BOOL& fwait)
{
	DWORD bytesRecv = 0;
	//Nhận dữ liệu từ sever
	int iResult = WSARecv(serverSocket, &responseBuffer, 1, &bytesRecv, &Flag, &receive_Response_Overlapped, NULL);
	if (iResult == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		cerr << "Error when receiving\n";
		return SOCKET_ERROR;
	}
	//Đợi đến khi việc nhận dữ liệu hoàn thành
	iResult = WSAWaitForMultipleEvents(1, &receive_Response_Overlapped.hEvent, TRUE, 60, TRUE);
	if (iResult == WAIT_FAILED)
	{
		cerr << "Wait failed" << endl;
		return SOCKET_ERROR;
	}
	// Kiểm tra quá trình nhận dữ liệu kết thúc chưa
	iResult = WSAGetOverlappedResult(serverSocket, &receive_Response_Overlapped, &bytesRecv, fwait, &Flag);
	return bytesRecv;
}

//Nhận Response từ WebServer va gửi về cho Client
void settleResponse(const string& hostName, const string& request, SOCKET& clientProxy) {
	// truy vấn đến máy chủ (tên miền, danh sách IP kết nối đến máy chủ,...)
	struct hostent* pHost;
	pHost = gethostbyname(hostName.c_str());
	if (pHost == NULL) {
		cerr << "Failed to get host information\n";
		return;
	}
	SOCKET serverSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (serverSocket == INVALID_SOCKET) {
		cerr << "Failed to create socket: " << WSAGetLastError() << '\n';
		return;
	}

	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;//  sử dụng phiên bản IPv4
	memcpy(&serverAddr.sin_addr, pHost->h_addr, pHost->h_length);
	serverAddr.sin_port = htons(80);  // kết nôi đến port 80 dành cho HTTP
	if (connect(serverSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR_IN)) != 0)
	{
		cerr << "Failed to connect to server\n";
		closesocket(serverSocket);
		return;
	}

	DWORD bytesSend = 0; // lưu trữ số byte gửi thành công
	DWORD Flag = 0;

	//Thiết lập dữ liệu cần gửi (content, length)
	WSABUF requestBuffer;
	requestBuffer.buf = (char*)request.c_str();
	requestBuffer.len = strlen(request.c_str());

	//Thiết lập WSAOVERLAPPED cho quá trình gửi request lên server (xử lý đa luồng (I/O không đồng bộ))
	WSAOVERLAPPED send_Resquest_Overlapped;
	WSAEVENT sendEvent[1];
	sendEvent[0] = WSACreateEvent();
	send_Resquest_Overlapped.hEvent = sendEvent[0];

	//Gửi request lên server
	int iResult = WSASend(serverSocket, &requestBuffer, 1, &bytesSend, Flag, &send_Resquest_Overlapped, NULL);

	if (iResult == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING))
	{
		cout << "Error when sending!\n";
		return;
	}

	DWORD sendResult = WSAWaitForMultipleEvents(1, sendEvent, FALSE, WSA_INFINITE, FALSE);
	if (sendResult == WAIT_FAILED)
	{
		cerr << "Wait failed\n";
		return;
	}

	// Kiểm tra xem quá trình gửi đã kết thúc chưa
	DWORD bytesSent = 0;
	if (!WSAGetOverlappedResult(serverSocket, &send_Resquest_Overlapped, &bytesSent, FALSE, &Flag))
	{
		cerr << "Error when getting send result!\n";
		return;
	}

	DWORD bytesRecv = 0;
	char buffer[BUFFSIZE + 1];
	char sendBuf[BUFFSIZE + 1];
	string response = "";
	memset(buffer, 0, sizeof(buffer));
	memset(sendBuf, 0, sizeof(sendBuf));

	//Thiết lập dữ liệu cần nhận 
	WSABUF responseBuffer;
	responseBuffer.buf = buffer;
	responseBuffer.len = sizeof(buffer);

	//Thiết lập WSAOVERLAPPED cho quá trình nhận response từ server cho client
	WSAOVERLAPPED receive_Response_Overlapped;
	WSAEVENT recvEvent[1];
	recvEvent[0] = WSACreateEvent();
	receive_Response_Overlapped.hEvent = recvEvent[0];

	// Đợi đến khi resquest được chuyển lên server
	WSAWaitForMultipleEvents(1, sendEvent, FALSE, WSA_INFINITE, FALSE);
	// Kiểm tra xem quá trình gửi đã kết thúc chưa
	iResult = WSAGetOverlappedResult(serverSocket, &send_Resquest_Overlapped, &bytesSend, FALSE, &Flag);

	// Nhận response từ server
	bytesRecv = recv(serverSocket, responseBuffer.buf, BUFFSIZE, 0);

	int modeMessageBody = -1;
	response += responseBuffer.buf; // Thêm dữ liệu vào response
	memcpy(sendBuf, responseBuffer.buf, bytesRecv);
	send(clientProxy, sendBuf, bytesRecv, 0); //Chuyển dữ liệu cho client

	modeMessageBody = responseType(response);

	// Xử lý theo Content-Length và Transfer-Encoding
	if (modeMessageBody == 1) {
		// Độ lớn của response nhận được
		DWORD lenRequest = getContentLength(response);
		DWORD receivedBytes = bytesRecv;

		// Nhận thêm dữ liệu cho đến khi đủ Content-Length
		while (receivedBytes < lenRequest) {
			bytesRecv = Receive(serverSocket, responseBuffer, receive_Response_Overlapped, Flag, TRUE);

			if (bytesRecv == 0 || bytesRecv == SOCKET_ERROR) {
				cerr << "Error receiving data! " << WSAGetLastError() << '\n';
				break;
			}

			response += responseBuffer.buf; // Thêm dữ liệu vào response
			receivedBytes += bytesRecv;

			// Gửi dữ liệu đã nhận về cho client
			send(clientProxy, responseBuffer.buf, bytesRecv, 0);
		}
	}
	// Xử lý theo Transfer-Encoding
	else if (modeMessageBody == 2) {
		while (response.find("\r\n0\r\n\r\n") == string::npos) {
			bytesRecv = Receive(serverSocket, responseBuffer, receive_Response_Overlapped, Flag, FALSE);

			if (bytesRecv == 0 || bytesRecv == SOCKET_ERROR) {
				cerr << "Error receiving data! " << WSAGetLastError() << '\n';
				break;
			}

			response += responseBuffer.buf; // Thêm dữ liệu vào response

			// Gửi dữ liệu đã nhận về cho client
			send(clientProxy, responseBuffer.buf, bytesRecv, 0);
		}
	}
	string requestLine = getRequestLine(request);
	if (isImage(request) && imageCache.count(requestLine) == 0) {
		cacheAdd(requestLine, response);
	}
	cout << "RESPONSE : \n" << response << '\n';
	cout << "Close socket!\n";
	shutdown(serverSocket, 2);
	closesocket(serverSocket);

	return;
}

// Nhận thông tin yêu cầu từ client
int getRequestFromClient(SOCKET& clientSocket, char* buffer, string& request) {
	request = ""; // Khởi tạo chuỗi yêu cầu

	int lenRequest = 0;
	int bytesRead = 0;

	// Nhận dữ liệu từ client cho đến khi gặp dòng trống (kết thúc yêu cầu)
	do {
		bytesRead = recv(clientSocket, buffer, BUFFSIZE, 0);

		if (bytesRead <= 0) {
			cerr << "Error receiving request or request is empty.\n";
			closesocket(clientSocket); // Đóng kết nối
			return 0;
		}

		// Thêm dữ liệu nhận được vào chuỗi yêu cầu
		request += string(buffer, bytesRead);

		// Kiểm tra xem chuỗi yêu cầu đã kết thúc chưa
		if (request.find("\r\n\r\n") != string::npos) {
			break;
		}
	} while (true);

	return 1; // Trả về 1 nếu nhận yêu cầu thành công
}



// Gửi phản hồi mã lỗi 403 (Forbidden)
void sendForbiddenResponse(SOCKET clientSocket) {
	string response = "HTTP/1.1 403 Forbidden\r\n";
	response += "Content-Type: text/html\r\n";
	response += "Connection: close\r\n";
	response += "\r\n";
	response += "<html><body><h1>403 Forbidden</h1><p>Your access is forbidden.</p></body></html>";

	send(clientSocket, response.c_str(), response.size(), 0);
}

// Luồng xử lý yêu cầu từ client
unsigned __stdcall requestThread(void*) {
	while (true) {
		SOCKET clientSocket;
		// Tránh chồng chéo các tiến trình (xử lý tuần tự các yêu cầu từ client)
		WaitForSingleObject(Queue_Mutex, INFINITE);
		if (!QUEUE_REQUEST_METHODS.size()) {
			ReleaseMutex(Queue_Mutex);
			continue;
		}
		// Xử lý yêu cầu từ hàng đợi
		try {
			clientSocket = QUEUE_REQUEST_METHODS.front();
			QUEUE_REQUEST_METHODS.pop();
			ReleaseMutex(Queue_Mutex);
		}
		catch (exception) {
			ReleaseMutex(Queue_Mutex);
			continue;
		}
		ReleaseMutex(Queue_Mutex);
		try {
			char buffer[BUFFSIZE + 1] = { 0 };
			memset(buffer, 0, sizeof(buffer));
			string request = "";
			if (!getRequestFromClient(clientSocket, buffer, request))
				return 0;

			string hostName, port;
			request_data(request, hostName, port);

			// Kiểm tra tính hợp lệ của yêu cầu và port
			if (validAccess(whitelisting, hostName, getMethod(request)) && port == "80") {
				cout << "REQUEST : \n" << request << '\n';
				string requestLine = getRequestLine(request);

				// Kiểm tra xem yêu cầu có phải là ảnh và có trong cache không
				if (isImage(request) && imageCache.count(requestLine) > 0) {
					auto cacheInfo = imageCache[requestLine];
					if (time(nullptr) < cacheInfo.expirationTime) {
						// Gửi phản hồi từ cache đến client
						cout << "Data from cache was sent into client! \n";
						const char* data = cacheInfo.response.c_str();
						size_t dataLen = cacheInfo.response.size();
						send(clientSocket, data, (int)dataLen, 0);
					}
				}
				else {
					// Xử lý và gửi phản hồi từ máy chủ gốc
					settleResponse(hostName, request, clientSocket);
				}
			}
			else {
				// Gửi mã lỗi 403 (Forbidden)
				sendForbiddenResponse(clientSocket);
			}

			shutdown(clientSocket, 2);
			closesocket(clientSocket);
		}
		// Không xử lý các yêu cầu khác
		catch (exception) {
			shutdown(clientSocket, 2);
			closesocket(clientSocket);
		}
	}
	return -1;
}



// Đọc cấu hình whitelist từ file và thiết lập thông tin
void setup_whitelist(const string& fileName) {
	ifstream ifs;
	ifs.open(fileName.c_str(), ios::in);
	if (ifs.is_open()) {
		string line;
		int lineIndex = 0;
		while (getline(ifs, line, '\n')) {
			if (lineIndex == 0) {
				// Đọc giá trị cacheTime từ dòng đầu tiên của file cấu hình
				stringstream ss(line);
				string temp;
				getline(ss, temp, '=');
				ss >> cacheTime;
				ss.clear();
			}
			else if (lineIndex == 1) {
				// Đọc danh sách whitelist từ dòng thứ hai của file cấu hình
				stringstream ss(line);
				string temp;
				getline(ss, temp, '=');
				while (getline(ss, temp, ',')) {
					whitelisting.push_back(temp);
				}
			}
			else if (lineIndex == 2) {
				// Đọc khoảng thời gian hoạt động từ dòng thứ ba của file cấu hình
				stringstream ss(line);
				string temp;
				getline(ss, temp, '=');
				ss >> timeStart;
				ss.ignore();
				ss >> timeEnd;
			}
			lineIndex++;
		}
	}
	else {
		cerr << "Cannot set up white list !\n";
	}
}


// Kiểm tra tính hợp lệ của truy cập dựa trên whitelist và phương thức truy cập
bool validAccess(vector<string> whitelisting, string hostName, int method) {
	// Kiểm tra tính hợp lệ của phương thức truy cập
	if (method == -1)
		return false;

	// Kiểm tra tính hợp lệ của hostName dựa trên danh sách whitelist
	bool check_hostName = false;

	for (string x : whitelisting) {
		if (x == getTDL(hostName))
			check_hostName = true;
	}

	if (!check_hostName)
		return false;

	return true;
}


// Kiểm tra xem chương trình có hoạt động trong khoảng thời gian cho phép hay không
bool isInTime() {
	time_t curTime;
	struct tm* timeInfo;
	time(&curTime); // Lấy thời gian hệ thống
	timeInfo = localtime(&curTime); // Chuyển đổi thời gian sang cấu trúc tm
	int curHour = timeInfo->tm_hour; // Lấy giờ hiện tại

	// Kiểm tra xem giờ hiện tại có nằm trong khoảng thời gian cho phép không
	if (curHour >= timeStart && curHour < timeEnd)
		return true;
	return false;
}


// Tạo luồng để kiểm tra thời gian truy cập hợp lệ
void timeThread() {
	while (true) {
		// Kiểm tra nếu thời gian truy cập không hợp lệ
		if (!isInTime()) {
			cout << "Unvalid access time \n";
			exit(1); // Dừng chương trình
		}

		// Ngừng thực hiện trong 1 phút trước khi kiểm tra lại
		this_thread::sleep_for(std::chrono::minutes(1));
	}
}

// Khởi tạo máy chủ proxy để lắng nghe yêu cầu truy cập từ client
void initProxy() {
	// Khởi tạo Winsock
	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cerr << "Cannot create winsock! \n";
		return;
	}

	SOCKET proxyServer, clientSocket;
	SOCKADDR_IN clientAddr;
	memset(&clientAddr, 0, sizeof(clientAddr));

	// Tạo socket proxy sử dụng giao thức AF_INET (IPv4), kiểu SOCK_STREAM (TCP)
	proxyServer = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (proxyServer == INVALID_SOCKET) {
		cerr << "Cannot create socket! " << WSAGetLastError() << '\n';
		WSACleanup();
		return;
	}

	clientAddr.sin_family = AF_INET; // Sử dụng IPv4
	clientAddr.sin_port = htons(PROXY_PORT); // Sử dụng cổng 8888 (được chuyển thành big-endian)
	clientAddr.sin_addr.s_addr = htonl((INADDR_ANY)); // Chấp nhận kết nối từ tất cả các địa chỉ IP trên máy

	// Liên kết địa chỉ với socket proxy để xác định máy chủ proxy trên mạng
	if (bind(proxyServer, (SOCKADDR*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
		cerr << "Error : " << WSAGetLastError() << '\n';
		WSACleanup();
		return;
	}

	// Lắng nghe các yêu cầu kết nối đến máy chủ proxy với hàng đợi chờ kết nối
	if (listen(proxyServer, 25)) {
		cerr << "Cannot listen to client with error : " << WSAGetLastError() << '\n';
		WSACleanup();
		return;
	}

	// Tạo mutex để quản lý các yêu cầu truy cập
	Queue_Mutex = CreateMutex(NULL, FALSE, NULL);

	// Tạo và khởi chạy luồng xử lý các yêu cầu truy cập từ người dùng
	HANDLE threads[MAX_THREADS];
	unsigned int threadId[MAX_THREADS];
	for (int i = 0; i < MAX_THREADS; i++) {
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, requestThread, NULL, 0, &threadId[i]);
	}

	while (true) {
		// Chấp nhận yêu cầu kết nối từ client
		clientSocket = accept(proxyServer, NULL, NULL);
		if (clientSocket == INVALID_SOCKET) {
			cerr << "Error in accepted connection : " << WSAGetLastError() << '\n';
			closesocket(clientSocket);
		}
		else {
			WaitForSingleObject(Queue_Mutex, INFINITE);
			QUEUE_REQUEST_METHODS.push(clientSocket);
			ReleaseMutex(Queue_Mutex);
		}
	}

	// Đóng tất cả liên kết gửi/nhận với client
	shutdown(proxyServer, 2);
	closesocket(proxyServer);

	// Giải phóng Winsock
	WSACleanup();
}
