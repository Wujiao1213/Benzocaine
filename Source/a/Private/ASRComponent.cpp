#include "ASRComponent.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/DateTime.h"
#include "GenericPlatform/GenericPlatformHttp.h"
// ========================================================
// 👇 【核心修复】防止 OpenSSL 的 UI 与 UE 的 UI 命名空间冲突
// ========================================================
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/hmac.h>
#include <openssl/evp.h>
THIRD_PARTY_INCLUDES_END
#undef UI

UASRComponent::UASRComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UASRComponent::BeginPlay()
{
    Super::BeginPlay();
    LoadServerConfig();
}

void UASRComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopStreaming();
    DisconnectFromServer();
    Super::EndPlay(EndPlayReason);
}

void UASRComponent::LoadServerConfig()
{
    FString ConfigFilePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("ServerConfig"), TEXT("XunfeiConfig.ini"));
    FPaths::MakeStandardFilename(ConfigFilePath);

    // 读取讯飞三要素
    if (!GConfig->GetString(TEXT("Xunfei"), TEXT("AppID"), AppID, ConfigFilePath) ||
        !GConfig->GetString(TEXT("Xunfei"), TEXT("APIKey"), APIKey, ConfigFilePath) ||
        !GConfig->GetString(TEXT("Xunfei"), TEXT("APISecret"), APISecret, ConfigFilePath))
    {
        AppID = TEXT("YOUR_APP_ID");
        APIKey = TEXT("YOUR_API_KEY");
        APISecret = TEXT("YOUR_API_SECRET");
        
        GConfig->SetString(TEXT("Xunfei"), TEXT("AppID"), *AppID, ConfigFilePath);
        GConfig->SetString(TEXT("Xunfei"), TEXT("APIKey"), *APIKey, ConfigFilePath);
        GConfig->SetString(TEXT("Xunfei"), TEXT("APISecret"), *APISecret, ConfigFilePath);
        GConfig->Flush(false, ConfigFilePath);
        UE_LOG(LogTemp, Warning, TEXT("⚠️ 未找到讯飞配置文件，已生成默认模板: %s"), *ConfigFilePath);
    }
}

// ==========================================
// 讯飞鉴权 URL 生成 (注意：UE原生缺少HMAC-SHA256)
// ==========================================
FString UASRComponent::GenerateAuthURL()
{
    // 1. 获取当前 UTC 时间，并格式化为讯飞要求的 RFC1123 格式 (例如: Thu, 01 Aug 2019 01:53:21 GMT)
    FDateTime Now = FDateTime::UtcNow();
    FString DateStr = Now.ToHttpDate();

    // 2. 拼接 Signature 的原始字符串
    FString Host = TEXT("ws-api.xfyun.cn");
    FString SignatureOrigin = FString::Printf(TEXT("host: %s\ndate: %s\nGET /v2/iat HTTP/1.1"), *Host, *DateStr);

    // 3. 使用 APISecret 对原始字符串进行 HMAC-SHA256 加密
    FTCHARToUTF8 OriginUtf8(*SignatureOrigin);
    FTCHARToUTF8 SecretUtf8(*APISecret);

    unsigned char Hash[EVP_MAX_MD_SIZE];
    unsigned int ResultLen = 0;

    HMAC(EVP_sha256(), SecretUtf8.Get(), SecretUtf8.Length(),
         (unsigned char*)OriginUtf8.Get(), OriginUtf8.Length(),
         Hash, &ResultLen);

    // 4. 将加密后的二进制结果进行 Base64 编码
    FString SignatureBase64 = FBase64::Encode(Hash, ResultLen);

    // 5. 拼接 Authorization 字典字符串
    FString AuthOrigin = FString::Printf(TEXT("api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\""), 
        *APIKey, *SignatureBase64);

    // 6. 对 Authorization 字典再进行一次 Base64 编码
    FTCHARToUTF8 AuthUtf8(*AuthOrigin);
    FString AuthBase64 = FBase64::Encode((uint8*)AuthUtf8.Get(), AuthUtf8.Length());

    // 7. 对生成的参数进行 URL 编码（防止特殊字符破坏 URL 结构）
    FString EncodedAuth = FGenericPlatformHttp::UrlEncode(AuthBase64);
    FString EncodedDate = FGenericPlatformHttp::UrlEncode(DateStr);

    // 8. 拼接最终的 WebSocket URL
    FString FinalURL = FString::Printf(TEXT("%s?authorization=%s&date=%s&host=%s"),
        *BaseURL, *EncodedAuth, *EncodedDate, *Host);

    UE_LOG(LogTemp, Log, TEXT("🔑 讯飞鉴权 URL 生成成功!"));
    return FinalURL;
}

void UASRComponent::StartStreaming()
{
    if (bIsStreaming) return;

    FScopeLock Lock(&AudioMutex);
    AccumulatedAudioBuffer.Reset();
    FullyRecognizedText.Reset(); // 👈 新增：开始新录音时，清空上一次的旧文字
    AudioStatus = 0; // 重置为首帧状态
    
    FString AuthURL = GenerateAuthURL();
    ConnectToServer(AuthURL);

    if (StartAudioCapture())
    {
        bIsStreaming = true;
        if (OnStatusChanged.IsBound()) OnStatusChanged.Broadcast(true);
    }
    else
    {
        DisconnectFromServer();
    }
}

void UASRComponent::StopStreaming()
{
    if (!bIsStreaming) return;

    StopAudioCapture();
    
    // 发送最后一帧（Status = 2）
    if (Socket.IsValid() && Socket->IsConnected())
    {
        FScopeLock Lock(&AudioMutex);
        SendAudioChunk(AccumulatedAudioBuffer, 2); 
    }

    bIsStreaming = false;
    AccumulatedAudioBuffer.Reset();
    AudioStatus = 0;
    
    if (OnStatusChanged.IsBound()) OnStatusChanged.Broadcast(false);
}

void UASRComponent::ConnectToServer(const FString& ServerURL)
{
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModuleChecked("WebSockets");
    }

    if (Socket.IsValid())
    {
        Socket->Close();
        Socket.Reset(); 
    }
    
    Socket = FWebSocketsModule::Get().CreateWebSocket(ServerURL, TEXT("ws"));
    TWeakObjectPtr<UASRComponent> WeakThis(this);
    
    Socket->OnConnected().AddLambda([WeakThis]() { 
        AsyncTask(ENamedThreads::GameThread, [WeakThis]() {
            if (WeakThis.IsValid()) UE_LOG(LogTemp, Log, TEXT("✅ 讯飞 WebSocket 连接成功！")); 
        });
    });
    
    Socket->OnConnectionError().AddLambda([WeakThis](const FString& Error) {
        FString SafeError = Error;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, SafeError]() {
            if (WeakThis.IsValid()) UE_LOG(LogTemp, Error, TEXT("❌ 讯飞连接失败: %s"), *SafeError);
        });
    });

    Socket->OnClosed().AddLambda([WeakThis](int32 StatusCode, const FString& Reason, bool bWasClean) {
        FString SafeReason = Reason;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, StatusCode, SafeReason, bWasClean]() {
            if (WeakThis.IsValid()) UE_LOG(LogTemp, Warning, TEXT("ℹ️ 讯飞连接关闭: %s"), *SafeReason);
        });
    });

    Socket->OnMessage().AddLambda([WeakThis](const FString& Message) {
        FString SafeMessage = Message;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, SafeMessage]() {
            if (WeakThis.IsValid()) WeakThis->OnMessageReceived(SafeMessage);
        });
    });

    Socket->Connect();
}

void UASRComponent::DisconnectFromServer()
{
    if (Socket.IsValid() && Socket->IsConnected())
    {
        Socket->Close();
    }
}

bool UASRComponent::StartAudioCapture()
{
    Audio::FAudioCaptureDeviceParams Params;
    Params.DeviceIndex = 0; 

    // 使用兼容 UE 5.2 及以下版本的 OpenCaptureStream
    // 注意：Lambda 表达式的第一个参数直接变为了 const float*
    bool bSuccess = AudioCapture.OpenCaptureStream(
        Params, 
        [this](const float* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate, double StreamTime, bool bOverFlow)
        {
            this->CurrentSampleRate = InSampleRate;
            
            // 因为 InAudio 已经是 float*，不需要再做 static_cast 转换了，直接传给处理函数
            this->OnAudioCaptureCallback(InAudio, NumFrames, NumChannels);
        }, 
        1024 
    );
    
    if (bSuccess)
    {
        AudioCapture.StartStream();
        UE_LOG(LogTemp, Log, TEXT("🎙️ 麦克风启动成功"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ 麦克风启动失败"));
    }
    
    return bSuccess;
}

void UASRComponent::StopAudioCapture()
{
    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.StopStream();
        AudioCapture.CloseStream();
        UE_LOG(LogTemp, Log, TEXT("麦克风已关闭"));
    }
}

void UASRComponent::OnAudioCaptureCallback(const float* InAudio, int32 NumFrames, int32 NumChannels)
{
    ProcessAndSendAudioData(InAudio, NumFrames, NumChannels);
}

void UASRComponent::ProcessAndSendAudioData(const float* InAudio, int32 NumFrames, int32 NumChannels)
{
    if (!bIsStreaming || !Socket.IsValid() || !Socket->IsConnected()) return;

    FScopeLock Lock(&AudioMutex);

    // 下采样逻辑（16kHz 是讯飞的标准要求）
    int32 StepRatio = 3; 
    for (int32 i = 0; i < NumFrames; i += StepRatio)
    {
        float SumSample = 0.0f;
        int32 SamplesToAverage = FMath::Min(StepRatio, NumFrames - i);

        for (int32 j = 0; j < SamplesToAverage; ++j)
        {
            int32 SampleIndex = (i + j) * NumChannels;
            float Mixed = InAudio[SampleIndex];
            if (NumChannels >= 2 && (SampleIndex + 1) < NumFrames * NumChannels)
                Mixed = (Mixed + InAudio[SampleIndex + 1]) * 0.5f;
            SumSample += Mixed;
        }
        
        float AvgSample = SumSample / SamplesToAverage;
        float ClampedSample = FMath::Clamp(AvgSample * 2.0f, -1.0f, 1.0f); 
        AccumulatedAudioBuffer.Add(static_cast<int16>(ClampedSample * 32767.0f));
    }

    // 讯飞建议每隔 40ms 发送一次数据，16kHz 16bit 对应 1280 字节（640个int16）
    const int32 ChunkSize = 640; 
    
    while (AccumulatedAudioBuffer.Num() >= ChunkSize)
    {
        TArray<int16> ChunkData;
        ChunkData.Append(AccumulatedAudioBuffer.GetData(), ChunkSize);
        AccumulatedAudioBuffer.RemoveAt(0, ChunkSize);

        // 发送数据块
        SendAudioChunk(ChunkData, AudioStatus);

        // 发送首帧后，后续状态改为中间帧(1)
        if (AudioStatus == 0) AudioStatus = 1;
    }
}

// ==========================================
// 按照讯飞 JSON 格式发送音频
// ==========================================
void UASRComponent::SendAudioChunk(const TArray<int16>& AudioData, int32 Status)
{
    if (!Socket.IsValid() || !Socket->IsConnected()) return;

    // 1. 将二进制音频进行 Base64 编码
    FString AudioBase64 = FBase64::Encode((uint8*)AudioData.GetData(), AudioData.Num() * sizeof(int16));

    // 2. 构建 JSON Payload
    TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject());

    // 如果是首帧，必须带上业务参数
    if (Status == 0)
    {
        TSharedPtr<FJsonObject> CommonObj = MakeShareable(new FJsonObject());
        CommonObj->SetStringField(TEXT("app_id"), AppID);
        RootJson->SetObjectField(TEXT("common"), CommonObj);

        TSharedPtr<FJsonObject> BusinessObj = MakeShareable(new FJsonObject());
        BusinessObj->SetStringField(TEXT("domain"), TEXT("iat"));
        BusinessObj->SetStringField(TEXT("language"), TEXT("zh_cn"));
        BusinessObj->SetStringField(TEXT("accent"), TEXT("mandarin"));
        BusinessObj->SetNumberField(TEXT("vinfo"), 1); // 返回详细信息
        BusinessObj->SetNumberField(TEXT("vad_eos"), 2000); // 静音超时断开
        RootJson->SetObjectField(TEXT("business"), BusinessObj);
    }

    TSharedPtr<FJsonObject> DataObj = MakeShareable(new FJsonObject());
    DataObj->SetNumberField(TEXT("status"), Status);
    DataObj->SetStringField(TEXT("format"), TEXT("audio/L16;rate=16000")); // 16KHz，16bit
    DataObj->SetStringField(TEXT("encoding"), TEXT("raw"));
    DataObj->SetStringField(TEXT("audio"), AudioBase64); // 填入Base64

    RootJson->SetObjectField(TEXT("data"), DataObj);

    // 3. 序列化并发送
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(RootJson.ToSharedRef(), Writer);

    Socket->Send(JsonString);
}

// ==========================================
// 解析讯飞返回的流式文本
// ==========================================
void UASRComponent::OnMessageReceived(const FString& InMessage)
{
    TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(InMessage);
    TSharedPtr<FJsonObject> JsonObject;

    if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
    {
        int32 Code = JsonObject->GetIntegerField(TEXT("code"));
        if (Code != 0)
        {
            FString ErrorMsg = JsonObject->GetStringField(TEXT("message"));
            UE_LOG(LogTemp, Error, TEXT("❌ 讯飞识别报错! Code: %d, Message: %s"), Code, *ErrorMsg);
            return;
        }

        const TSharedPtr<FJsonObject>* DataObj;
        if (JsonObject->TryGetObjectField(TEXT("data"), DataObj))
        {
            const TSharedPtr<FJsonObject>* ResultObj;
            if ((*DataObj)->TryGetObjectField(TEXT("result"), ResultObj))
            {
                // 解析 ws 数组 -> cw 数组 -> w 字段 (具体的字词)
                const TArray<TSharedPtr<FJsonValue>>* WsArray;
                if ((*ResultObj)->TryGetArrayField(TEXT("ws"), WsArray))
                {
                    FString IncrementalText = TEXT("");
                    for (const TSharedPtr<FJsonValue>& WsItem : *WsArray)
                    {
                        const TArray<TSharedPtr<FJsonValue>>* CwArray;
                        if (WsItem->AsObject()->TryGetArrayField(TEXT("cw"), CwArray))
                        {
                            for (const TSharedPtr<FJsonValue>& CwItem : *CwArray)
                            {
                                IncrementalText += CwItem->AsObject()->GetStringField(TEXT("w"));
                            }
                        }
                    }

                    if (!IncrementalText.IsEmpty())
                    {
                        // 👇 核心修改：将新传来的字词碎片，累加到完整文本里
                        FullyRecognizedText += IncrementalText;

                        // 修改日志打印，方便观察
                        UE_LOG(LogTemp, Log, TEXT("💬 讯飞当前完整结果: %s"), *FullyRecognizedText);
    
                        if (OnQuestionRecognized.IsBound())
                        {
                            // 👇 核心修改：广播给蓝图的是【当前最完整的整句话】
                            OnQuestionRecognized.Broadcast(FullyRecognizedText);
                        }
                    }
                }
            }

            // 判断是否后端表示结束
            int32 RecvStatus = (*DataObj)->GetIntegerField(TEXT("status"));
            if (RecvStatus == 2)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ 讯飞当前会话识别结束"));
                DisconnectFromServer(); // 断开本次连接，下次说话再连
            }
        }
    }
}