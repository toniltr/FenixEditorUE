#include "Auth/FenixAuthService.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "FenixSupabaseRoutes.h"

// ─────────────────────────────────────────────────────────────
// Initialize
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::Initialize(const FString& InSupabaseUrl, const FString& InAnonKey)
{
    SupabaseUrl     = InSupabaseUrl;
    SupabaseAnonKey = InAnonKey;

    LoadSession();

    UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Inicializado. Sesión activa: %s"),
           IsLoggedIn() ? TEXT("SÍ") : TEXT("NO"));
}

// ─────────────────────────────────────────────────────────────
// GetEffectiveToken
// ─────────────────────────────────────────────────────────────
FString UFenixAuthService::GetEffectiveToken() const
{
    return IsLoggedIn() ? CurrentSession.AccessToken : SupabaseAnonKey;
}

// ─────────────────────────────────────────────────────────────
// Login
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::Login(const FString& Email, const FString& Password)
{
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(FenixSupabaseRoutes::Auth::Login(), TEXT("POST"), JsonStr);
    Req->OnProcessRequestComplete().BindUObject(this, &UFenixAuthService::OnLoginResponse);
    Req->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Login iniciado para: %s"), *Email);
}

void UFenixAuthService::OnLoginResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        OnLoginResult.Broadcast(false, FFenixSession{}, TEXT("Sin conexión"));
        return;
    }

    int32   Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    if (Code != 200)
    {
        FString Err = ExtractErrorMessage(Body);
        UE_LOG(LogTemp, Warning, TEXT("[FenixAuth] Login fallido %d: %s"), Code, *Err);
        OnLoginResult.Broadcast(false, FFenixSession{}, Err);
        return;
    }

    FFenixSession Session;
    if (!ParseSession(Body, Session))
    {
        OnLoginResult.Broadcast(false, FFenixSession{}, TEXT("Error parseando sesión"));
        return;
    }

    CurrentSession = Session;
    SaveSession();

    UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Login OK — usuario: %s"), *Session.User.Email);
    OnLoginResult.Broadcast(true, CurrentSession, TEXT(""));
}

// ─────────────────────────────────────────────────────────────
// Register
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::Register(const FString& Email, const FString& Password)
{
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(FenixSupabaseRoutes::Auth::Register(), TEXT("POST"), JsonStr);
    Req->OnProcessRequestComplete().BindUObject(this, &UFenixAuthService::OnRegisterResponse);
    Req->ProcessRequest();
}

void UFenixAuthService::OnRegisterResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        OnRegisterResult.Broadcast(false, FFenixSession{}, TEXT("Sin conexión"));
        return;
    }

    int32   Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    if (Code != 200)
    {
        OnRegisterResult.Broadcast(false, FFenixSession{}, ExtractErrorMessage(Body));
        return;
    }

    // Supabase puede devolver sesión vacía si requiere confirmación de email
    FFenixSession Session;
    ParseSession(Body, Session);

    if (Session.IsValid())
    {
        CurrentSession = Session;
        SaveSession();
    }

    UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Registro OK para: %s"), *Session.User.Email);
    OnRegisterResult.Broadcast(true, CurrentSession, TEXT(""));
}

// ─────────────────────────────────────────────────────────────
// Logout
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::Logout()
{
    if (!IsLoggedIn())
    {
        OnLogoutResult.Broadcast(true);
        return;
    }

    auto Req = MakeAuthRequest(FenixSupabaseRoutes::Auth::Logout(), TEXT("POST"), TEXT("{}"));
    Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + CurrentSession.AccessToken);
    Req->OnProcessRequestComplete().BindUObject(this, &UFenixAuthService::OnLogoutResponse);
    Req->ProcessRequest();
}

void UFenixAuthService::OnLogoutResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    // Limpiamos siempre la sesión local aunque el servidor falle
    ClearSession();
    UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Sesión cerrada"));
    OnLogoutResult.Broadcast(true);
}

// ─────────────────────────────────────────────────────────────
// RefreshSession
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::RefreshSession()
{
    if (CurrentSession.RefreshToken.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[FenixAuth] No hay refresh token"));
        return;
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("refresh_token"), CurrentSession.RefreshToken);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    auto Req = MakeAuthRequest(FenixSupabaseRoutes::Auth::RefreshToken(), TEXT("POST"), JsonStr);
    Req->OnProcessRequestComplete().BindUObject(this, &UFenixAuthService::OnRefreshResponse);
    Req->ProcessRequest();
}

void UFenixAuthService::OnRefreshResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid() || Res->GetResponseCode() != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FenixAuth] Refresh fallido — cerrando sesión"));
        ClearSession();
        OnRefreshSessionResult.Broadcast(false, FFenixSession{}, TEXT("Failed to refresh session"));
        return;
    }

    FFenixSession NewSession;
    if (ParseSession(Res->GetContentAsString(), NewSession))
    {
        CurrentSession = NewSession;
        SaveSession();
        UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Token refrescado OK"));
        OnRefreshSessionResult.Broadcast(true, CurrentSession, TEXT(""));
    }
}

// ─────────────────────────────────────────────────────────────
// Persistencia — GConfig
// ─────────────────────────────────────────────────────────────
void UFenixAuthService::SaveSession()
{
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

void UFenixAuthService::LoadSession()
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
        CurrentSession.AccessToken  = AccessToken;
        CurrentSession.RefreshToken = RefreshToken;
        CurrentSession.User.Id      = UserId;
        CurrentSession.User.Email   = UserEmail;

        UE_LOG(LogTemp, Log, TEXT("[FenixAuth] Sesión restaurada para: %s"), *UserEmail);

        // El access token puede haber caducado — refrescamos automáticamente
        RefreshSession();
    }
}

void UFenixAuthService::ClearSession()
{
    CurrentSession = FFenixSession{};

    GConfig->SetString(TEXT("FenixSession"), TEXT("AccessToken"),  TEXT(""), GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("RefreshToken"), TEXT(""), GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("UserId"),       TEXT(""), GGameUserSettingsIni);
    GConfig->SetString(TEXT("FenixSession"), TEXT("UserEmail"),    TEXT(""), GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

// ─────────────────────────────────────────────────────────────
// HTTP helper
// ─────────────────────────────────────────────────────────────
TSharedRef<IHttpRequest, ESPMode::ThreadSafe>
UFenixAuthService::MakeAuthRequest(
    const FString& Endpoint, const FString& Method, const FString& JsonBody)
{
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(SupabaseUrl + Endpoint);
    Req->SetVerb(Method);
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetHeader(TEXT("apikey"),       SupabaseAnonKey);
    Req->SetContentAsString(JsonBody);
    return Req;
}

// ─────────────────────────────────────────────────────────────
// ParseSession
// ─────────────────────────────────────────────────────────────
bool UFenixAuthService::ParseSession(const FString& JsonStr, FFenixSession& OutSession)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);

    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        return false;

    Root->TryGetStringField(TEXT("access_token"),  OutSession.AccessToken);
    Root->TryGetStringField(TEXT("refresh_token"), OutSession.RefreshToken);
    Root->TryGetStringField(TEXT("token_type"),    OutSession.TokenType);

    double ExpiresIn = 0.0;
    if (Root->TryGetNumberField(TEXT("expires_in"), ExpiresIn))
        OutSession.ExpiresIn = (int32)ExpiresIn;

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
// ExtractErrorMessage
// ─────────────────────────────────────────────────────────────
FString UFenixAuthService::ExtractErrorMessage(const FString& JsonStr)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);

    if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
    {
        FString Msg;
        if (Root->TryGetStringField(TEXT("error_description"), Msg)) return Msg;
        if (Root->TryGetStringField(TEXT("msg"),               Msg)) return Msg;
        if (Root->TryGetStringField(TEXT("message"),           Msg)) return Msg;
    }

    return TEXT("Error desconocido");
}
