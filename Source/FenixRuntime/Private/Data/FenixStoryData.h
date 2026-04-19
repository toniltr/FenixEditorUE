#pragma once

#include "CoreMinimal.h"
#include "FenixStoryData.generated.h"

// ─── Placement ───────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixVector3
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float X = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Y = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Z = 0.f;
};

USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixRotation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Pitch = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Yaw   = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Roll  = 0.f;
};

USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixPlacement
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixVector3  Location;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixRotation Rotation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixVector3  Scale = {1,1,1};
};

// ─── Item ─────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixItemParams
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString TravelTo;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool    bIsLocked  = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Hungry     = 0;
};

USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixItem
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString       UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString       Type;   // BP_Door, BP_Burger…
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixPlacement Placement;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixItemParams Params;
};

// ─── NPC Placement (decorado en escena) ──────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixNpcPlacement
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString        UUID;  // ref a Story.npcs
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixPlacement Placement;
};

// ─── Scene ────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixScene
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Width = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Depth = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixPlacement Camera;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FFenixPlacement Player;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixItem>         Items;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixNpcPlacement> Npcs;
};

// ─── NPC ──────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixNpc
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DialogueUUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString RoutineUUID;
};

// ─── Quest Objective ──────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixObjective
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Order  = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Type;   // TALK_TO_NPC, COLLECT_ITEM…
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Target;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Item;   // solo DELIVER_ITEM
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Amount = 1;
};

// ─── Quest ────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixQuest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Order = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixObjective> Objectives;
};

// ─── Story (raíz) ─────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FENIXRUNTIME_API FFenixStory
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString UUID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Author;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Version;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Status;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString StartScene;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixScene> Scenes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixNpc>   Npcs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FFenixQuest> Quests;
};