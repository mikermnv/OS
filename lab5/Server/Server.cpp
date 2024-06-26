#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#define BUFSIZE 512

std::vector<HANDLE> hClientsProcess;
std::vector<HANDLE> hClientsThread;

struct ThreadParams
{
	HANDLE hpipe;
	std::string filename;
	int clientId;
};

//requests
char get_to_modify[] = "read %d \0";
char modify[] = "modw %d %s %f \0";
char exit_cycle[] = "exit \0";
char release_read[] = "relr %d \0";
char release_write[] = "relw %d \0";

//responses
std::string data = "data ";
char modif_data_resp[] = "updt %d \0";
char not_found[] = "404_ \0";
char server_internal_error[] = "500_ \0";
char ok[] = "200_ \0";

const wchar_t shared_pipe_name[] = TEXT("\\\\.\\pipe\\SERVER_REY_PIPE_%d");

DWORD WINAPI InstanceThread(LPVOID lpvParam);
std::vector<std::string> ParsedRequest(char* request);
VOID Send(char* response, HANDLE hPipe);
VOID Receive(char* request, HANDLE hPipe);
VOID ProcessRequest(char* request, HANDLE hPipe, std::string filename, int clientID, std::vector<bool>* isReading, std::vector<bool>* isWriting);
VOID Respond500(HANDLE hPipe);
VOID Respond400(HANDLE hPipe);

//for file sync
std::vector<int> readers_counters;
std::vector<HANDLE> hModifyResourceMutex;
std::vector<HANDLE> hModifyCounterMutex;

int recordsNumber = 0;

struct Employee
{
	int ID;
	char name[10];
	double hours;
};

void Write_Block(int i, Employee newEmployee, std::string filename, std::vector<bool>* isWriting)
{
	auto res = WaitForSingleObject(hModifyResourceMutex[i], INFINITE);
	(*isWriting)[i] = true;
	std::fstream fout(filename);
	//fout.open(filename, std::ofstream::binary);

	fout.seekp(sizeof(Employee) * i, std::ios::beg);
	fout.write((const char*)&newEmployee, sizeof(Employee));
	fout.close();
}

void Write_Release(int i, std::vector<bool>* isWriting)
{
	(*isWriting)[i] = false;
	ReleaseMutex(hModifyResourceMutex[i]);
}

Employee Read_Block(int i, int ID, std::string filename, std::vector<bool>* isReading)
{
	WaitForSingleObject(hModifyCounterMutex[i], INFINITE);
	if (!(*isReading)[i])
	{
		readers_counters[i]++;
		(*isReading)[i] = true;
	}

	if (readers_counters[i] == 1)
		WaitForSingleObject(hModifyResourceMutex[i], INFINITE); // Wait to read and do not let to write
	ReleaseMutex(hModifyCounterMutex[i]);
	std::ifstream fin;
	fin.open(filename);

	//read
	fin.seekg(sizeof(Employee) * i);
	Employee e;
	fin.read((char*)&e, sizeof(Employee));
	fin.close();
	return e;
}

void Read_Release(int i, std::vector<bool>* isReading)
{
	WaitForSingleObject(hModifyCounterMutex[i], INFINITE);
	if ((*isReading)[i]) {
		readers_counters[i]--;
		(*isReading)[i] = false;
	}
	if (readers_counters[i] == 0)
		ReleaseMutex(hModifyResourceMutex[i]); // Let writers write
	ReleaseMutex(hModifyCounterMutex[i]);
}

int main() {

	//File
	int lastUsedId = 0;
	std::ofstream out;
	std::string filename;
	std::cout << "Hi! I am server! Please, enter FILE NAME for employee data:\n";
	std::cin >> filename;
	out.open(filename, std::ofstream::binary | std::ofstream::trunc);

	int amountOfRecords = 0;
	std::cout << "Please, enter amount of records: ";
	std::cin >> amountOfRecords;
	recordsNumber = amountOfRecords;
	for (size_t i = 0; i < amountOfRecords; i++)
	{
		Employee e;
		lastUsedId++;
		std::cout << "Employee #" << lastUsedId << "\nName: ";
		std::string name;
		std::cin >> name;

		int max = 10;
		if (name.size() < 10)
			max = name.size();

		for (size_t k = 0; k < max; k++)
			e.name[k] = name[k];
		for (size_t k = max; k < 10; k++)
		{
			e.name[k] = '\0';
		}

		double hours = 0;
		std::cout << "Enter hours: ";
		std::cin >> hours;
		e.ID = lastUsedId;
		e.hours = hours;

		out.write((const char*)&e, sizeof(Employee));
	}

	out.close();
	std::cout << "Data successfully writen!\n";

	std::ifstream in;
	in.open(filename);

	Employee employee;

	std::cout << "\n";

	for (size_t i = 0; i < amountOfRecords; i++)
	{
		in.read((char*)&employee, sizeof(Employee));
		std::cout << "Id:    #" << employee.ID << "\n";
		std::cout << "Name:  " << employee.name << "\n";
		std::cout << "Hours: " << employee.hours << "\n\n";
	}

	in.seekg(0);

	// Initialize mutex

	for (size_t i = 0; i < amountOfRecords; i++)
	{
		readers_counters.push_back(0);
		hModifyResourceMutex.push_back(CreateMutex(
			NULL,              // default security attributes
			FALSE,             // initially not owned
			NULL));
		hModifyCounterMutex.push_back(CreateMutex(
			NULL,              // default security attributes
			FALSE,             // initially not owned
			NULL));
	}

	// Client Server
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	int clientsNumber = 0;
	std::cout << "Enter number of clients: ";

	std::cin >> clientsNumber;

	for (int i = 0; i < clientsNumber; i++)
	{
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		wchar_t buffer[1000];
		wsprintfW(buffer, L"Client.exe %d", i + 1);

		// Start the child process. 
		if (!CreateProcess(NULL,   // No module name (use command line)
			buffer,        // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			CREATE_NEW_CONSOLE,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi)           // Pointer to PROCESS_INFORMATION structure
			)
		{
			printf("CreateProcess failed (%d).\n", GetLastError());
			return 0;
		}

		hClientsProcess.push_back(pi.hProcess);
		hClientsThread.push_back(pi.hThread);
	}

	BOOL   fConnected = FALSE;
	DWORD  dwThreadId = 0;
	HANDLE hPipe = INVALID_HANDLE_VALUE, hThread = NULL;

	for (int i = 0; i < clientsNumber; i++)
	{
		ThreadParams* params = new ThreadParams;
		params->filename = filename;
		wchar_t buffer[1000];
		wsprintf(buffer, shared_pipe_name, i + 1);

		hPipe = CreateNamedPipe(
			buffer,             // pipe name 
			PIPE_ACCESS_DUPLEX,       // read/write access 
			PIPE_TYPE_MESSAGE |       // message type pipe 
			PIPE_READMODE_MESSAGE |   // message-read mode 
			PIPE_WAIT,                // blocking mode 
			PIPE_UNLIMITED_INSTANCES, // max. instances  
			BUFSIZE,                  // output buffer size 
			BUFSIZE,                  // input buffer size 
			0,                        // client time-out 
			NULL);                    // default security attribute 

		params->hpipe = hPipe;
		params->clientId = i + 1;

		if (hPipe == INVALID_HANDLE_VALUE)
		{
			std::cout << "CreateNamedPipe failed, GLE=%d.\n";
			return -1;
		}

		// Wait for the client to connect; if it succeeds, 
		// the function returns a nonzero value. If the function
		// returns zero, GetLastError returns ERROR_PIPE_CONNECTED. 

		fConnected = ConnectNamedPipe(hPipe, NULL) ?
			TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

		if (fConnected)
		{
			hThread = CreateThread(
				NULL,              // no security attribute 
				0,                 // default stack size 
				InstanceThread,    // thread proc
				(LPVOID)params,    // thread parameter 
				0,                 // not suspended 
				&dwThreadId);      // returns thread ID 
		}
		else {
			std::cout << "Could not connect";
			// The client could not connect, so close the pipe. 
			CloseHandle(hPipe);
		}
		while (true) {

		}
		return 0;
	}
}

DWORD WINAPI InstanceThread(LPVOID lpvParam)
// This routine is a thread processing function to read from and reply to a client
// via the open pipe connection passed from the main loop. Note this allows
// the main loop to continue executing, potentially creating more threads of
// of this procedure to run concurrently, depending on the number of incoming
// client connections.
{
	char buf_req[100];

	DWORD cbBytesRead = 0, cbReplyBytes = 0, cbWritten = 0;
	BOOL fSuccess = FALSE;
	HANDLE hPipe = NULL;

	// Do some extra error checking since the app will keep running even if this
	// thread fails.

	//if (lpvParam == NULL)
	//{
	//	printf("\nERROR - Pipe Server Failure:\n");
	//	printf("   InstanceThread got an unexpected NULL value in lpvParam.\n");
	//	printf("   InstanceThread exitting.\n");
	//	if (pchReply != NULL) HeapFree(hHeap, 0, pchReply);
	//	if (pchRequest != NULL) HeapFree(hHeap, 0, pchRequest);
	//	return (DWORD)-1;
	//}

	//if (pchRequest == NULL)
	//{
	//	printf("\nERROR - Pipe Server Failure:\n");
	//	printf("   InstanceThread got an unexpected NULL heap allocation.\n");
	//	printf("   InstanceThread exitting.\n");
	//	if (pchReply != NULL) HeapFree(hHeap, 0, pchReply);
	//	return (DWORD)-1;
	//}

	//if (pchReply == NULL)
	//{
	//	printf("\nERROR - Pipe Server Failure:\n");
	//	printf("   InstanceThread got an unexpected NULL heap allocation.\n");
	//	printf("   InstanceThread exitting.\n");
	//	if (pchRequest != NULL) HeapFree(hHeap, 0, pchRequest);
	//	return (DWORD)-1;
	//}

	// Print verbose messages. In production code, this should be for debugging only.
	//printf("InstanceThread created, receiving and processing messages.\n");

	// The thread's parameter is a handle to a pipe object instance. 

	hPipe = ((ThreadParams*)lpvParam)->hpipe;
	std::string filename = ((ThreadParams*)lpvParam)->filename;
	int clientId = ((ThreadParams*)lpvParam)->clientId;
	std::ifstream fin;
	fin.open(filename);
	std::cout << "Running thread for serving client: " << clientId << "\n";
	std::vector<bool> isReading;
	std::vector<bool> isWriting;
	for (size_t i = 0; i < recordsNumber; i++)
	{
		isReading.push_back(false);
		isWriting.push_back(false);
	}

	std::ofstream fout;
	fout.open(filename, std::ofstream::binary | std::ofstream::app);

	// Loop until done reading
	while (1)
	{
		// Read client requests from the pipe. This simplistic code only allows messages
		// up to BUFSIZE characters in length.
		// std::cout << "\n reading from pipe " << hPipe << "\n";
		fSuccess = ReadFile(
			hPipe,        // handle to pipe 
			buf_req,    // buffer to receive data 
			100, // size of buffer 
			&cbBytesRead, // number of bytes read 
			NULL);        // not overlapped I/O 

		if (!fSuccess || cbBytesRead == 0)
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				std::cout << "InstanceThread: client disconnected.\n";
			}
			else
			{
				std::cout << "InstanceThread ReadFile failed, GLE=%d.\n";
			}
			break;
		}

		std::cout << "Request from client " << clientId << " is: \"" << buf_req << "\"\n";

		ProcessRequest(buf_req, hPipe, filename, clientId, &isReading, &isWriting);
	}

	// Flush the pipe to allow the client to read the pipe's contents 
	// before disconnecting. Then disconnect the pipe, and close the 
	// handle to this pipe instance. 

	FlushFileBuffers(hPipe);
	DisconnectNamedPipe(hPipe);
	CloseHandle(hPipe);

	std::cout << "End of work with client: " << clientId << "\n";
	return 1;
}

VOID ProcessRequest(char* request, HANDLE hPipe, std::string filename, int clientID, std::vector<bool>* isReading, std::vector<bool>* isWriting)
{
	char* output = new char[BUFSIZE] {0};
	std::vector<std::string> parsed_request = ParsedRequest(request);

	char* command = (char*)parsed_request[0].c_str();

	if (strncmp(command, get_to_modify, 4) == 0)
	{
		char* buf = new char[100] { 1 };
		int id = std::stoi(parsed_request[1]) - 1;
		if (id < 0 || id >= recordsNumber)
		{
			std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << server_internal_error << "\n";
			Respond500(hPipe);
			return;
		}
		if ((*isWriting)[id] == true)
		{
			Respond400(hPipe);
			return;
		}
		Employee e = Read_Block(id, id, filename, isReading);
		std::string command = std::string(data);
		std::string response = command
			.append(std::to_string(e.ID))
			.append(" ")
			.append(e.name)
			.append(" ")
			.append(std::to_string(e.hours))
			.append(" \0");

		std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << response << "\n";
		Send((char*)response.c_str(), hPipe);
	}
	else if (strncmp(command, modify, 4) == 0)
	{
		int id = std::stoi(parsed_request[1]) - 1;
		if (id < 0 || id >= recordsNumber)
		{
			std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << server_internal_error << "\n";
			Respond500(hPipe);
			return;
		}
		if ((*isReading)[id] == true)
		{
			Respond400(hPipe);
			return;
		}
		char* name = (char*)(parsed_request[2]).c_str();
		double hours = std::stod(parsed_request[3]);
		Employee e;
		e.ID = id + 1;
		strcpy(e.name, name);
		e.hours = hours;

		Write_Block(id, e, filename, isWriting);
		std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << ok << "\n";
		Send((char*)ok, hPipe);
	}
	else if (strncmp(command, exit_cycle, 4) == 0)
	{
		Send((char*)"Requested exit", hPipe);
	}
	else if (strncmp(command, release_read, 4) == 0)
	{
		char buf[200]{ 0 };
		int id = std::stoi(parsed_request[1]) - 1;
		if (id < 0 || id >= recordsNumber)
		{
			std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << server_internal_error << "\n";
			Respond500(hPipe);
			return;
		}
		Read_Release(id, isReading);
		std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << ok << "\n";
		Send((char*)ok, hPipe);
	}
	else if (strncmp(command, release_write, 4) == 0)
	{
		char buf[200]{ 0 };
		int id = std::stoi(parsed_request[1]) - 1;
		if (id < 0 || id >= recordsNumber)
		{
			std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << server_internal_error << "\n";
			Respond500(hPipe);
			return;
		}
		Write_Release(id, isWriting);
		std::cout << "For client " << clientID << " request: \"" << request << "\" sending response: " << ok << "\n";
		Send((char*)ok, hPipe);
	}
	else
	{
		Send((char*)"400_", hPipe);
	}
}

VOID Respond500(HANDLE hPipe) {
	Send((char*)"500_", hPipe);
}

VOID Respond400(HANDLE hPipe) {
	Send((char*)"400_", hPipe);
}

std::vector<std::string> ParsedRequest(char* request)
{
	std::string request_str = std::string(request);

	std::vector<std::string> words{};

	size_t pos = 0;
	while ((pos = request_str.find(" ")) != std::string::npos) {
		words.push_back(request_str.substr(0, pos));
		request_str.erase(0, pos + 1);
	}
	return words;
}

VOID Send(char* response, HANDLE hPipe)
{
	DWORD cbBytesWritten = 0;
	BOOL fSuccess = WriteFile(
		hPipe,        // handle to pipe 
		response,     // buffer to write from 
		strlen(response) + 1, // number of bytes to write 
		&cbBytesWritten,   // number of bytes written 
		NULL);        // not overlapped I/O 

	if (!fSuccess)
	{
		printf("InstanceThread WriteFile failed, GLE=%d.\n", GetLastError());
	}
}

VOID Receive(char* request, HANDLE hPipe)
{
	DWORD cbBytesRead = 0;
	BOOL fSuccess = ReadFile(
		hPipe,        // handle to pipe 
		request,    // buffer to receive data 
		strlen(request), // size of buffer 
		&cbBytesRead, // number of bytes read 
		NULL);        // not overlapped I/O 

	if (!fSuccess || cbBytesRead == 0)
	{
		if (GetLastError() == ERROR_BROKEN_PIPE)
		{
			std::cout << "InstanceThread: client disconnected.\n";
		}
		else
		{
			std::cout << "InstanceThread ReadFile failed, GLE=%d.\n";
		}
	}
}