// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "VoicePlaybackComponent.generated.h"

// 定义一个委托，当声音彻底播放完毕时触发（用于通知数字人停止口型/动作）
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVoicePlaybackFinished);

UCLASS( ClassGroup=(AI), meta=(BlueprintSpawnableComponent) )
class A_API UVoicePlaybackComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UVoicePlaybackComponent();

	// 核心暴露接口：接收来自 TTS 组件的二进制数据并播放
	UFUNCTION(BlueprintCallable, Category = "AI|AudioPlayback")
	void PlayRawPCMData(const TArray<uint8>& AudioData, bool bIsFirstChunk, bool bIsLastChunk);

	// 停止播放并清空缓存
	UFUNCTION(BlueprintCallable, Category = "AI|AudioPlayback")
	void StopPlayback();
	
	/** 当前是否正在播放语音（公开给蓝图使用，用于驱动动画状态机） */
	UPROPERTY(BlueprintReadOnly, Category = "AI|AudioPlayback")
	bool bIsPlaying = false;

	// 播放结束时的事件
	UPROPERTY(BlueprintAssignable, Category = "AI|AudioPlayback")
	FOnVoicePlaybackFinished OnPlaybackFinished;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 监听 AudioComponent 的播放完成事件
	UFUNCTION()
	void HandleAudioFinished();

	// 实际发出声音的喇叭
	UPROPERTY()
	UAudioComponent* AudioComponent;
	
	// 统计当前这一句话的总字节数
	uint32 TotalBytesQueued = 0;

	// 负责接收和解析内存 PCM 数据的程序化波形
	UPROPERTY()
	USoundWaveProcedural* SoundWaveProcedural;
	
	/** 记录音频开始播放时的世界时间（秒） */
	float PlaybackStartTime = 0.0f;
	
	// 用于控制音频精准结束的定时器句柄
	FTimerHandle StopTimerHandle;
};
