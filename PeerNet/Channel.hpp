#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>

using std::atomic;
using std::mutex;
using std::string;
using std::shared_ptr;
using std::make_shared;
using std::unordered_map;
using std::chrono::duration;
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

namespace PeerNet
{
	//	Base Class Channel	-	Foundation for the other Channel types
	class Channel
	{
	protected:
		//	Main Variables
		const NetAddress*const MyAddress;
		const PacketType ChannelID;

		//	Outgoing Variables
		mutex Out_Mutex;			//	Synchronize this channels Outgoing vars and funcs
		atomic<unsigned long> Out_NextID;	//	Next packet ID we'll use
		//	Since we use shared pointers to manage memory cleanup for our packets
		//	Unreliable packets need to be held onto long enough to actually get sent
		unordered_map<unsigned long, const shared_ptr<NetPacket>> Out_Packets;
		atomic<size_t> Out_CurAmount;	//	Current amount of unacknowledged packets
		atomic<unsigned long> Out_LastACK;	//	Most recent acknowledged ID

		//	Incoming Variables
		mutex In_Mutex;				//	Synchronize this channels Incoming vars and funcs
		atomic<unsigned long> In_LastID;	//	The largest received ID so far
	public:
		//	Constructor initializes our base class
		Channel(const NetAddress*const Address, const PacketType &ChanID)
			: MyAddress(Address), ChannelID(ChanID), Out_Mutex(), Out_NextID(1), Out_Packets(), Out_CurAmount(0), Out_LastACK(0), In_Mutex(), In_LastID(0) {}
		//
		inline const auto GetChannelID() const { return ChannelID; }
		//	Initialize and return a new packet for sending
		inline shared_ptr<SendPacket> NewPacket()
		{
			shared_ptr<SendPacket> Packet = std::make_shared<SendPacket>(Out_NextID.load(), GetChannelID(), MyAddress);
#ifdef _PERF_SPINLOCK
			while (!Out_Mutex.try_lock()) {}
#else
			Out_Mutex.lock();
#endif
			Out_Packets.emplace(Out_NextID++, Packet);
			Out_CurAmount.store(Out_Packets.size());
			Out_Mutex.unlock();
			return Packet;
		}

		//	Receives a packet
		inline virtual const bool Receive(ReceivePacket*const IN_Packet) = 0;
		//	Gets the current amount of unacknowledged packets
		inline const auto GetUnacknowledgedCount() { return Out_CurAmount.load(); }
		//	Get the largest received ID so far
		inline const auto GetLastID() const { return In_LastID.load(); }
		//	Acknowledge delivery and processing of all packets up to this ID
		inline void ACK(const unsigned long& ID)
		{
			//	We hold onto all the sent packets with an ID higher than that of
			//	Which our remote peer has not confirmed delivery for as those
			//	Packets may still be going through their initial sending process
			if (ID > Out_LastACK)
			{
				Out_LastACK = ID;
#ifdef _PERF_SPINLOCK
				while (!Out_Mutex.try_lock()) {}
#else
				Out_Mutex.lock();
#endif
				auto Out_Itr = Out_Packets.begin();
				while (Out_Itr != Out_Packets.end()) {
					if (Out_Itr->first <= Out_LastACK.load()) {
						if (Out_Itr->second->IsSending.load() == false)
						{
							Out_Packets.erase(Out_Itr++);
						}
					}
					else {
						++Out_Itr;
					}
				}
				Out_CurAmount.store(Out_Packets.size());
				Out_Mutex.unlock();
			}
			//	If their last received reliable ID is less than our last sent reliable id
			//	Send the most recently sent reliable packet to them again
			//	Note: if this packet is still in-transit it will be sent again
			//	ToDo:	Hold off on resending the packet until it's creation time
			//			is greater than this clients RTT
			//if (ID < Out_NextID - 1 && Out_Packets.count(Out_NextID - 1)) { MyPeer->Socket->PostCompletion<NetPacket*>(CK_SEND, Out_Packets[Out_NextID - 1].get()); }
		}

		/*virtual const string CompressPacket(NetPacket* OUT_Packet) = 0;
		virtual NetPacket* DecompressPacket(string IN_Data) = 0;*/
	};
}
