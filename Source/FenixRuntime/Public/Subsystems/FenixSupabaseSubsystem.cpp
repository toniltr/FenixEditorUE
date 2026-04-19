#include "Subsystems/FenixSupabaseSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"

// Nombre del slot para persistir sesión
static const FString FenixSaveSlot = TEXT("FenixSession");
static const int32   FenixSaveIdx  = 0;

// ─────────────────────────────────────────────────────────────
// Init / Deinit
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadSessionFromSlot();   // Intenta restaurar sesión previa
    UE_LOG(LogTemp, Log, TEXT("[Fenix] Subsystem iniciado. Sesión activa: %s"),
           IsLoggedIn() ? TEXT("SÍ") : TEXT("NO"));
}

void UFenixSupabaseSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

// ─────────────────────────────────────────────────────────────
// AUTH — Login
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Login(const FString& Email, const FString& Password)
{
    // Body JSON
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(
        TEXT("/auth/v1/token?grant_type=password"),
        TEXT("POST"),
        JsonStr
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnLoginResponse
    );
    Req->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Login iniciado para: %s"), *Email);
}

void UFenixSupabaseSubsystem::OnLoginResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        OnLoginResult.Broadcast(false, FFenixSession{}, TEXT("Sin conexión"));
        return;
    }

    int32 Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    if (Code != 200)
    {
        // Extrae el mensaje de error de Supabase
        FString ErrorMsg = ExtractErrorMessage(Body);
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] Login fallido %d: %s"), Code, *ErrorMsg);
        OnLoginResult.Broadcast(false, FFenixSession{}, ErrorMsg);
        return;
    }

    FFenixSession Session;
    if (!ParseSession(Body, Session))
    {
        OnLoginResult.Broadcast(false, FFenixSession{}, TEXT("Error parseando sesión"));
        return;
    }

    CurrentSession = Session;
    SaveSessionToSlot();

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Login OK — usuario: %s"), *Session.User.Email);
    OnLoginResult.Broadcast(true, CurrentSession, TEXT(""));
}

// ─────────────────────────────────────────────────────────────
// AUTH — Register
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Register(const FString& Email, const FString& Password)
{
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(
        TEXT("/auth/v1/signup"),
        TEXT("POST"),
        JsonStr
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnRegisterResponse
    );
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::OnRegisterResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        OnRegisterResult.Broadcast(false, FFenixSession{}, TEXT("Sin conexión"));
        return;
    }

    int32 Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    // Supabase devuelve 200 si el email necesita confirmación
    // o 200 con sesión si email confirm está desactivado
    if (Code != 200)
    {
        FString ErrorMsg = ExtractErrorMessage(Body);
        OnRegisterResult.Broadcast(false, FFenixSession{}, ErrorMsg);
        return;
    }

    FFenixSession Session;
    ParseSession(Body, Session);   // Puede estar vacía si requiere confirmación

    if (Session.IsValid())
    {
        CurrentSession = Session;
        SaveSessionToSlot();
    }

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Registro OK para: %s"), *Session.User.Email);
    OnRegisterResult.Broadcast(true, CurrentSession, TEXT(""));
}

// ─────────────────────────────────────────────────────────────
// AUTH — Logout
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::Logout()
{
    if (!IsLoggedIn())
    {
        OnLogoutResult.Broadcast(true);
        return;
    }

    auto Req = MakeAuthRequest(
        TEXT("/auth/v1/logout"),
        TEXT("POST"),
        TEXT("{}")
    );
    // Para logout necesitamos el Bearer token
    Req->SetHeader(TEXT("Authorization"),
                   TEXT("Bearer ") + CurrentSession.AccessToken);

    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnLogoutResponse
    );
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::OnLogoutResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    // Limpiamos siempre la sesión local aunque falle el servidor
    CurrentSession = FFenixSession{};
    bStoryLoaded   = false;
    SaveSessionToSlot();   // Borra el slot

    UE_LOG(LogTemp, Log, TEXT("[Fenix] Sesión cerrada"));
    OnLogoutResult.Broadcast(true);
}

// ─────────────────────────────────────────────────────────────
// AUTH — Refresh Token
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::RefreshSession()
{
    if (CurrentSession.RefreshToken.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] No hay refresh token"));
        return;
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("refresh_token"), CurrentSession.RefreshToken);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(
        TEXT("/auth/v1/token?grant_type=refresh_token"),
        TEXT("POST"),
        JsonStr
    );
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnRefreshResponse
    );
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::OnRefreshResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid() || Res->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] Refresh fallido, cerrando sesión"));
        CurrentSession = FFenixSession{};
        SaveSessionToSlot();
        return;
    }

    FFenixSession NewSession;
    if (ParseSession(Res->GetContentAsString(), NewSession))
    {
        CurrentSession = NewSession;
        SaveSessionToSlot();
        UE_LOG(LogTemp, Log, TEXT("[Fenix] Token refrescado OK"));
    }
}

// ─────────────────────────────────────────────────────────────
// DATA API — usa el token si hay sesión
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::FetchStory(const FString& StoryUUID)
{
    if (!IsLoggedIn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory: no hay sesión activa"));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    FString Endpoint = FString::Printf(
        TEXT("/rest/v1/stories?uuid=eq.%s&select=*,scenes(*,items(*),scene_npcs(*)),npcs(*),quests(*,objectives(*))"),
        *StoryUUID
    );

    auto Req = MakeRequest(Endpoint, TEXT("GET"), /*bUseAuthToken=*/true);
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnFetchStoryResponse
    );
    Req->ProcessRequest();
}

void UFenixSupabaseSubsystem::FetchPublishedStories()
{
    FString Endpoint = TEXT("/rest/v1/stories?status=eq.PUBLISH&select=uuid,name,description,status,updated_at");
    bool bAuth = IsLoggedIn();

    auto Req = MakeRequest(Endpoint, TEXT("GET"), bAuth);
    Req->OnProcessRequestComplete().BindUObject(
        this, &UFenixSupabaseSubsystem::OnFetchStoriesResponse
    );
    Req->ProcessRequest();
}

// ─────────────────────────────────────────────────────────────
// HTTP helpers
// ─────────────────────────────────────────────────────────────
TSharedRef<IHttpRequest, ESPMode::ThreadSafe>
UFenixSupabaseSubsystem::MakeRequest(
    const FString& Endpoint, const FString& Method, bool bUseAuthToken)
{
    auto& Http = FHttpModule::Get();
    auto  Req  = Http.CreateRequest();

    Req->SetURL(SupabaseUrl + Endpoint);
    Req->SetVerb(Method);
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetHeader(TEXT("apikey"),       SupabaseAnonKey);

    // Si hay sesión activa usamos el JWT del usuario (respeta RLS)
    // Si no, usamos el anon key (solo accede a datos públicos)
    FString Token = (bUseAuthToken && IsLoggedIn())
        ? CurrentSession.AccessToken
        : SupabaseAnonKey;

    Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);

    return Req;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe>
UFenixSupabaseSubsystem::MakeAuthRequest(
    const FString& Endpoint, const FString& Method, const FString& JsonBody)
{
    auto& Http = FHttpModule::Get();
    auto  Req  = Http.CreateRequest();

    Req->SetURL(SupabaseUrl + Endpoint);
    Req->SetVerb(Method);
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetHeader(TEXT("apikey"),       SupabaseAnonKey);
    Req->SetContentAsString(JsonBody);

    return Req;
}

// ─────────────────────────────────────────────────────────────
// Parseo sesión
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseSession(
    const FString& JsonStr, FFenixSession& OutSession)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);

    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        return false;

    OutSession.AccessToken  = Root->GetStringField(TEXT("access_token"));
    OutSession.RefreshToken = Root->GetStringField(TEXT("refresh_token"));
    OutSession.ExpiresIn    = (int32)Root->GetNumberField(TEXT("expires_in"));
    OutSession.TokenType    = Root->GetStringField(TEXT("token_type"));

    // Usuario anidado
    const TSharedPtr<FJsonObject>* UserObj;
    if (Root->TryGetObjectField(TEXT("user"), UserObj) && UserObj->IsValid())
    {
        OutSession.User.Id        = (*UserObj)->GetStringField(TEXT("id"));
        OutSession.User.Email     = (*UserObj)->GetStringField(TEXT("email"));
        OutSession.User.Role      = (*UserObj)->GetStringField(TEXT("role"));
        OutSession.User.CreatedAt = (*UserObj)->GetStringField(TEXT("created_at"));
    }

    return OutSession.IsValid();
}

// ─────────────────────────────────────────────────────────────
// Extrae mensaje de error de respuesta Supabase
// ─────────────────────────────────────────────────────────────
FString UFenixSupabaseSubsystem::ExtractErrorMessage(const FString& JsonStr)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);

    if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
    {
        // Supabase puede devolver "error_description" o "msg"
        FString Msg;
        if (Root->TryGetStringField(TEXT("error_description"), Msg)) return Msg;
        if (Root->TryGetStringField(TEXT("msg"),               Msg)) return Msg;
        if (Root->TryGetStringField(TEXT("message"),           Msg)) return Msg;
    }
    return TEXT("Error desconocido");
}

// ─────────────────────────────────────────────────────────────
// Persistencia de sesión (SaveGame simple)
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::SaveSessionToSlot()
{
    // Usamos GConfig para guardar tokens en el ini de usuario
    // (alternativa: UGameplayStatics::SaveGameToSlot con un SaveGame custom)
    GConfig->SetString(TEXT("FenixSession"), TEXT("AccessToken"),
                       *CurrentSession.AccessToken,  GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("RefreshToken"),
                       *CurrentSession.RefreshToken, GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("UserId"),
                       *CurrentSession.User.Id,      GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("UserEmail"),
                       *CurrentSession.User.Email,   GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void UFenixSupabaseSubsystem::LoadSessionFromSlot()
{
    FString AccessToken, RefreshToken, UserId, UserEmail;

    GConfig->GetString(TEXT("FenixSession"), TEXT("AccessToken"),
                       AccessToken,  GGameUserSettingsIni);
    GConfig->GetString(TEXT("FenixSession"), TEXT("RefreshToken"),
                       RefreshToken, GGameUserSettingsIni);
    GConfig->GetString(TEXT("FenixSession"), TEXT("UserId"),
                       UserId,       GGameUserSettingsIni);
    GConfig->GetString(TEXT("FenixSession"), TEXT("UserEmail"),
                       UserEmail,    GGameUserSettingsIni);

    if (!AccessToken.IsEmpty())
    {
        CurrentSession.AccessToken        = AccessToken;
        CurrentSession.RefreshToken       = RefreshToken;
        CurrentSession.User.Id            = UserId;
        CurrentSession.User.Email         = UserEmail;

        UE_LOG(LogTemp, Log, TEXT("[Fenix] Sesión restaurada para: %s"), *UserEmail);

        // Refresca el token automáticamente al arrancar
        // (el access token puede haber caducado)
        RefreshSession();
    }
}