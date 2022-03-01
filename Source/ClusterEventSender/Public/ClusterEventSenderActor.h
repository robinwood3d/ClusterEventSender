// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sockets.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "ClusterEventSenderActor.generated.h"

struct FPacketHeader
{
	uint32 PacketBodyLength;

	FString ToString()
	{
		return FString::Printf(TEXT("<length=%u>"), PacketBodyLength);
	}
};

UCLASS()
class CLUSTEREVENTSENDER_API AClusterEventSenderActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AClusterEventSenderActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	FSocket* CreateSocket(const FString& InName);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	bool Connect(const FString& InAddr, const int32 InPort, const int32 TriesAmount, const float TryDelay);

	void Disconnect();

	UFUNCTION(BlueprintCallable)
	void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly);

	bool SendPacket(TSharedPtr<FJsonObject> JsonPacket);

	// Send specified amount of bytes from specified buffer
	bool SendChunk(const TArray<uint8>& ChunkBuffer, const uint32 ChunkSize, const FString& ChunkName = FString("WriteDataChunk"));

	inline FSocket* GetSocket()
	{
		return Socket;
	}

	// Access to the connection name
	inline const FString& GetConnectionName() const
	{
		return ConnectionName;
	}

	// Access to the internal read/write buffer
	inline TArray<uint8>& GetPersistentBuffer()
	{
		return DataBuffer;
	}

	inline bool IsOpen() const
	{
		return (Socket && (Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected));
	}

private:
	// Socket
	FSocket* Socket = nullptr;

	// Connection name (basically for nice logging)
	FString ConnectionName;

	// Data buffer
	TArray<uint8> DataBuffer;

	static constexpr int32  PacketBufferSize = 4 * 1024 * 1024; // bytes
};
