#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Data/FenixStoryData.h"
#include "FenixSupabaseSubsystem.generated.h"

// ─── Structs de Auth ─────────────────────────────────────────

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

    UPROPERTY(BlueprintReadOnly) FString AccessToken;
    UPROPERTY(BlueprintReadOnly) FString RefreshToken;
    UPROPERTY(BlueprintReadOnly) int32   ExpiresIn  = 0;
    UPROPERTY(BlueprintReadOnly) FString TokenType;
    UPROPERTY(BlueprintReadOnly) FFenixUser User;

    bool IsValid() const { return !AccessToken.IsEmpty(); }
};

// ─── Delegates ───────────────────────────────────────────────

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnStoryLoaded,
    bool,               bSuccess,
    const FFenixStory&, Story
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnStoriesListLoaded,
    bool,                      bSuccess,
    const TArray<FFenixStory>&, Stories
);

// ─────────────────────────────────────────────────────────────

UCLASS()
class FENIXRUNTIME_API UFenixSupabaseSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ── Configuración ────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fenix|Config")
    FString SupabaseUrl;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fenix|Config")
    FString SupabaseAnonKey;

    // ── Eventos Auth ─────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnLoginResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnRegisterResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnLogoutResult OnLogoutResult;

    // ── Eventos Data ─────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category="Fenix|Events")
    FOnStoryLoaded OnStoryLoaded;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Events")
    FOnStoriesListLoaded OnStoriesListLoaded;

    // ══ AUTH API ═════════════════════════════════════════════

    /**
     * Login con email + contraseña.
     * Dispara OnLoginResult cuando termina.
     */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Login(const FString& Email, const FString& Password);

    /**
     * Registro de nuevo usuario.
     * Dispara OnRegisterResult cuando termina.
     */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Register(const FString& Email, const FString& Password);

    /**
     * Cierra sesión (invalida el token en Supabase).
     * Dispara OnLogoutResult cuando termina.
     */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Logout();

    /**
     * Refresca el access token usando el refresh token guardado.
     * Se llama automáticamente antes de peticiones si el token caducó.
     */
    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void RefreshSession();

    // ── Consultas de estado ──────────────────────────────────

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    bool IsLoggedIn() const { return CurrentSession.IsValid(); }

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixSession& GetSession() const { return CurrentSession; }

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixUser& GetCurrentUser() const { return CurrentSession.User; }

    // ══ DATA API ═════════════════════════════════════════════

    UFUNCTION(BlueprintCallable, Category="Fenix|API")
    void FetchStory(const FString& StoryUUID);

    UFUNCTION(BlueprintCallable, Category="Fenix|API")
    void FetchPublishedStories();

    UFUNCTION(BlueprintPure, Category="Fenix|Data")
    const FFenixStory& GetCurrentStory() const { return CurrentStory; }

    UFUNCTION(BlueprintPure, Category="Fenix|Data")
    bool HasStoryLoaded() const { return bStoryLoaded; }

    // ── USubsystem ────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    // Estado interno
    FFenixSession CurrentSession;
    FFenixStory   CurrentStory;
    bool          bStoryLoaded = false;

    // Persistencia de sesión entre sesiones de juego
    void SaveSessionToSlot();
    void LoadSessionFromSlot();

    // HTTP helpers
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(
        const FString& Endpoint,
        const FString& Method = TEXT("GET"),
        bool bUseAuthToken = false          // ← si true añade el Bearer JWT
    );

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeAuthRequest(
        const FString& Endpoint,
        const FString& Method,
        const FString& JsonBody
    );

    // Parseo
    bool ParseSession(const FString& JsonStr, FFenixSession& OutSession);
    bool ParseStory(const FString& JsonStr, FFenixStory& OutStory);
    bool ParseScene(TSharedPtr<FJsonObject> Obj, FFenixScene& Out);
    bool ParseItem(TSharedPtr<FJsonObject> Obj, FFenixItem& Out);
    bool ParseNpc(TSharedPtr<FJsonObject> Obj, FFenixNpc& Out);
    bool ParseQuest(TSharedPtr<FJsonObject> Obj, FFenixQuest& Out);

    // Callbacks HTTP
    void OnLoginResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnRegisterResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnLogoutResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnRefreshResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnFetchStoryResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnFetchStoriesResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    FString ExtractErrorMessage(const FString& JsonStr);

};