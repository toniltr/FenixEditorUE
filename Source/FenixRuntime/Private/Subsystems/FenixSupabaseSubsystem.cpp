#include "Subsystems/FenixSupabaseSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "FenixDeveloperSettings.h"
#include "FenixSupabaseRoutes.h"
#include "Parsing/FenixStoryParser.h"    // ← parseo delegado (clase futura)

// ─────────────────────────────────────────────────────────────
// Initialize / Deinitialize
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // ── Leer configuración ────────────────────────────────────
    const UFenixDeveloperSettings* Settings = UFenixDeveloperSettings::Get();
    if (Settings)
    {
        SupabaseUrl     = Settings->SupabaseUrl;
        SupabaseAnonKey = Settings->SupabaseAnonKey;

        if (Settings->bVerboseLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("[Fenix] URL: %s"), *SupabaseUrl);
            UE_LOG(LogTemp, Log, TEXT("[Fenix] AnonKey configurada: %s"),
                   SupabaseAnonKey.IsEmpty() ? TEXT("NO") : TEXT("SÍ"));
        }
    }

    // ── Crear y enlazar AuthService ───────────────────────────
    AuthService = NewObject<UFenixAuthService>(this);

    // Reenviar los eventos de auth al subsystem para que Blueprints
    // solo necesiten escuchar un único objeto
    AuthService->OnLoginResult.AddDynamic(this,
        &UFenixSupabaseSubsystem::OnLoginResult.operator());   // ver nota (*)
    AuthService->OnRegisterResult.AddDynamic(this,
        &UFenixSupabaseSubsystem::OnRegisterResult.operator());
    AuthService->OnLogoutResult.AddDynamic(this,
        &UFenixSupabaseSubsystem::OnLogoutResult.operator());
    AuthService->OnRefreshSessionResult.AddDynamic(this,
        &UFenixSupabaseSubsystem::OnRefreshSessionResult.operator());

    // (*) Alternativa más limpia si prefieres lambdas en lugar de AddDynamic:
    //
    //   AuthService->OnLoginResult.AddLambda(
    //       [this](bool bOk, const FFenixSession& S, const FString& Err)
    //       { OnLoginResult.Broadcast(bOk, S, Err); });
    //
    // La lambda evita tener que exponer handlers intermedios en el .h
    // y es la opción recomendada al refactorizar delegates existentes.

    AuthService->Initialize(SupabaseUrl, SupabaseAnonKey);

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Subsystem iniciado"));
}

void UFenixSupabaseSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

// ─────────────────────────────────────────────────────────────
// AUTH — reenvío al servicio
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Login(const FString& Email, const FString& Password)
{
    AuthService->Login(Email, Password);
}

void UFenixSupabaseSubsystem::Register(const FString& Email, const FString& Password)
{
    AuthService->Register(Email, Password);
}

void UFenixSupabaseSubsystem::Logout()
{
    AuthService->Logout();
}

void UFenixSupabaseSubsystem::RefreshSession()
{
    AuthService->RefreshSession();
}

bool UFenixSupabaseSubsystem::IsLoggedIn() const
{
    return AuthService && AuthService->IsLoggedIn();
}

const FFenixSession& UFenixSupabaseSubsystem::GetSession() const
{
    return AuthService->GetSession();
}

const FFenixUser& UFenixSupabaseSubsystem::GetCurrentUser() const
{
    return AuthService->GetCurrentUser();
}

// ─────────────────────────────────────────────────────────────
// DATA API — FetchStory
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::FetchStory(const FString& StoryUUID)
{
    if (!IsLoggedIn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory: no hay sesión activa"));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    auto Req = MakeDataRequest(
        FenixSupabaseRoutes::Data::FetchStory(StoryUUID),
        /*bUseAuthToken=*/true
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnFetchStoryResponse);
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::OnFetchStoryResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory: sin conexión"));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    if (Res->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory fallido: %d"),
               Res->GetResponseCode());
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    FFenixStory Story;
    if (!UFenixStoryParser::ParseStory(Res->GetContentAsString(), Story))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory: error parseando historia"));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    CurrentStory = Story;
    bStoryLoaded = true;

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Historia cargada: %s (%d escenas, %d NPCs, %d quests)"),
           *Story.Name, Story.Scenes.Num(), Story.Npcs.Num(), Story.Quests.Num());

    OnStoryLoaded.Broadcast(true, CurrentStory);
}

// ─────────────────────────────────────────────────────────────
// DATA API — FetchPublishedStories / FetchMyStories
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::FetchPublishedStories()
{
    auto Req = MakeDataRequest(
        FenixSupabaseRoutes::Data::FetchPublishedStories(),
        /*bUseAuthToken=*/IsLoggedIn()
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnFetchStoriesResponse);
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::FetchMyStories()
{
    if (!IsLoggedIn())
    {
        OnStoriesListLoaded.Broadcast(false, {});
        return;
    }

    auto Req = MakeDataRequest(
        FenixSupabaseRoutes::Data::FetchMyStories(),
        /*bUseAuthToken=*/true
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnFetchStoriesResponse);
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::OnFetchStoriesResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid() || Res->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStories fallido"));
        OnStoriesListLoaded.Broadcast(false, TArray<FFenixStory>{});
        return;
    }

    TArray<TSharedPtr<FJsonValue>> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
        Res->GetContentAsString());

    if (!FJsonSerializer::Deserialize(Reader, Root))
    {
        OnStoriesListLoaded.Broadcast(false, TArray<FFenixStory>{});
        return;
    }

    TArray<FFenixStory> Stories;
    for (const auto& Val : Root)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!Val->TryGetObject(Obj)) continue;

        FFenixStory Story;
        (*Obj)->TryGetStringField(TEXT("uuid"),        Story.UUID);
        (*Obj)->TryGetStringField(TEXT("name"),        Story.Name);
        (*Obj)->TryGetStringField(TEXT("description"), Story.Description);
        (*Obj)->TryGetStringField(TEXT("status"),      Story.Status);
        if (!Story.UUID.IsEmpty()) Stories.Add(Story);
    }

    UE_LOG(LogTemp, Log, TEXT("[Fenix] %d historias cargadas"), Stories.Num());
    OnStoriesListLoaded.Broadcast(true, Stories);
}

// ─────────────────────────────────────────────────────────────
// HTTP helper de datos
// ─────────────────────────────────────────────────────────────
TSharedRef<IHttpRequest, ESPMode::ThreadSafe>
UFenixSupabaseSubsystem::MakeDataRequest(
    const FString& Endpoint, bool bUseAuthToken)
{
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(SupabaseUrl + Endpoint);
    Req->SetVerb(TEXT("GET"));
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetHeader(TEXT("apikey"),       SupabaseAnonKey);
    Req->SetHeader(TEXT("Authorization"),
                   TEXT("Bearer ") + AuthService->GetEffectiveToken());
    return Req;
}
