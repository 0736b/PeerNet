#pragma once

#include <stack>
#include <deque>

#define PN_MaxPacketSize 1472		//	Max size of an outgoing or incoming packet
#define RIO_ResultsPerThread 128	//	How many results to dequeue from the stack per thread
#define PN_MaxSendPackets 10240		//	Max outgoing packets per socket before you run out of memory
#define PN_MaxReceivePackets 10240	//	Max pending incoming packets before new packets are disgarded

//	Receive Completion Keys
enum COMPLETION_KEY_RECV
{
	CK_STOP_RECV = 0,	//	used to break a threads main loop
	CK_RIO_RECV = 1,	//	RIO Receive completions
};

//	Send Completion Keys
enum COMPLETION_KEY_SEND
{
	CK_STOP_SEND = 0,	//	Used to break a threads main loop
	CK_RIO_SEND = 1,	//	RIO Send completions
	CK_SEND = 2,		//	Used to initiate a send
};

//	Receive Data Buffer Struct
//	Holds a pointer to the senders address buffer
struct RIO_BUF_RECV : public RIO_BUF {
	PRIO_BUF pAddrBuff;	//	Address Buffer for this data buffer
};

//	Send Data Buffer Struct
//	Holds a pointer to the data buffer pool this send used
class ConcurrentDeque;
struct RIO_BUF_SEND : public RIO_BUF {
	//	Originating container for this buffer
	ConcurrentDeque* BufferContainer;
};

//	Single Consumer Multi Producer concurrent deque manager
class ConcurrentDeque {
	std::deque<RIO_BUF_SEND*> Buffers;
	std::mutex BufferMutex;
public:
	//	Push a buffer back into the container
	inline void Push(RIO_BUF_SEND* Buffer) {
		BufferMutex.lock();
		Buffers.push_back(Buffer);
		BufferMutex.unlock();
	}

	//	Pull a buffer removing it from the container
	inline RIO_BUF_SEND* Pull() {
		BufferMutex.lock();
		if (Buffers.empty()) { BufferMutex.unlock(); return nullptr; }
		RIO_BUF_SEND* Buffer = Buffers.front();
		Buffers.pop_front();
		BufferMutex.unlock();
		return Buffer;
	}

	//	Cleanup
	inline void Cleanup() {
		while (!Buffers.empty())
		{
			RIO_BUF_SEND* Buff = Buffers.front();
			delete Buff;
			Buffers.pop_front();
		}
	}
};

namespace PeerNet
{
	//
	//	NetSocket Class
	//
	class NetSocket
	{
		PeerNet* _PeerNet = nullptr;
		NetAddress* Address = nullptr;
		RIO_EXTENSION_FUNCTION_TABLE RIO;
		std::mutex RioMutex;
		SOCKET Socket;
		//	Receives
		RIO_CQ CompletionQueue_Receive;
		const unsigned char ThreadCount_Receive;
		const HANDLE IOCP_Receive;
		RIO_BUFFERID Address_BufferID_Receive;
		PCHAR Address_Buffer_Receive;
		RIO_BUFFERID Data_BufferID_Receive;
		PCHAR const Data_Buffer_Receive;
		std::deque<RIO_BUF_RECV*> Buffers_Receive;
		std::stack<thread> Threads_Receive;
		//	Sends
		RIO_CQ CompletionQueue_Send;
		const unsigned char ThreadCount_Send;
		const HANDLE IOCP_Send;
		RIO_BUFFERID Data_BufferID_Send;
		PCHAR const Data_Buffer_Send;
		std::stack<thread> Threads_Send;

		//	Request Queue
		RIO_RQ RequestQueue;
	public:

		//
		//	NetSocket Constructor
		//
		inline NetSocket(PeerNet* PNInstance, NetAddress* MyAddress) : _PeerNet(PNInstance), Address(MyAddress), RIO(_PeerNet->RIO()), RioMutex(),
			Socket(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED)),
			ThreadCount_Receive(thread::hardware_concurrency()), ThreadCount_Send(thread::hardware_concurrency()),
			IOCP_Receive(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ThreadCount_Receive)),
			IOCP_Send(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ThreadCount_Send)),
			Address_Buffer_Receive(new char[sizeof(SOCKADDR_INET)*PN_MaxReceivePackets]),
			Data_Buffer_Receive(new char[PN_MaxPacketSize*PN_MaxReceivePackets]),
			Data_Buffer_Send(new char[PN_MaxPacketSize*PN_MaxSendPackets]),
			Buffers_Receive()
		{
			//	Make sure our socket was created properly
			if (Socket == INVALID_SOCKET) { printf("Socket Failed(%i)\n", WSAGetLastError()); }
			

			//	Create Receive Completion Queue
			OVERLAPPED Overlap_Receive;
			RIO_NOTIFICATION_COMPLETION Completion_Receive;
			Completion_Receive.Type = RIO_IOCP_COMPLETION;
			Completion_Receive.Iocp.IocpHandle = IOCP_Receive;
			Completion_Receive.Iocp.CompletionKey = (void*)CK_RIO_RECV; //-V566
			Completion_Receive.Iocp.Overlapped = &Overlap_Receive;
			CompletionQueue_Receive = RIO.RIOCreateCompletionQueue(PN_MaxReceivePackets, &Completion_Receive);
			if (CompletionQueue_Receive == RIO_INVALID_CQ) { printf("Create Receive Completion Queue Failed: %i\n", WSAGetLastError()); }
			//	Create Send Completion Queue
			OVERLAPPED Overlap_Send;
			RIO_NOTIFICATION_COMPLETION Completion_Send;
			Completion_Send.Type = RIO_IOCP_COMPLETION;
			Completion_Send.Iocp.IocpHandle = IOCP_Send;
			Completion_Send.Iocp.CompletionKey = (void*)CK_RIO_SEND; //-V566
			Completion_Send.Iocp.Overlapped = &Overlap_Send;
			CompletionQueue_Send = RIO.RIOCreateCompletionQueue(PN_MaxSendPackets, &Completion_Send);
			if (CompletionQueue_Send == RIO_INVALID_CQ) { printf("Create Send Completion Queue Failed: %i\n", WSAGetLastError()); }

			//	Create Request Queue
			RequestQueue = RIO.RIOCreateRequestQueue(Socket, PN_MaxReceivePackets, 1, PN_MaxSendPackets, 1, CompletionQueue_Receive, CompletionQueue_Send, NULL);
			if (RequestQueue == RIO_INVALID_RQ) { printf("Request Queue Failed: %i\n", WSAGetLastError()); }

			//
			//	Initialize Receive side
			//

			//	Register Receive Address Memory Buffer
			Address_BufferID_Receive = RIO.RIORegisterBuffer(Address_Buffer_Receive, sizeof(SOCKADDR_INET)*PN_MaxReceivePackets);
			if (Address_BufferID_Receive == RIO_INVALID_BUFFERID) { printf("Receive Address_Buffer: Invalid BufferID\n"); }

			//	Register Receive Data Memory Buffer
			Data_BufferID_Receive = RIO.RIORegisterBuffer(Data_Buffer_Receive, PN_MaxPacketSize*PN_MaxReceivePackets);
			if (Data_BufferID_Receive == RIO_INVALID_BUFFERID) { printf("Receive Data_Buffer: Invalid BufferID\n"); }

			//	Fill our Receive buffers
			unsigned long AddressOffset = 0;
			unsigned long ReceiveOffset = 0;
			for (unsigned long i = 0; i < PN_MaxReceivePackets; i++)
			{
				RIO_BUF_RECV* pBuf = new RIO_BUF_RECV;
				pBuf->BufferId = Data_BufferID_Receive;
				pBuf->Offset = ReceiveOffset;
				pBuf->Length = PN_MaxPacketSize;
				//
				pBuf->pAddrBuff = new RIO_BUF;
				pBuf->pAddrBuff->BufferId = Address_BufferID_Receive;
				pBuf->pAddrBuff->Offset = AddressOffset;
				pBuf->pAddrBuff->Length = sizeof(SOCKADDR_INET);
				//	Save our Receive buffer so it can be cleaned up when the socket is destroyed
				Buffers_Receive.push_back(pBuf);
				//	Post a receive using this buffer
				if (!RIO.RIOReceiveEx(RequestQueue, pBuf, 1, NULL, pBuf->pAddrBuff, NULL, NULL, NULL, pBuf))
				{ printf("RIO Receive Failed %i\n", WSAGetLastError()); }
				//	Increment counters
				ReceiveOffset += PN_MaxPacketSize;
				AddressOffset += sizeof(SOCKADDR_INET);
			}
			//	Notify receives are ready
			if (RIO.RIONotify(CompletionQueue_Receive) != ERROR_SUCCESS) { printf("\tRIO Receive Notify Failed\n"); }

			//
			//
			//	Receive Threads
			//
			for (unsigned char i = 0; i < ThreadCount_Receive; i++) {
				Threads_Receive.emplace(thread([&]() {
					RIORESULT CompletionResults[RIO_ResultsPerThread];
					DWORD numberOfBytes = 0;	//	Unused
					ULONG_PTR completionKey = 0;
					LPOVERLAPPED pOverlapped = nullptr;
					//	ZStd
					char*const Uncompressed_Data = new char[PN_MaxPacketSize]; //-V819
					ZSTD_DCtx*const Decompression_Context = ZSTD_createDCtx();

					//	Lock our thread to its own core
					SetThreadAffinityMask(GetCurrentThread(), i);
					//	Set our scheduling priority
					SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

					//	Run this threads main loop
					while (true) {
						//	Grab the next available completion or block until one arrives
						GetQueuedCompletionStatus(IOCP_Receive, &numberOfBytes, &completionKey, &pOverlapped, INFINITE);
						// Break our main loop on CK_STOP
						if (completionKey == CK_STOP_RECV) { break; }
						//	Process our completion
						switch (completionKey)
						{
						case CK_RIO_RECV:
						{
							RioMutex.lock();
							const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Receive, CompletionResults, RIO_ResultsPerThread);
							RIO.RIONotify(CompletionQueue_Receive);
							RioMutex.unlock();

							//	Actually read the data from each received packet
							for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
							{
								//	Get the raw packet data into our buffer
								RIO_BUF_RECV* pBuffer = reinterpret_cast<RIO_BUF_RECV*>(CompletionResults[CurResult].RequestContext);

								const size_t DecompressResult = ZSTD_decompressDCtx(Decompression_Context,
									Uncompressed_Data, PN_MaxPacketSize, &Data_Buffer_Receive[(size_t)pBuffer->Offset],
									(size_t)CompletionResults[CurResult].BytesTransferred);

								//	Return if decompression fails
								//	TODO: Should be < 0; Will randomly crash at 0 though.
								if (DecompressResult < 1) {
									printf("Receive Packet - Decompression Failed!\n"); continue;
								}
								//	Grab
								//	"show" packet to peer for processing
								//	

								_PeerNet->TranslateData((SOCKADDR_INET*)&Address_Buffer_Receive[(size_t)pBuffer->pAddrBuff->Offset], std::string(Uncompressed_Data, DecompressResult));

								RioMutex.lock();
								//	Push another read request into the queue
								if (!RIO.RIOReceiveEx(RequestQueue, pBuffer, 1, NULL, pBuffer->pAddrBuff, NULL, NULL, 0, pBuffer)) { printf("RIO Receive2 Failed\n"); }
								RioMutex.unlock();
							}
						}
						break;

						default: printf("Receive Thread - Unknown Completion Key\n");
						}
					} // Close While Loop

					//	Cleanup ZStd
					ZSTD_freeDCtx(Decompression_Context);
					delete[] Uncompressed_Data;
				}));
			}

			//
			//	Initialize Send side
			//

			//	Register Data Memory Buffer
			Data_BufferID_Send = RIO.RIORegisterBuffer(Data_Buffer_Send, PN_MaxPacketSize*PN_MaxSendPackets);
			if (Data_BufferID_Send == RIO_INVALID_BUFFERID) { printf("Send Data_Buffer: Invalid BufferID\n"); }

			//	Fill our Send buffers

			//
			//
			//	Send Threads
			//
			ULONG SendOffset = 0;
			for (unsigned char i = 0; i < ThreadCount_Send; i++) {
				//	Fill Send buffers for this thread
				ConcurrentDeque* SendBuffers = new ConcurrentDeque;
				for (unsigned long b = 0; b < PN_MaxSendPackets / ceil(ThreadCount_Send); b++)
				{
					RIO_BUF_SEND* pBuf = new RIO_BUF_SEND;
					pBuf->BufferId = Data_BufferID_Send;
					pBuf->Offset = SendOffset;
					pBuf->Length = PN_MaxPacketSize;
					//	Save our BufferContainer so used buffers can be returned to it later
					pBuf->BufferContainer = SendBuffers;
					//	Save our Receive buffer so it can be cleaned up when this thread is destroyed
					SendBuffers->Push(pBuf);
					//	Increment counters
					SendOffset += PN_MaxPacketSize;
				}
				//	Create the thread
				Threads_Send.emplace(thread([&]() {
					//
					ConcurrentDeque* MyBuffers = SendBuffers;
					RIORESULT CompletionResults[RIO_ResultsPerThread];
					DWORD numberOfBytes = 0;	//	Unused
					ULONG_PTR completionKey = 0;
					LPOVERLAPPED pOverlapped = nullptr;
					//	ZStd
					ZSTD_CCtx*const Compression_Context = ZSTD_createCCtx();

					//	Lock our thread to its own core
					SetThreadAffinityMask(GetCurrentThread(), i);
					//	Set our scheduling priority
					SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

					//	Run this threads main loop
					while (true) {
						//	Grab the next available completion or block until one arrives
						GetQueuedCompletionStatus(IOCP_Send, &numberOfBytes, &completionKey, &pOverlapped, INFINITE);
						// Break our main loop on CK_STOP
						if (completionKey == CK_STOP_RECV) { break; }
						//	Process our completion
						switch (completionKey)
						{
						//
						//	Finish Sending Event
						case CK_RIO_SEND:
						{
							RioMutex.lock();
							const ULONG NumResults = RIO.RIODequeueCompletion(CompletionQueue_Send, CompletionResults, RIO_ResultsPerThread);
							RIO.RIONotify(CompletionQueue_Send);
							RioMutex.unlock();
							//	Actually read the data from each received packet
							for (ULONG CurResult = 0; CurResult < NumResults; CurResult++)
							{
								//	Get the raw packet data into our buffer
								RIO_BUF_SEND* pBuffer = reinterpret_cast<RIO_BUF_SEND*>(CompletionResults[CurResult].RequestContext);
								//	Send this pBuffer back to the correct Thread Environment
								pBuffer->BufferContainer->Push(pBuffer);
							}
						}
						break;

						//
						//	Start Sending Event
						case CK_SEND:
						{
							RIO_BUF_SEND*const pBuffer = MyBuffers->Pull();
							//	If we are out of buffers push the request back out for another thread to pick up
							if (pBuffer == nullptr) {
								if (PostQueuedCompletionStatus(IOCP_Send, NULL, CK_SEND, pOverlapped) == 0) {
									printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
								}
							break;
							}
							::PeerNet::SendPacket* OutPacket = static_cast<::PeerNet::SendPacket*>(pOverlapped);

							//	Compress our outgoing packets data payload into the rest of the data buffer
							pBuffer->Length = (ULONG)ZSTD_compressCCtx(Compression_Context,
								&Data_Buffer_Send[(size_t)pBuffer->Offset], PN_MaxPacketSize, OutPacket->GetData()->str().c_str(), OutPacket->GetData()->str().size(), 1);

							//	If compression was successful, actually transmit our packet
							if (pBuffer->Length > 0) {
								//printf("Compressed: %i->%i\n", SendPacket->GetData().size(), pBuffer->Length);

								RioMutex.lock();
								RIO.RIOSendEx(RequestQueue, pBuffer, 1, NULL, OutPacket->GetAddress(), NULL, NULL, NULL, pBuffer);
								RioMutex.unlock();
							}
							else { printf("Packet Compression Failed - %i\n", pBuffer->Length); }
							//	Mark packet as not sending
							OutPacket->IsSending.store(0);
							//	Mark managed SendPackets for cleanup
							if (OutPacket->GetManaged())
							{
								OutPacket->NeedsDelete.store(1);
							}
						}
						break;

						default: printf("Send Thread - Unknown Completion Key\n");
						}
					}
					//	Cleanup the send buffers for this thread
					MyBuffers->Cleanup();
					delete MyBuffers;
					//	Cleanup ZStd
					ZSTD_freeCCtx(Compression_Context);
				}));
			}

			//	Notify sends are ready
			if (RIO.RIONotify(CompletionQueue_Send) != ERROR_SUCCESS) { printf("\tRIO Send Notify Failed\n"); }

			//	Finally bind our socket so we can send/receive data
			if (bind(Socket, Address->AddrInfo()->ai_addr, (int)Address->AddrInfo()->ai_addrlen) == SOCKET_ERROR)
			{ printf("Bind Failed(%i)\n", WSAGetLastError()); }
			else
			{ printf("\tListening On - %s\n", Address->FormattedAddress()); }
		}

		//
		//	NetSocket Destructor
		//
		inline ~NetSocket()
		{
			//	Prohibit the Socket from conducting any more Sends or Receives
			shutdown(Socket, SD_BOTH);
			//	Post a CK_STOP for each created send/receive thread
			for (unsigned char i = 0; i < ThreadCount_Receive; i++) {
				if (PostQueuedCompletionStatus(IOCP_Receive, NULL, CK_STOP_RECV, NULL) == 0) {
					printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
				}
			}
			for (unsigned char i = 0; i < ThreadCount_Send; i++) {
				if (PostQueuedCompletionStatus(IOCP_Send, NULL, CK_STOP_SEND, NULL) == 0) {
					printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
				}
			}
			//	Wait for each send/receive thread to exit
			while (!Threads_Receive.empty()) { Threads_Receive.top().join(); Threads_Receive.pop(); }
			while (!Threads_Send.empty()) { Threads_Send.top().join(); Threads_Send.pop(); }
			//	Close each send/receive completion queue
			RIO.RIOCloseCompletionQueue(CompletionQueue_Receive);
			RIO.RIOCloseCompletionQueue(CompletionQueue_Send);
			//	Deregister the receive address buffer
			RIO.RIODeregisterBuffer(Address_BufferID_Receive);
			//	Cleanup the receive buffers
			while (!Buffers_Receive.empty())
			{
				RIO_BUF_RECV* Buff = Buffers_Receive.front();
				delete Buff->pAddrBuff;
				delete Buff;
				Buffers_Receive.pop_front();
			}
			delete[] Address_Buffer_Receive;
			//	Deregister each send/receive data buffer
			RIO.RIODeregisterBuffer(Data_BufferID_Receive);
			RIO.RIODeregisterBuffer(Data_BufferID_Send);
			delete[] Data_Buffer_Receive;
			delete[] Data_Buffer_Send;
			//	Close each send/receive IO Completion Port
			CloseHandle(IOCP_Receive);
			CloseHandle(IOCP_Send);
			//	Shutdown Socket
			closesocket(Socket);

			printf("\tShutdown Socket - %s\n", Address->FormattedAddress());
			//	Cleanup our NetAddress
			//	TODO: Need to return this address back into the Unused Address Pool instead of deleting it
			delete Address;
		}

		inline void SendPacket(SendPacket* Packet)
		{
			if (PostQueuedCompletionStatus(IOCP_Send, NULL, CK_SEND, Packet) == 0) {
				printf("PostQueuedCompletionStatus Error: %i\n", GetLastError());
			}
		}
	};
}