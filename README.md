# PeerNet

Multi Threadded Winsock 2 Registered Input/Output (RIO) Peer to Peer (P2P) Networking using a Client-Server like Functional Programming model.

![alt tag](https://ci.appveyor.com/api/projects/status/ni2ttyxpcoiubt7d/branch/master?svg=true)

**Each client acts a server and each server can optionally act as a client. You choose who sends data to who.**

>>Data Serialization with Cereal - https://github.com/USCiLab/cereal
>>
>>Data Compression with LZ4 - https://github.com/Cyan4973/lz4

PeerNet is entirely UDP Packet based with mechanisms in place for sending and receiving reliable packets.

Round Trip Time (RTT) for a reliable packet is 200μs (Microseconds) and 100μs for unreliable packets.
