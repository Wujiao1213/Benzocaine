#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLMComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMResponseReceived, const FString&, AnswerText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMError, const FString&, ErrorMessage);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class A_API ULLMComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/*
	 * 构造函数：初始化默认接口地址和内部状态。
	 * 注意：配置读取放在 BeginPlay 中执行，避免编辑器加载阶段访问项目路径。
	 */
	ULLMComponent();

	/*
	 * 蓝图提问入口：向扣子智能体发送一次非流式问题。
	 * QuestionText 是用户输入文本，不能为空。
	 */
	UFUNCTION(BlueprintCallable, Category="LLM")
	void AskQuestion(const FString& QuestionText);

	/*
	 * 成功事件：当从扣子响应中解析到有效回答时广播。
	 * AnswerText 是智能体返回的最终回答文本。
	 */
	UPROPERTY(BlueprintAssignable, Category="LLM")
	FOnLLMResponseReceived OnLLMResponseReceived;

	/*
	 * 错误事件：当配置、HTTP 请求或 JSON 解析失败时广播。
	 * ErrorMessage 可直接显示在蓝图 UI 中。
	 */
	UPROPERTY(BlueprintAssignable, Category="LLM")
	FOnLLMError OnLLMError;

protected:
	/*
	 * BeginPlay：组件开始运行时读取或创建 Content/ServerConfig/CozeConfig.ini。
	 */
	virtual void BeginPlay() override;

private:
	/*
	 * 配置读取函数：读取 APIKey 和 BotID。
	 * 如果文件或字段缺失，会自动创建模板配置文件。
	 */
	void LoadServerConfig();

	/*
	 * HTTP 完成回调：检查 HTTP 响应并安全解析扣子 JSON。
	 * Request 为已完成请求，Response 为服务端响应，bWasSuccessful 为 UE HTTP 层状态。
	 */
	void HandleLLMResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	/*
	 * 错误广播函数：统一输出日志并通知蓝图。
	 * ErrorMessage 应描述当前失败原因。
	 */
	void BroadcastLLMError(const FString& ErrorMessage);

private:
	FString APIKey;
	FString BotID;
	FString ChatURL;
	FString UserID;
	bool bConfigLoaded;
};
