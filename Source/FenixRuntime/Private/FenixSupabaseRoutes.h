#pragma once

#include "CoreMinimal.h"

/**
 * FenixSupabaseRoutes
 *
 * Centraliza todos los endpoints de la API de Supabase usados por FenixRuntime.
 * No es un UObject — es una struct de utilidad estática pura, sin overhead de reflexión.
 *
 * Uso:
 *   FenixSupabaseRoutes::Auth::Login()
 *   FenixSupabaseRoutes::Data::FetchStory(StoryUUID)
 *
 * Si cambias la versión de la API de Supabase o el schema de tablas,
 * este es el único archivo que necesitas tocar.
 */
struct FENIXRUNTIME_API FenixSupabaseRoutes
{
    // ══ AUTH ══════════════════════════════════════════════════
    // Endpoints de autenticación — /auth/v1/
    // No requieren JWT, usan solo el apikey (anon key).
    // ─────────────────────────────────────────────────────────

    struct Auth
    {
        /** POST — Login con email + contraseña. Body: { email, password } */
        static FString Login()
        {
            return TEXT("/auth/v1/token?grant_type=password");
        }

        /** POST — Registro de nuevo usuario. Body: { email, password } */
        static FString Register()
        {
            return TEXT("/auth/v1/signup");
        }

        /**
         * POST — Cierra sesión e invalida el token en Supabase.
         * Requiere header Authorization: Bearer <access_token>
         */
        static FString Logout()
        {
            return TEXT("/auth/v1/logout");
        }

        /** POST — Refresca el access token. Body: { refresh_token } */
        static FString RefreshToken()
        {
            return TEXT("/auth/v1/token?grant_type=refresh_token");
        }
    };

    // ══ DATA ══════════════════════════════════════════════════
    // Endpoints REST de PostgREST — /rest/v1/
    // Requieren header apikey. Los que usan RLS también necesitan
    // Authorization: Bearer <access_token>.
    // ─────────────────────────────────────────────────────────

    struct Data
    {
        /**
         * GET — Carga una historia completa por UUID con todas sus relaciones.
         * Requiere sesión activa (RLS).
         *
         * Relaciones incluidas:
         *   scenes → items, scene_npcs
         *   npcs
         *   quests → objectives
         */
        static FString FetchStory(const FString& StoryUUID)
        {
            return FString::Printf(
                TEXT("/rest/v1/stories?uuid=eq.%s")
                TEXT("&select=*")
                TEXT(",scenes(*,items(*),scene_npcs(*))") 
                TEXT(",npcs(*)")
                TEXT(",quests(*,objectives(*))"),
                *StoryUUID
            );
        }

        /**
         * GET — Lista todas las historias publicadas (status=PUBLISH).
         * No requiere sesión (datos públicos).
         */
        static FString FetchPublishedStories()
        {
            return TEXT("/rest/v1/stories")
                   TEXT("?status=eq.PUBLISH")
                   TEXT("&select=uuid,name,description,status,updated_at");
        }

        /**
         * GET — Lista las historias del usuario autenticado.
         * Requiere sesión activa (RLS filtra por JWT automáticamente).
         */
        static FString FetchMyStories()
        {
            return TEXT("/rest/v1/stories")
                   TEXT("?select=uuid,name,description,status,updated_at")
                   TEXT("&order=updated_at.desc");
        }
    };
};
