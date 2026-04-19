#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FenixDeveloperSettings.generated.h"

/**
 * Configuración del plugin FenixRuntime.
 * Aparece en: Edit → Project Settings → Plugins → Fenix Runtime
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Fenix Runtime"))
class FENIXRUNTIME_API UFenixDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UFenixDeveloperSettings();

    // ── Acceso estático desde cualquier parte ─────────────────
    static const UFenixDeveloperSettings* Get()
    {
        return GetDefault<UFenixDeveloperSettings>();
    }

    // ── Supabase Connection ───────────────────────────────────

    UPROPERTY(Config, EditAnywhere, Category="Supabase",
        meta=(DisplayName="Project URL",
              ToolTip="URL de tu proyecto Supabase. Ej: https://xxxx.supabase.co"))
    FString SupabaseUrl;

    UPROPERTY(Config, EditAnywhere, Category="Supabase",
        meta=(DisplayName="Anon Key",
              ToolTip="Clave pública anon de tu proyecto Supabase (publishable key)."))
    FString SupabaseAnonKey;


    // ── Debug ─────────────────────────────────────────────────

    UPROPERTY(Config, EditAnywhere, Category="Debug",
        meta=(DisplayName="Verbose Logging",
              ToolTip="Activa logs detallados de todas las peticiones HTTP."))
    bool bVerboseLogging = false;

    // ── UDeveloperSettings ────────────────────────────────────
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};