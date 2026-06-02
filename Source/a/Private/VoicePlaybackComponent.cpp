// Fill out your copyright notice in the Description page of Project Settings.


#include "VoicePlaybackComponent.h"
#include "Components/AudioComponent.h"

UVoicePlaybackComponent::UVoicePlaybackComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bIsPlaying = false;
}

void UVoicePlaybackComponent::BeginPlay()
{
    Super::BeginPlay();

    // 1. 动态创建一个 AudioComponent (相当于一个虚拟喇叭)
    AudioComponent = NewObject<UAudioComponent>(this);
    AudioComponent->bAutoActivate = false;
    AudioComponent->SetupAttachment(GetOwner()->GetRootComponent());
    AudioComponent->RegisterComponent(); // 注册到场景中
}

void UVoicePlaybackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (SoundWaveProcedural)
    {
        SoundWaveProcedural->RemoveFromRoot(); // 摘除免死金牌，允许被 GC 回收
    }
    Super::EndPlay(EndPlayReason);
}

void UVoicePlaybackComponent::PlayRawPCMData(const TArray<uint8>& AudioData, bool bIsFirstChunk, bool bIsLastChunk)
{
    if (AudioData.Num() == 0 || !AudioComponent) return;

    // 【流式核心 1】全新一句话的初始化
    if (bIsFirstChunk)
    {
        // 立刻清除可能残留的定时器
        GetWorld()->GetTimerManager().ClearTimer(StopTimerHandle);
        
        if (AudioComponent->IsPlaying())
        {
            AudioComponent->Stop();
        }
        
        // 【关键修改 2】：放弃 ResetAudio()，直接销毁重建！
        // 这是解决 UE5 连续多次流式播放卡死的最有效手段
        if (SoundWaveProcedural)
        {
            SoundWaveProcedural->RemoveFromRoot();
        }
        
        SoundWaveProcedural = NewObject<USoundWaveProcedural>();
        SoundWaveProcedural->AddToRoot();
        SoundWaveProcedural->SetSampleRate(16000); 
        SoundWaveProcedural->NumChannels = 1;      
        SoundWaveProcedural->Duration = 10000.f; // 防止提前回收
        SoundWaveProcedural->bLooping = false; 
        SoundWaveProcedural->SoundGroup = SOUNDGROUP_Voice;
        
        TotalBytesQueued = 0;
        PlaybackStartTime = GetWorld()->GetTimeSeconds();
    }
    
    // 安全检查，理论上不会触发
    if (!SoundWaveProcedural) return;

    // 累加字节数并排队
    TotalBytesQueued += AudioData.Num();
    SoundWaveProcedural->QueueAudio(AudioData.GetData(), AudioData.Num());
    
    // 【流式核心 2】启动播放
    if (bIsFirstChunk || !AudioComponent->IsPlaying())
    {
        AudioComponent->SetSound(SoundWaveProcedural);
        AudioComponent->Play();
        bIsPlaying = true;
    }
    
    // 【流式核心 3】尾帧处理与精确定时
    if (bIsLastChunk)
    {
        float RealDuration = (float)TotalBytesQueued / 32000.0f;
        float PlayedTime = GetWorld()->GetTimeSeconds() - PlaybackStartTime;
        float TimeRemaining = RealDuration - PlayedTime;
        
        // 容错处理：如果算出来已经超标，给一个极小的时间强制触发
        if (TimeRemaining <= 0.0f) TimeRemaining = 0.05f;

        UE_LOG(LogTemp, Log, TEXT("🎯 [播放组件] 尾帧到达。总长:%.2fs, 已播:%.2fs, 将在%.2fs后停止"), 
            RealDuration, PlayedTime, TimeRemaining);

        GetWorld()->GetTimerManager().SetTimer(
            StopTimerHandle, 
            this, 
            &UVoicePlaybackComponent::HandleAudioFinished, 
            TimeRemaining, 
            false
        );
    }
}

void UVoicePlaybackComponent::StopPlayback()
{
    // 手动停止时清除定时器
    GetWorld()->GetTimerManager().ClearTimer(StopTimerHandle);
    
    if (AudioComponent && AudioComponent->IsPlaying())
    {
        AudioComponent->Stop();
    }
    
    bIsPlaying = false;
    
    // 手动停止时也需要摘除波形
    if (SoundWaveProcedural)
    {
        SoundWaveProcedural->RemoveFromRoot();
        SoundWaveProcedural = nullptr;
    }
}

void UVoicePlaybackComponent::HandleAudioFinished()
{
    UE_LOG(LogTemp, Log, TEXT("🔇 [播放组件] 定时器触发，音频播放精准结束。"));
    
    // 安全措施：强制停止底层的音频组件
    if (AudioComponent && AudioComponent->IsPlaying())
    {
        AudioComponent->Stop();
    }
    
    bIsPlaying = false;
    
    // 广播给外部（如动画蓝图），通知停止播放口型/动作
    if (OnPlaybackFinished.IsBound())
    {
        OnPlaybackFinished.Broadcast();
    }
}