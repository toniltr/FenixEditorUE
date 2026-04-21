#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "FenixAuthService.generated.h"

// ─── Structs de Auth ──────────────────────────────────────────
// (Mantenidos aquí para que sean accesibles desde Blueprints
//  sin depender del subsystem completo)

USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixUser
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Email;
    UPROPERTY(BlueprintReadOnly) FString Role;
    UPROPERTY(BlueprintReadOnly) FString CreatedAt;
};

USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixSession
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString    AccessToken;
    UPROPERTY(BlueprintReadOnly) FString    RefreshToken;
    UPROPERTY(BlueprintReadOnly) int32      ExpiresIn = 0;
    UPROPERTY(BlueprintReadOnly) FString    TokenType;
    UPROPERTY(BlueprintReadOnly) FFenixUser User;

    bool IsValid() const { return !AccessToken.IsEmpty(); }
};

// ─── Delegates ────────────────────────────────────────────────

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnAuthResult,
    bool,                bSuccess,
    const FFenixSession&, Session,
    const FString&,      ErrorMessage
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnLogoutResult,
    bool, bSuccess
);

// ─────────────────────────────────────────────────────────────

/**
 * UFenixAuthService
 *
 * Responsabilidad única: gestión de identidad del usuario.
 *   - Peticiones HTTP de autenticación (Login, Register, Logout, Refresh)
 *   - Parseo de la sesión devuelta por Supabase
 *   - Persistencia de la sesión en el .ini de usuario (GConfig)
 *
 * El subsystem principal lo instancia como miembro y delega
 * todo lo relacionado con auth a esta clase.
 *
 * No expone métodos de datos (FetchStory, etc.) — eso es
 * responsabilidad de UFenixSupabaseSubsystem.
 */
UCLASS()
class FENIXRUNTIME_API UFenixAuthService : public UObject
{
    GENERATED_BODY()

public:

    // ── Inicialización ────────────────────────────────────────

    /**
     * Llamar desde UFenixSupabaseSubsystem::Initialize().
     * Recibe la URL y clave de Supabase ya leídas del DeveloperSettings.
     * Intenta restaurar la sesión previa del .ini y refresca el token si existe.
     */
    void Initialize(const FString& InSupabaseUrl, const FString& InAnonKey);

    // ── Eventos ───────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnLoginResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnRegisterResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnLogoutResult OnLogoutResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnRefreshSessionResult;

    // ── API pública ───────────────────────────────────────────

    /** Login con email + contraseña. Dispara OnLoginResult al terminar. */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Login(const FString& Email, const FString& Password);

    /** Registro de nuevo usuario. Dispara OnRegisterResult al terminar. */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Register(const FString& Email, const FString& Password);

    /** Cierra sesión e invalida el token en Supabase. Dispara OnLogoutResult. */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Logout();

    /**
     * Refresca el access token usando el refresh token guardado.
     * Se llama automáticamente desde Initialize() si hay sesión guardada.
     */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void RefreshSession();

    // ── Consultas de estado (sin HTTP) ────────────────────────

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    bool IsLoggedIn() const { return CurrentSession.IsValid(); }

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixSession& GetSession() const { return CurrentSession; }

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixUser& GetCurrentUser() const { return CurrentSession.User; }

    /** Devuelve el access token activo (o el anon key si no hay sesión). */
    FString GetEffectiveToken() const;

private:

    // ── Config ────────────────────────────────────────────────
    FString SupabaseUrl;
    FString SupabaseAnonKey;

    // ── Estado interno ────────────────────────────────────────
    FFenixSession CurrentSession;

    // ── Persistencia ──────────────────────────────────────────
    void SaveSession();
    void LoadSession();
    void ClearSession();

    // ── HTTP helpers ──────────────────────────────────────────
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeAuthRequest(
        const FString& Endpoint,
        const FString& Method,
        const FString& JsonBody
    );

    // ── Parseo ────────────────────────────────────────────────
    bool    ParseSession(const FString& JsonStr, FFenixSession& OutSession);
    FString ExtractErrorMessage(const FString& JsonStr);

    // ── Callbacks HTTP ────────────────────────────────────────
    void OnLoginResponse   (FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnRegisterResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnLogoutResponse  (FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnRefreshResponse (FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
};
