// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "Components/ActorComponent.h"
#include "XunfeiTTSComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTTSAudioReceived, const TArray<uint8>&, AudioData, bool, bIsFirstChunk, bool, bIsLastChunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTTSError, const FString&, ErrorMessage);

UCLASS( ClassGroup=(AI), meta=(BlueprintSpawnableComponent) )
class A_API UXunfeiTTSComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UXunfeiTTSComponent();
	
	// 发起合成请求
	UFUNCTION(BlueprintCallable, Category = "AI|TTS")
	void RequestTTS(const FString& TextToSpeak);

	UPROPERTY(BlueprintAssignable, Category = "AI|TTS")
	FOnTTSAudioReceived OnAudioReceived;

	UPROPERTY(BlueprintAssignable, Category = "AI|TTS")
	FOnTTSError OnError;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
public:	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	// ==== 讯飞控制台获取的秘钥信息 ====
	FString APPID = TEXT("31b72264");
	FString APISecret = TEXT("OTNhZWQ4NTNkOWU3YTk0NDc2MTU0NTFl");
	FString APIKey = TEXT("390162f80c420b8751e0267b6befb45d");
	FString VoiceName = TEXT("aisjiuxu"); 
	
	// 【核心修改 2】：新增状态控制变量
	bool bIsFirstChunk = true;     // 标记是否为第一块数据
	
	// ==== 内部逻辑 ====
	FString GenerateAuthUrl();
	TArray<uint8> CalculateHMACSHA256(const FString& Key, const FString& Data);
    
	TSharedPtr<IWebSocket> Socket;
	TArray<uint8> AccumulatedAudioData; // 用于拼接流式返回的音频块
	FString CurrentText; // 缓存当前要说的文本
};
