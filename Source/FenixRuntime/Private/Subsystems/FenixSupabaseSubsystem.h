#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Auth/FenixAuthService.h"       // ← sesión y auth
#include "Data/FenixStoryData.h"         // ← structs de historia
#include "FenixSupabaseSubsystem.generated.h"

// ─── Delegates de datos ───────────────────────────────────────

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnStoryLoaded,
    bool,               bSuccess,
    const FFenixStory&, Story
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnStoriesListLoaded,
    bool,                       bSuccess,
    const TArray<FFenixStory>&, Stories
);

// ─────────────────────────────────────────────────────────────

/**
 * UFenixSupabaseSubsystem
 *
 * Punto de entrada único desde Blueprints y código de juego.
 * Orquesta las operaciones delegando en clases especializadas:
 *
 *   Auth  → UFenixAuthService     (login, register, sesión)
 *   Parse → UFenixStoryParser     (deserialización JSON)
 *   URLs  → FenixSupabaseRoutes   (endpoints centralizados)
 *
 * Esta clase solo contiene:
 *   - Reenvío de eventos de auth (para que Blueprints solo necesiten
 *     escuchar un objeto)
 *   - Peticiones HTTP de datos (FetchStory, FetchStories)
 *   - Estado de la historia cargada en memoria
 */
UCLASS()
class FENIXRUNTIME_API UFenixSupabaseSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // ══ AUTH — delegados al servicio ══════════════════════════
    // Los Blueprints escuchan aquí; internamente reenvían desde AuthService.

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnLoginResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnRegisterResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnLogoutResult OnLogoutResult;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Auth")
    FOnAuthResult OnRefreshSessionResult;

    // ══ AUTH — API pública ════════════════════════════════════

    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Login(const FString& Email, const FString& Password);

    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Register(const FString& Email, const FString& Password);

    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void Logout();

    UFUNCTION(BlueprintCallable, Category="Fenix|Auth")
    void RefreshSession();

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    bool IsLoggedIn() const;

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixSession& GetSession() const;

    UFUNCTION(BlueprintPure, Category="Fenix|Auth")
    const FFenixUser& GetCurrentUser() const;

    // ══ DATA — eventos ════════════════════════════════════════

    UPROPERTY(BlueprintAssignable, Category="Fenix|Events")
    FOnStoryLoaded OnStoryLoaded;

    UPROPERTY(BlueprintAssignable, Category="Fenix|Events")
    FOnStoriesListLoaded OnStoriesListLoaded;

    // ══ DATA — API pública ════════════════════════════════════

    /** Carga una historia completa con todas sus relaciones. */
    UFUNCTION(BlueprintCallable, Category="Fenix|API")
    void FetchStory(const FString& StoryUUID);

    /** Lista todas las historias publicadas (no requiere sesión). */
    UFUNCTION(BlueprintCallable, Category="Fenix|API")
    void FetchPublishedStories();

    /** Lista las historias del usuario autenticado (requiere sesión). */
    UFUNCTION(BlueprintCallable, Category="Fenix|API")
    void FetchMyStories();

    UFUNCTION(BlueprintPure, Category="Fenix|Data")
    const FFenixStory& GetCurrentStory() const { return CurrentStory; }

    UFUNCTION(BlueprintPure, Category="Fenix|Data")
    bool HasStoryLoaded() const { return bStoryLoaded; }

    // ── USubsystem ────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:

    // ── Dependencias ──────────────────────────────────────────
    UPROPERTY()
    UFenixAuthService* AuthService = nullptr;

    // ── Estado de datos ───────────────────────────────────────
    FFenixStory CurrentStory;
    bool        bStoryLoaded = false;

    // ── HTTP helper (solo para peticiones de datos) ───────────
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeDataRequest(
        const FString& Endpoint,
        bool bUseAuthToken = false
    );

    // ── Callbacks HTTP de datos ───────────────────────────────
    void OnFetchStoryResponse  (FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);
    void OnFetchStoriesResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk);

    // ── Config (leída de DeveloperSettings) ───────────────────
    FString SupabaseUrl;
    FString SupabaseAnonKey;
};
