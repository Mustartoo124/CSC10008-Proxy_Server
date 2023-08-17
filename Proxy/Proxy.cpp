#include"Utility.h"

int main() {
	setup_whitelist("Information.conf");
	InitThreadMutex();
	thread timeChecker(timeThread);
	cout << "Proxy is running \n";
	initProxy();
	timeChecker.join();
	system("pause");
	return 0;
}