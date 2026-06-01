#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "IWebSocket.h"
#include "ASRComponent.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnXunfeiStatusChanged, bool, bIsRecording);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnXunfeiTextRecognized, const FString&, RecognizedText);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class A_API UASRComponent : public UActorComponent
{
    GENERATED_BODY()

public:	
    UASRComponent();

    UPROPERTY(BlueprintAssignable, Category = "Voice")
    FOnXunfeiStatusChanged OnStatusChanged;

    UPROPERTY(BlueprintAssignable, Category = "Voice")
    FOnXunfeiTextRecognized OnQuestionRecognized;

    UFUNCTION(BlueprintCallable, Category = "Voice")
    void StartStreaming();

    UFUNCTION(BlueprintCallable, Category = "Voice")
    void StopStreaming();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 讯飞配置参数
    FString AppID;
    FString APIKey;
    FString APISecret;
    FString BaseURL = TEXT("ws://ws-api.xfyun.cn/v2/iat"); // 讯飞听写API

    // 状态与缓存
    bool bIsStreaming = false;
    int32 AudioStatus = 0; // 0:第一帧, 1:中间帧, 2:最后一帧
    FCriticalSection AudioMutex;
    TArray<int16> AccumulatedAudioBuffer;

    // 录音相关
    Audio::FAudioCapture AudioCapture;
    int32 CurrentSampleRate = 16000;
    
    // WebSocket
    TSharedPtr<IWebSocket> Socket;

    // 内部函数
    void LoadServerConfig();
    FString GenerateAuthURL();
    void ConnectToServer(const FString& ServerURL);
    void DisconnectFromServer();
    
    bool StartAudioCapture();
    void StopAudioCapture();
    void OnAudioCaptureCallback(const float* InAudio, int32 NumFrames, int32 NumChannels);
    void ProcessAndSendAudioData(const float* InAudio, int32 NumFrames, int32 NumChannels);
    
    void SendAudioChunk(const TArray<int16>& AudioData, int32 Status);
    void OnMessageReceived(const FString& InMessage);
    
    //用于缓存整场对话的完整文本
    FString FullyRecognizedText;
};