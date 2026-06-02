// Fill out your copyright notice in the Description page of Project Settings.


#include "XunfeiTTSComponent.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

namespace MinimalSHA256
{
    inline uint32 RightRot(uint32 value, uint32 count) {
        return (value >> count) | (value << (32 - count));
    }

    void Transform(uint32* state, const uint8* data) {
        uint32 a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
        const uint32 k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        for (i = 0, j = 0; i < 16; ++i, j += 4)
            m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
        for ( ; i < 64; ++i)
            m[i] = m[i - 16] + (RightRot(m[i - 15], 7) ^ RightRot(m[i - 15], 18) ^ (m[i - 15] >> 3)) + m[i - 7] + (RightRot(m[i - 2], 17) ^ RightRot(m[i - 2], 19) ^ (m[i - 2] >> 10));

        a = state[0]; b = state[1]; c = state[2]; d = state[3];
        e = state[4]; f = state[5]; g = state[6]; h = state[7];

        for (i = 0; i < 64; ++i) {
            t1 = h + (RightRot(e, 6) ^ RightRot(e, 11) ^ RightRot(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + m[i];
            t2 = (RightRot(a, 2) ^ RightRot(a, 13) ^ RightRot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    class FSHA256 {
        uint8 data[64];
        uint32 datalen;
        uint64 bitlen;
        uint32 state[8];
    public:
        FSHA256() {
            datalen = 0; bitlen = 0;
            state[0] = 0x6a09e667; state[1] = 0xbb67ae85; state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
            state[4] = 0x510e527f; state[5] = 0x9b05688c; state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
        }
        void Update(const uint8* data_in, uint32 len) {
            for (uint32 i = 0; i < len; ++i) {
                data[datalen] = data_in[i]; datalen++;
                if (datalen == 64) {
                    Transform(state, data);
                    bitlen += 512; datalen = 0;
                }
            }
        }
        void Final() {
            uint32 i = datalen;
            if (datalen < 56) {
                data[i++] = 0x80;
                while (i < 56) data[i++] = 0x00;
            } else {
                data[i++] = 0x80;
                while (i < 64) data[i++] = 0x00;
                Transform(state, data);
                for(uint32 k=0; k<56; ++k) data[k] = 0;
            }
            bitlen += datalen * 8;
            data[63] = bitlen; data[62] = bitlen >> 8; data[61] = bitlen >> 16; data[60] = bitlen >> 24;
            data[59] = bitlen >> 32; data[58] = bitlen >> 40; data[57] = bitlen >> 48; data[56] = bitlen >> 56;
            Transform(state, data);
        }
        void GetHash(uint8* hash) {
            for (uint32 i = 0; i < 4; ++i) {
                hash[i]      = (state[0] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 4]  = (state[1] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 8]  = (state[2] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 12] = (state[3] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 16] = (state[4] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 20] = (state[5] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 24] = (state[6] >> (24 - i * 8)) & 0x000000ff;
                hash[i + 28] = (state[7] >> (24 - i * 8)) & 0x000000ff;
            }
        }
    };
}
// =========================================================================

UXunfeiTTSComponent::UXunfeiTTSComponent()
{
    //关闭Tick事件
    PrimaryComponentTick.bCanEverTick = false;
}

void UXunfeiTTSComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UXunfeiTTSComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Socket.IsValid() && Socket->IsConnected())
    {
        Socket->Close();
    }
    // 彻底释放底层指针，防止变成阻碍下次运行的僵尸对象
    Socket.Reset();
    Super::EndPlay(EndPlayReason);
}

void UXunfeiTTSComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UXunfeiTTSComponent::RequestTTS(const FString& TextToSpeak)
{
    if (TextToSpeak.IsEmpty()) return;
    
    CurrentText = TextToSpeak;
    AccumulatedAudioData.Reset(); 
    bIsFirstChunk = true;

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModuleChecked("WebSockets");
    }

    if (Socket.IsValid() && Socket->IsConnected())
    {
        Socket->Close();
        Socket.Reset();
    }

    FString AuthUrl = GenerateAuthUrl();
    Socket = FWebSocketsModule::Get().CreateWebSocket(AuthUrl, TEXT("ws"));

    TWeakObjectPtr<UXunfeiTTSComponent> WeakThis(this);

    Socket->OnConnected().AddLambda([WeakThis]() {
        UE_LOG(LogTemp, Log, TEXT("🌐 [讯飞TTS] WebSocket 鉴权连接成功！"));
        if (UXunfeiTTSComponent* StrongThis = WeakThis.Get())
        {
            TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject());
            TSharedPtr<FJsonObject> CommonJson = MakeShareable(new FJsonObject());
            CommonJson->SetStringField(TEXT("app_id"), StrongThis->APPID);
            RootJson->SetObjectField(TEXT("common"), CommonJson);

            TSharedPtr<FJsonObject> BusinessJson = MakeShareable(new FJsonObject());
            BusinessJson->SetStringField(TEXT("aue"), TEXT("raw"));
            BusinessJson->SetStringField(TEXT("vcn"), StrongThis->VoiceName);
            BusinessJson->SetStringField(TEXT("tte"), TEXT("UTF8"));
            RootJson->SetObjectField(TEXT("business"), BusinessJson);

            TSharedPtr<FJsonObject> DataJson = MakeShareable(new FJsonObject());
            DataJson->SetNumberField(TEXT("status"), 2); 
            
            FTCHARToUTF8 Utf8Text(*(StrongThis->CurrentText));
            TArray<uint8> TextBytes;
            TextBytes.Append((uint8*)Utf8Text.Get(), Utf8Text.Length());
            DataJson->SetStringField(TEXT("text"), FBase64::Encode(TextBytes));
            
            RootJson->SetObjectField(TEXT("data"), DataJson);

            FString Payload;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
            FJsonSerializer::Serialize(RootJson.ToSharedRef(), Writer);
            
            StrongThis->Socket->Send(Payload);
        }
    });

    Socket->OnMessage().AddLambda([WeakThis](const FString& Message) {
        FString SafeMessage = Message;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, SafeMessage]() {
            if (UXunfeiTTSComponent* StrongThis = WeakThis.Get())
            {
                TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(SafeMessage);
                TSharedPtr<FJsonObject> JsonObject;

                if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
                {
                    int32 Code = JsonObject->GetNumberField(TEXT("code"));
                    
                    // ==========================================
                    // 🚨 【核心拦截 1】如果返回码不是 0，说明讯飞明确拒绝了服务！
                    // ==========================================
                    if (Code != 0)
                    {
                        FString ErrorMsg = JsonObject->GetStringField(TEXT("message"));
                        UE_LOG(LogTemp, Error, TEXT("🚨 [讯飞TTS] 业务错误！错误码: %d, 原因: %s"), Code, *ErrorMsg);
                        
                        if (StrongThis->OnError.IsBound()) StrongThis->OnError.Broadcast(ErrorMsg);
                        StrongThis->Socket->Close();
                        return;
                    }

                    TSharedPtr<FJsonObject> DataObj = JsonObject->GetObjectField(TEXT("data"));
                    
                    // 防御性检查：确保真的有 data 字段
                    if (!DataObj.IsValid())
                    {
                        UE_LOG(LogTemp, Error, TEXT("🚨 [讯飞TTS] 接收成功但缺失 data 字段。原始报文: %s"), *SafeMessage);
                        return;
                    }

                    FString AudioBase64 = DataObj->GetStringField(TEXT("audio"));
                    
                    TArray<uint8> AudioChunk;
                    FBase64::Decode(AudioBase64, AudioChunk);
                    StrongThis->AccumulatedAudioData.Append(AudioChunk); 

                    int32 Status = DataObj->GetNumberField(TEXT("status"));
                    bool bFirst = StrongThis->bIsFirstChunk;
                    bool bLast = (Status==2);
                    
                    StrongThis->bIsFirstChunk = false;

                    if (StrongThis->OnAudioReceived.IsBound())
                    {
                        StrongThis->OnAudioReceived.Broadcast(AudioChunk, bFirst, bLast);
                    }
                    if (bLast)
                    {
                        UE_LOG(LogTemp, Log, TEXT("✅ [讯飞TTS] 流式接收完毕！总大小: %d bytes"), StrongThis->AccumulatedAudioData.Num());
                        StrongThis->Socket->Close();
                    }
                }
                else
                {
                    // ==========================================
                    // 🚨 【核心拦截 2】解析 JSON 彻底失败！可能是收到了 HTML 报错页面等
                    // ==========================================
                    UE_LOG(LogTemp, Error, TEXT("🚨 [讯飞TTS] 收到无法解析的数据，原始报文: %s"), *SafeMessage);
                }
            }
        });
    });

    Socket->OnConnectionError().AddLambda([WeakThis](const FString& Error) {
        FString SafeError = Error;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, SafeError]() {
            UE_LOG(LogTemp, Error, TEXT("❌ [讯飞TTS] WebSocket 连接失败: %s"), *SafeError);
        });
    });

    // ==========================================
    // 🚨 【核心拦截 3】监听底层强制掐断！
    // ==========================================
    Socket->OnClosed().AddLambda([WeakThis](int32 StatusCode, const FString& Reason, bool bWasClean) {
        FString SafeReason = Reason;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, StatusCode, SafeReason, bWasClean]() {
            if (!bWasClean)
            {
                UE_LOG(LogTemp, Error, TEXT("🛑 [讯飞TTS] WebSocket 被异常掐断！状态码: %d, 原因: %s"), 
                    StatusCode, 
                    SafeReason.IsEmpty() ? TEXT("未知断线原因") : *SafeReason);
            }
        });
    });

    Socket->Connect();
}

FString UXunfeiTTSComponent::GenerateAuthUrl()
{
    FString DateStr = FDateTime::UtcNow().ToHttpDate();
    FString SignatureOrigin = FString::Printf(TEXT("host: tts-api.xfyun.cn\ndate: %s\nGET /v2/tts HTTP/1.1"), *DateStr);

    TArray<uint8> HmacBytes = CalculateHMACSHA256(APISecret, SignatureOrigin);
    FString SignatureBase64 = FBase64::Encode(HmacBytes);

    FString AuthString = FString::Printf(TEXT("api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\""), *APIKey, *SignatureBase64);
    FString AuthBase64 = FBase64::Encode(AuthString);

    FString EncodedDate = DateStr.Replace(TEXT(" "), TEXT("%20")).Replace(TEXT(","), TEXT("%2C")).Replace(TEXT(":"), TEXT("%3A"));

    return FString::Printf(TEXT("wss://tts-api.xfyun.cn/v2/tts?authorization=%s&date=%s&host=tts-api.xfyun.cn"), *AuthBase64, *EncodedDate);
}

TArray<uint8> UXunfeiTTSComponent::CalculateHMACSHA256(const FString& Key, const FString& Data)
{
    TArray<uint8> KeyBytes, DataBytes;
    FTCHARToUTF8 Utf8Key(*Key);
    KeyBytes.Append((uint8*)Utf8Key.Get(), Utf8Key.Length());
    FTCHARToUTF8 Utf8Data(*Data);
    DataBytes.Append((uint8*)Utf8Data.Get(), Utf8Data.Length());

    if (KeyBytes.Num() > 64)
    {
        // 现在使用我们自己写的 MinimalSHA256::FSHA256，绝不报错
        MinimalSHA256::FSHA256 SHA256;
        SHA256.Update(KeyBytes.GetData(), KeyBytes.Num());
        SHA256.Final();
        KeyBytes.SetNumUninitialized(32);
        SHA256.GetHash(KeyBytes.GetData());
    }
    while (KeyBytes.Num() < 64) KeyBytes.Add(0);

    TArray<uint8> OPad, IPad;
    OPad.SetNumUninitialized(64);
    IPad.SetNumUninitialized(64);
    for (int32 i = 0; i < 64; ++i)
    {
        OPad[i] = KeyBytes[i] ^ 0x5c;
        IPad[i] = KeyBytes[i] ^ 0x36;
    }

    MinimalSHA256::FSHA256 InnerHash;
    InnerHash.Update(IPad.GetData(), 64);
    InnerHash.Update(DataBytes.GetData(), DataBytes.Num());
    InnerHash.Final();
    uint8 InnerHashBytes[32];
    InnerHash.GetHash(InnerHashBytes);

    MinimalSHA256::FSHA256 OuterHash;
    OuterHash.Update(OPad.GetData(), 64);
    OuterHash.Update(InnerHashBytes, 32);
    OuterHash.Final();
    uint8 FinalHashBytes[32];
    OuterHash.GetHash(FinalHashBytes);

    TArray<uint8> Result;
    Result.Append(FinalHashBytes, 32);
    return Result;
}