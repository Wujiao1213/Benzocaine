#include "LLMComponent.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Json.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMComponent, Log, All);

/*
 * 构造函数：初始化扣子接口地址、默认用户 ID 和配置状态。
 * 注意：构造阶段可能发生在编辑器资源加载期间，因此不在这里读取配置文件。
 */
ULLMComponent::ULLMComponent()
	: ChatURL(TEXT("https://api.coze.cn/open_api/v2/chat"))
	, UserID(TEXT("user_123"))
	, bConfigLoaded(false)
{
	PrimaryComponentTick.bCanEverTick = false;
}

/*
 * BeginPlay：组件进入游戏世界后读取扣子配置。
 * 如果配置文件不存在，LoadServerConfig 会自动创建模板文件。
 */
void ULLMComponent::BeginPlay()
{
	Super::BeginPlay();
	LoadServerConfig();
}

/*
 * 蓝图提问入口：构造并发送一次扣子 HTTP POST 请求。
 * QuestionText 会先去除首尾空白；空文本或无效配置会在发起网络请求前直接报错。
 */
void ULLMComponent::AskQuestion(const FString& QuestionText)
{
	const FString CleanQuestion = QuestionText.TrimStartAndEnd();
	if (CleanQuestion.IsEmpty())
	{
		BroadcastLLMError(TEXT("Question text is empty."));
		return;
	}

	if (!bConfigLoaded)
	{
		LoadServerConfig();
	}

	if (APIKey.IsEmpty() || BotID.IsEmpty() || APIKey == TEXT("YOUR_COZE_API_KEY") || BotID == TEXT("YOUR_COZE_BOT_ID"))
	{
		BroadcastLLMError(TEXT("CozeConfig.ini APIKey or BotID is not configured."));
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("bot_id"), BotID);
	RootObject->SetStringField(TEXT("user"), UserID);
	RootObject->SetStringField(TEXT("query"), CleanQuestion);
	RootObject->SetBoolField(TEXT("stream"), false);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		BroadcastLLMError(TEXT("Failed to serialize Coze request JSON."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ChatURL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(RequestBody);

	TWeakObjectPtr<ULLMComponent> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			WeakThis->HandleLLMResponse(CompletedRequest, Response, bWasSuccessful);
		});

	UE_LOG(LogLLMComponent, Log, TEXT("Sending question to Coze: %s"), *CleanQuestion);
	if (!Request->ProcessRequest())
	{
		BroadcastLLMError(TEXT("Failed to start HTTP request."));
	}
}

/*
 * 配置读取函数：从 Content/ServerConfig/CozeConfig.ini 读取 APIKey 和 BotID。
 * 如果文件或字段缺失，会生成模板配置，避免空凭证请求。
 */
void ULLMComponent::LoadServerConfig()
{
	const FString ConfigDir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("ServerConfig"));
	const FString ConfigFilePath = FPaths::Combine(ConfigDir, TEXT("CozeConfig.ini"));

	IFileManager::Get().MakeDirectory(*ConfigDir, true);

	const bool bReadAPIKey = GConfig && GConfig->GetString(TEXT("Coze"), TEXT("APIKey"), APIKey, ConfigFilePath);
	const bool bReadBotID = GConfig && GConfig->GetString(TEXT("Coze"), TEXT("BotID"), BotID, ConfigFilePath);

	APIKey = APIKey.TrimStartAndEnd();
	BotID = BotID.TrimStartAndEnd();

	if (!bReadAPIKey || !bReadBotID || APIKey.IsEmpty() || BotID.IsEmpty())
	{
		APIKey = TEXT("YOUR_COZE_API_KEY");
		BotID = TEXT("YOUR_COZE_BOT_ID");

		if (GConfig)
		{
			GConfig->SetString(TEXT("Coze"), TEXT("APIKey"), *APIKey, ConfigFilePath);
			GConfig->SetString(TEXT("Coze"), TEXT("BotID"), *BotID, ConfigFilePath);
			GConfig->Flush(false, ConfigFilePath);
		}
		else
		{
			const FString TemplateContent = TEXT("[Coze]\r\nAPIKey=YOUR_COZE_API_KEY\r\nBotID=YOUR_COZE_BOT_ID\r\n");
			FFileHelper::SaveStringToFile(TemplateContent, *ConfigFilePath);
		}

		bConfigLoaded = false;
		BroadcastLLMError(FString::Printf(TEXT("Coze config template created: %s"), *ConfigFilePath));
		return;
	}

	bConfigLoaded = true;
	UE_LOG(LogLLMComponent, Log, TEXT("Coze config loaded. APIKeyLen=%d BotIDLen=%d"), APIKey.Len(), BotID.Len());
}

/*
 * HTTP 完成回调：检查 HTTP 结果并防御性解析扣子 JSON。
 * 当前 v2/chat 响应顶层包含 messages 数组，最终回答位于 type == answer 的 content 字段。
 */
void ULLMComponent::HandleLLMResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		BroadcastLLMError(TEXT("Coze HTTP request failed or response is invalid."));
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseText = Response->GetContentAsString();
	UE_LOG(LogLLMComponent, Log, TEXT("Coze full response: %s"), *ResponseText);

	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		BroadcastLLMError(FString::Printf(TEXT("Coze HTTP error. Status=%d Response=%s"), ResponseCode, *ResponseText));
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		BroadcastLLMError(TEXT("Failed to parse Coze response JSON."));
		return;
	}

	int32 Code = INDEX_NONE;
	if (RootObject->TryGetNumberField(TEXT("code"), Code) && Code != 0)
	{
		FString Message;
		RootObject->TryGetStringField(TEXT("msg"), Message);
		BroadcastLLMError(FString::Printf(TEXT("Coze API error. Code=%d Message=%s"), Code, *Message));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* MessagesArray = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("messages"), MessagesArray) || !MessagesArray)
	{
		BroadcastLLMError(TEXT("Coze response does not contain messages array."));
		return;
	}

	for (const TSharedPtr<FJsonValue>& MessageValue : *MessagesArray)
	{
		if (!MessageValue.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> MessageObject = MessageValue->AsObject();
		if (!MessageObject.IsValid())
		{
			continue;
		}

		FString MessageType;
		if (!MessageObject->TryGetStringField(TEXT("type"), MessageType) || MessageType != TEXT("answer"))
		{
			continue;
		}

		FString AnswerText;
		if (MessageObject->TryGetStringField(TEXT("content"), AnswerText) && !AnswerText.IsEmpty())
		{
			UE_LOG(LogLLMComponent, Log, TEXT("Coze answer parsed: %s"), *AnswerText);
			OnLLMResponseReceived.Broadcast(AnswerText);
			return;
		}
	}

	BroadcastLLMError(TEXT("Coze messages array does not contain a valid answer content."));
}

/*
 * 错误广播函数：输出日志并把可读错误传给蓝图。
 * 这里保持纯文本，方便 UI 控件直接显示。
 */
void ULLMComponent::BroadcastLLMError(const FString& ErrorMessage)
{
	UE_LOG(LogLLMComponent, Error, TEXT("%s"), *ErrorMessage);
	OnLLMError.Broadcast(ErrorMessage);
}
