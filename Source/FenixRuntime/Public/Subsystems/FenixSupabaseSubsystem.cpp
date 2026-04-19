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

    Root->TryGetStringField(TEXT("access_token"),  OutSession.AccessToken);
    Root->TryGetStringField(TEXT("refresh_token"), OutSession.RefreshToken);
    Root->TryGetStringField(TEXT("token_type"),    OutSession.TokenType);
    double ExpiresIn = 0.0;
    if (Root->TryGetNumberField(TEXT("expires_in"), ExpiresIn))
        OutSession.ExpiresIn = (int32)ExpiresIn;

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

// ─────────────────────────────────────────────────────────────
// Helpers de parsing (privados, sin UObject)
// ─────────────────────────────────────────────────────────────
namespace
{
    FFenixVector3 ParseVector3(const TSharedPtr<FJsonObject>& Obj)
    {
        FFenixVector3 V;
        if (!Obj.IsValid()) return V;
        double Tmp;
        if (Obj->TryGetNumberField(TEXT("x"), Tmp)) V.X = (float)Tmp;
        if (Obj->TryGetNumberField(TEXT("y"), Tmp)) V.Y = (float)Tmp;
        if (Obj->TryGetNumberField(TEXT("z"), Tmp)) V.Z = (float)Tmp;
        return V;
    }

    FFenixRotation ParseRotation(const TSharedPtr<FJsonObject>& Obj)
    {
        FFenixRotation R;
        if (!Obj.IsValid()) return R;
        double Tmp;
        if (Obj->TryGetNumberField(TEXT("pitch"), Tmp)) R.Pitch = (float)Tmp;
        if (Obj->TryGetNumberField(TEXT("yaw"),   Tmp)) R.Yaw   = (float)Tmp;
        if (Obj->TryGetNumberField(TEXT("roll"),  Tmp)) R.Roll  = (float)Tmp;
        return R;
    }

    FFenixPlacement ParsePlacement(const TSharedPtr<FJsonObject>& Obj)
    {
        FFenixPlacement P;
        if (!Obj.IsValid()) return P;
        const TSharedPtr<FJsonObject>* Sub;
        if (Obj->TryGetObjectField(TEXT("location"), Sub)) P.Location = ParseVector3(*Sub);
        if (Obj->TryGetObjectField(TEXT("rotation"), Sub)) P.Rotation = ParseRotation(*Sub);
        if (Obj->TryGetObjectField(TEXT("scale"),    Sub)) P.Scale    = ParseVector3(*Sub);
        return P;
    }
}

// ─────────────────────────────────────────────────────────────
// ParseItem
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseItem(TSharedPtr<FJsonObject> Obj, FFenixItem& Out)
{
    if (!Obj.IsValid()) return false;

    Obj->TryGetStringField(TEXT("uuid"), Out.UUID);
    Obj->TryGetStringField(TEXT("type"), Out.Type);

    const TSharedPtr<FJsonObject>* Sub;
    if (Obj->TryGetObjectField(TEXT("placement"), Sub))
        Out.Placement = ParsePlacement(*Sub);

    if (Obj->TryGetObjectField(TEXT("params"), Sub))
    {
        (*Sub)->TryGetStringField(TEXT("travel_to"), Out.Params.TravelTo);
        (*Sub)->TryGetBoolField(TEXT("is_locked"),   Out.Params.bIsLocked);
        double H = 0.0;
        if ((*Sub)->TryGetNumberField(TEXT("hungry"), H))
            Out.Params.Hungry = (int32)H;
    }

    return !Out.UUID.IsEmpty();
}

// ─────────────────────────────────────────────────────────────
// ParseNpc
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseNpc(TSharedPtr<FJsonObject> Obj, FFenixNpc& Out)
{
    if (!Obj.IsValid()) return false;

    Obj->TryGetStringField(TEXT("uuid"),          Out.UUID);
    Obj->TryGetStringField(TEXT("name"),          Out.Name);
    Obj->TryGetStringField(TEXT("dialogue_uuid"), Out.DialogueUUID);
    Obj->TryGetStringField(TEXT("routine_uuid"),  Out.RoutineUUID);

    return !Out.UUID.IsEmpty();
}

// ─────────────────────────────────────────────────────────────
// ParseQuest
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseQuest(TSharedPtr<FJsonObject> Obj, FFenixQuest& Out)
{
    if (!Obj.IsValid()) return false;

    Obj->TryGetStringField(TEXT("uuid"),        Out.UUID);
    Obj->TryGetStringField(TEXT("name"),        Out.Name);
    Obj->TryGetStringField(TEXT("description"), Out.Description);
    double Ord = 0.0;
    if (Obj->TryGetNumberField(TEXT("order"), Ord)) Out.Order = (int32)Ord;

    const TArray<TSharedPtr<FJsonValue>>* ObjArr;
    if (Obj->TryGetArrayField(TEXT("objectives"), ObjArr))
    {
        for (const auto& Val : *ObjArr)
        {
            const TSharedPtr<FJsonObject>* ObjObj;
            if (!Val->TryGetObject(ObjObj)) continue;

            FFenixObjective Objective;
            (*ObjObj)->TryGetStringField(TEXT("uuid"),   Objective.UUID);
            (*ObjObj)->TryGetStringField(TEXT("type"),   Objective.Type);
            (*ObjObj)->TryGetStringField(TEXT("target"), Objective.Target);
            (*ObjObj)->TryGetStringField(TEXT("item"),   Objective.Item);
            double N = 0.0;
            if ((*ObjObj)->TryGetNumberField(TEXT("order"),  N)) Objective.Order  = (int32)N;
            if ((*ObjObj)->TryGetNumberField(TEXT("amount"), N)) Objective.Amount = (int32)N;
            Out.Objectives.Add(Objective);
        }
    }

    return !Out.UUID.IsEmpty();
}

// ─────────────────────────────────────────────────────────────
// ParseScene
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseScene(TSharedPtr<FJsonObject> Obj, FFenixScene& Out)
{
    if (!Obj.IsValid()) return false;

    Obj->TryGetStringField(TEXT("uuid"), Out.UUID);
    Obj->TryGetStringField(TEXT("name"), Out.Name);
    double W = 5.0, D = 5.0;
    if (Obj->TryGetNumberField(TEXT("width"), W)) Out.Width = (int32)W;
    if (Obj->TryGetNumberField(TEXT("depth"), D)) Out.Depth = (int32)D;

    const TSharedPtr<FJsonObject>* Sub;
    if (Obj->TryGetObjectField(TEXT("camera"), Sub)) Out.Camera = ParsePlacement(*Sub);
    if (Obj->TryGetObjectField(TEXT("player"), Sub)) Out.Player = ParsePlacement(*Sub);

    const TArray<TSharedPtr<FJsonValue>>* Arr;
    if (Obj->TryGetArrayField(TEXT("items"), Arr))
    {
        for (const auto& Val : *Arr)
        {
            const TSharedPtr<FJsonObject>* ItemObj;
            if (!Val->TryGetObject(ItemObj)) continue;
            FFenixItem Item;
            if (ParseItem(*ItemObj, Item)) Out.Items.Add(Item);
        }
    }

    if (Obj->TryGetArrayField(TEXT("scene_npcs"), Arr))
    {
        for (const auto& Val : *Arr)
        {
            const TSharedPtr<FJsonObject>* NpcObj;
            if (!Val->TryGetObject(NpcObj)) continue;
            FFenixNpcPlacement NpcPlacement;
            (*NpcObj)->TryGetStringField(TEXT("uuid"), NpcPlacement.UUID);
            if ((*NpcObj)->TryGetObjectField(TEXT("placement"), Sub))
                NpcPlacement.Placement = ParsePlacement(*Sub);
            Out.Npcs.Add(NpcPlacement);
        }
    }

    return !Out.UUID.IsEmpty();
}

// ─────────────────────────────────────────────────────────────
// ParseStory — recibe el array que devuelve PostgREST
// ─────────────────────────────────────────────────────────────
bool UFenixSupabaseSubsystem::ParseStory(const FString& JsonStr, FFenixStory& OutStory)
{
    TArray<TSharedPtr<FJsonValue>> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Root) || Root.Num() == 0)
        return false;

    const TSharedPtr<FJsonObject>* StoryObj;
    if (!Root[0]->TryGetObject(StoryObj)) return false;
    const TSharedPtr<FJsonObject>& S = *StoryObj;

    S->TryGetStringField(TEXT("uuid"),        OutStory.UUID);
    S->TryGetStringField(TEXT("name"),        OutStory.Name);
    S->TryGetStringField(TEXT("description"), OutStory.Description);
    S->TryGetStringField(TEXT("author"),      OutStory.Author);
    S->TryGetStringField(TEXT("version"),     OutStory.Version);
    S->TryGetStringField(TEXT("status"),      OutStory.Status);
    S->TryGetStringField(TEXT("start_scene"), OutStory.StartScene);

    const TArray<TSharedPtr<FJsonValue>>* Arr;
    if (S->TryGetArrayField(TEXT("scenes"), Arr))
    {
        for (const auto& Val : *Arr)
        {
            const TSharedPtr<FJsonObject>* Obj;
            if (!Val->TryGetObject(Obj)) continue;
            FFenixScene Scene;
            if (ParseScene(*Obj, Scene)) OutStory.Scenes.Add(Scene);
        }
    }
    if (S->TryGetArrayField(TEXT("npcs"), Arr))
    {
        for (const auto& Val : *Arr)
        {
            const TSharedPtr<FJsonObject>* Obj;
            if (!Val->TryGetObject(Obj)) continue;
            FFenixNpc Npc;
            if (ParseNpc(*Obj, Npc)) OutStory.Npcs.Add(Npc);
        }
    }
    if (S->TryGetArrayField(TEXT("quests"), Arr))
    {
        for (const auto& Val : *Arr)
        {
            const TSharedPtr<FJsonObject>* Obj;
            if (!Val->TryGetObject(Obj)) continue;
            FFenixQuest Quest;
            if (ParseQuest(*Obj, Quest)) OutStory.Quests.Add(Quest);
        }
    }

    return !OutStory.UUID.IsEmpty();
}

// ─────────────────────────────────────────────────────────────
// OnFetchStoryResponse
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::OnFetchStoryResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory: sin conexión"));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    int32 Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    if (Code != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchStory fallido %d: %s"),
               Code, *ExtractErrorMessage(Body));
        OnStoryLoaded.Broadcast(false, FFenixStory{});
        return;
    }

    FFenixStory Story;
    if (!ParseStory(Body, Story))
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
// OnFetchStoriesResponse
// ─────────────────────────────────────────────────────────────
void UFenixSupabaseSubsystem::OnFetchStoriesResponse(
    FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
{
    if (!bOk || !Res.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchPublishedStories: sin conexión"));
        OnStoriesListLoaded.Broadcast(false, TArray<FFenixStory>{});
        return;
    }

    int32 Code = Res->GetResponseCode();
    FString Body = Res->GetContentAsString();

    if (Code != 200)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchPublishedStories fallido %d: %s"),
               Code, *ExtractErrorMessage(Body));
        OnStoriesListLoaded.Broadcast(false, TArray<FFenixStory>{});
        return;
    }

    TArray<TSharedPtr<FJsonValue>> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Root))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Fenix] FetchPublishedStories: JSON inválido"));
        OnStoriesListLoaded.Broadcast(false, TArray<FFenixStory>{});
        return;
    }

    TArray<FFenixStory> Stories;
    for (const auto& Val : Root)
    {
        const TSharedPtr<FJsonObject>* StoryObj;
        if (!Val->TryGetObject(StoryObj)) continue;
        FFenixStory Story;
        (*StoryObj)->TryGetStringField(TEXT("uuid"),        Story.UUID);
        (*StoryObj)->TryGetStringField(TEXT("name"),        Story.Name);
        (*StoryObj)->TryGetStringField(TEXT("description"), Story.Description);
        (*StoryObj)->TryGetStringField(TEXT("status"),      Story.Status);
        if (!Story.UUID.IsEmpty()) Stories.Add(Story);
    }

    UE_LOG(LogTemp, Log, TEXT("[Fenix] %d historias publicadas cargadas"), Stories.Num());
    OnStoriesListLoaded.Broadcast(true, Stories);
}

// ─────────────────────────────────────────────────────────────
// Persistencia de sesión (SaveGame simple)
// ─────────────────────────────────────────────────────────────
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