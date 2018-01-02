#include "PeerNet.hpp"

//	User-Defined Peer Class - Must inherit from NetPeer
//	Allows the end-user to seamlessly integrate PeerNet into their application
//	by providing their own derived NetPeer(client) class
class MyPeer : public PeerNet::NetPeer {
	//	This function is called whenever this peer receives a packet
	inline void Receive(PeerNet::ReceivePacket* Packet) {
		printf("Received Packet ID: %i\n", Packet->GetPacketID());
		printf("\t%s\n", Packet->ReadData<std::string>().c_str());
	}
	//	This function is called each time our peer ticks
	inline void Tick() {}
public:
	inline MyPeer(PeerNet::PeerNet* PNInstance, PeerNet::NetSocket*const DefaultSocket, PeerNet::NetAddress*const NetAddr)
		: PeerNet::NetPeer(PNInstance, DefaultSocket, NetAddr) {
		NewInterval(std::chrono::milliseconds(1000 / 60).count());	// 60 Ticks every 1 second
	}
};

//	Factory Class for the User-Defined Peer Class
//	Allows the end-user to seamlessly integrate PeerNet into their application
//	by providing their own pre-initialization logic for their derived NetPeer Class
class MyPeerFactory : public PeerNet::NetPeerFactory {
public:
	//	This function is called whenever a new peer is created
	inline PeerNet::NetPeer* Create(PeerNet::PeerNet* PNInstance, PeerNet::NetSocket*const DefaultSocket, PeerNet::NetAddress*const NetAddr) {
		return new MyPeer(PNInstance, DefaultSocket, NetAddr);
	}
};

//	OperationIDs can be duplicated across different channels
//	However they must be unique within the same channel
enum OperationID {
	Unreliable1 = 0,
	Reliable1 = 0,
	Ordered1 = 0
};

int main() {
	//	Create our NetPeer Factory
	MyPeerFactory* Factory = new MyPeerFactory();

	//	Initialize PeerNet, 10240 SendPackets, 10240 ReceivePackets, 16 maximum NetSockets
	PeerNet::PeerNet *_PeerNet = new PeerNet::PeerNet(Factory, 10240, 16);

	//	Open a socket at 127.0.0.1:9999
	PeerNet::NetSocket* Socket = _PeerNet->OpenSocket("127.0.0.1", "9999");

	//	Use this socket as our default send socket
	_PeerNet->SetDefaultSocket(Socket);

	//	Connect to our socket, represented as a NetPeer
	PeerNet::NetPeer* Peer = _PeerNet->GetPeer("127.0.0.1", "9999");

	//	Send some Unreliable Packets
	for (int i = 0; i < 4; i++) {
		auto NewPacket = Peer->CreateUnreliablePacket(OperationID::Unreliable1);
		NewPacket->WriteData<std::string>("I'm about to be serialized and I'm unreliable!!");
		Peer->Send_Packet(NewPacket);
		i++;
	}

	//	Send some Reliable Packets
	for (int i = 0; i < 4; i++) {
		auto NewPacket = Peer->CreateReliablePacket(OperationID::Reliable1);
		NewPacket->WriteData<std::string>("I'm about to be serialized and I'm reliable!!");
		Peer->Send_Packet(NewPacket);
		i++;
	}

	//	Send some Ordered Packets
	for (int i = 0; i < 4; i++) {
		auto NewPacket = Peer->CreateOrderedPacket(OperationID::Ordered1);
		NewPacket->WriteData<std::string>("I'm about to be serialized and I'm ordered!!");
		Peer->Send_Packet(NewPacket);
	}

	//	Wait 10 seconds
	std::this_thread::sleep_for(std::chrono::seconds(10));

	//	Print out our Round-Trip-Time
	printf("\n\tRound-Trip-Time:\t%.3fms\n\n", Peer->RTT_KOL().count());

	//	Shutdown PeerNet
	delete _PeerNet;
	delete Factory;

	std::system("PAUSE");
	return 0;
}