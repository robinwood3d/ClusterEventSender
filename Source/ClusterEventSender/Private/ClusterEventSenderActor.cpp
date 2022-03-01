// Fill out your copyright notice in the Description page of Project Settings.


#include "ClusterEventSenderActor.h"

#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "SocketSubsystem.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

// Sets default values
AClusterEventSenderActor::AClusterEventSenderActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

}

// Called when the game starts or when spawned
void AClusterEventSenderActor::BeginPlay()
{
	Super::BeginPlay();

	Socket = CreateSocket("Sender");

	DataBuffer.AddUninitialized(PacketBufferSize);
	
}

FSocket* AClusterEventSenderActor::CreateSocket(const FString& InName)
{
	ConnectionName = InName;
	FSocket* NewSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, InName, false);
	return NewSocket;
}

bool AClusterEventSenderActor::Connect(const FString& InAddr, const int32 InPort, const int32 TriesAmount, const float TryDelay)
{
	// Generate IPv4 address
	FIPv4Address IPAddr;
	if (!FIPv4Address::Parse(InAddr, IPAddr))
	{
		UE_LOG(LogTemp, Error, TEXT("%s couldn't parse the address: %s"), *GetConnectionName(), *InAddr);
		return false;
	}

	// Generate internet address
	TSharedRef<FInternetAddr> InternetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	InternetAddr->SetIp(IPAddr.Value);
	InternetAddr->SetPort(InPort);

	// Start connection loop
	int32 TryIdx = 0;
	while (GetSocket()->Connect(*InternetAddr) == false)
	{
		UE_LOG(LogTemp, Log, TEXT("%s couldn't connect to the server %s [%d]"), *GetConnectionName(), *(InternetAddr->ToString(true)), TryIdx);
		if (TriesAmount > 0 && ++TryIdx >= TriesAmount)
		{
			UE_LOG(LogTemp, Error, TEXT("%s connection attempts limit reached"), *GetConnectionName());
			return false;
		}

		// Sleep some time before next try
		FPlatformProcess::Sleep(TryDelay / 1000.f);
	}

	return IsOpen();
}

void AClusterEventSenderActor::Disconnect()
{
	UE_LOG(LogTemp, Log, TEXT("%s disconnecting..."), *GetConnectionName());

	if (IsOpen())
	{
		GetSocket()->Close();
	}
}

// Called every frame
void AClusterEventSenderActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AClusterEventSenderActor::SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	Connect(Address, Port, 1, 0.f);

	TSharedPtr<FJsonObject> JsonObject = FJsonObjectConverter::UStructToJsonObject(Event);
	if (!JsonObject)
	{
		UE_LOG(LogTemp, Error, TEXT("Couldn't convert json cluster event data to net packet"));
		return;
	}

	bool bResult = SendPacket(JsonObject);
	if (!bResult)
	{
		UE_LOG(LogTemp, Error, TEXT("Couldn't send json cluster event"));
	}

}

bool AClusterEventSenderActor::SendPacket(TSharedPtr<FJsonObject> JsonPacket)
{
	if (!IsOpen())
	{
		UE_LOG(LogTemp, Error, TEXT("%s not connected"), *GetConnectionName());
		return false;
	}

	UE_LOG(LogTemp, Verbose, TEXT("%s - sending json..."), *GetConnectionName());

	// We'll be working with internal buffer to save some time on memcpy operation
	TArray<uint8>& Buffer = GetPersistentBuffer();
	Buffer.Reset();

	// Serialize data to json text
	FString OutputString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(JsonPacket.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s - Couldn't serialize json data"), *GetConnectionName());
		return false;
	}

	uint32 WriteOffset = 0;

	// Fil packet header with data
	FPacketHeader PacketHeader;
	PacketHeader.PacketBodyLength = OutputString.Len();
	UE_LOG(LogTemp, Verbose, TEXT("%s - Outgoing packet header: %s"), *GetConnectionName(), *PacketHeader.ToString());

	// Write packet header
	FMemory::Memcpy(Buffer.GetData() + WriteOffset, &PacketHeader, sizeof(FPacketHeader));
	WriteOffset += sizeof(FPacketHeader);

	// Write packet body
	FMemory::Memcpy(Buffer.GetData() + WriteOffset, TCHAR_TO_ANSI(*OutputString), PacketHeader.PacketBodyLength);
	WriteOffset += PacketHeader.PacketBodyLength;

	// Send packet
	if (!SendChunk(Buffer, WriteOffset, FString("send-json")))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s - Couldn't send json"), *GetConnectionName());
		return false;
	}

	UE_LOG(LogTemp, Verbose, TEXT("%s - Json sent"), *GetConnectionName());
	return true;
}

bool AClusterEventSenderActor::SendChunk(const TArray<uint8>& ChunkBuffer, const uint32 ChunkSize, const FString& ChunkName)
{
	uint32 BytesSentTotal = 0;
	int32  BytesSentNow = 0;
	uint32 BytesSendLeft = 0;

	// Write all bytes
	while (BytesSentTotal < ChunkSize)
	{
		BytesSendLeft = ChunkSize - BytesSentTotal;

		// Send data
		if (!Socket->Send(ChunkBuffer.GetData() + BytesSentTotal, BytesSendLeft, BytesSentNow))
		{
			UE_LOG(LogTemp, Error, TEXT("%s - %s send failed (length=%d)"), *Socket->GetDescription(), *ChunkName, ChunkSize);
			return false;
		}

		// Check amount of bytes sent
		if (BytesSentNow <= 0 || static_cast<uint32>(BytesSentNow) > BytesSendLeft)
		{
			UE_LOG(LogTemp, Error, TEXT("%s - %s send failed: %d of %u left"), *Socket->GetDescription(), *ChunkName, BytesSentNow, BytesSendLeft);
			return false;
		}

		BytesSentTotal += BytesSentNow;
		UE_LOG(LogTemp, VeryVerbose, TEXT("%s - %s sent %d bytes, %u bytes left"), *Socket->GetDescription(), *ChunkName, BytesSentNow, ChunkSize - BytesSentTotal);
	}

	UE_LOG(LogTemp, Verbose, TEXT("%s - %s was sent"), *Socket->GetDescription(), *ChunkName);

	return true;
}
