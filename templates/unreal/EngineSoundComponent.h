// Engine Sound Designer - runtime component for Unreal Engine 5 (C++).
// Reads engine_profile.json (exported next to the imported SoundWaves) and drives one
// UAudioComponent per loop: equal-power crossfade between rpm points, linear between load rows,
// pitch = rpm / loop rpm. Untested here (no Unreal installation on the build machine).
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "EngineSoundComponent.generated.h"

class UAudioComponent;
class USoundWave;

UCLASS (ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class UEngineSoundComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEngineSoundComponent();

    /// Absolute or project-relative path of engine_profile.json (Content/EngineSounds/<engine>/).
    UPROPERTY (EditAnywhere, Category = "Engine Sound") FString ProfilePath;
    /// Content path of the folder holding the imported SoundWaves, e.g. /Game/EngineSounds/MyEngine
    UPROPERTY (EditAnywhere, Category = "Engine Sound") FString SoundFolder;
    UPROPERTY (EditAnywhere, Category = "Engine Sound") float Volume = 1.0f;
    UPROPERTY (EditAnywhere, Category = "Engine Sound") float Smoothing = 0.05f;

    UFUNCTION (BlueprintCallable, Category = "Engine Sound") void SetInputs (float Rpm, float Throttle, float Load, int32 Gear, float SpeedKmh);
    UFUNCTION (BlueprintCallable, Category = "Engine Sound") void SetHybrid (bool bEngineOn, float MotorTorqueFraction);
    UFUNCTION (BlueprintCallable, Category = "Engine Sound") void Shift (int32 NewGear);

    virtual void BeginPlay() override;
    virtual void TickComponent (float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FPoint { float Rpm = 0; UAudioComponent* Audio = nullptr; float W = 0, T = 0; };
    struct FRow { FString State; float Load = 0; TArray<FPoint> Points; };
    TMap<FString, TArray<FRow>> Sets;
    TMap<FString, float> SetGain;
    float Rpm = 800, Throttle = 0, Load = 0, SpeedKmh = 0, MotorTorque = 0, IdleRpm = 800, Duck = 0;
    int32 Gear = 1;
    bool bEngineOn = true;
    static void AddRow (FRow& Row, float R, float W);
    static void RowWeights (TArray<FRow*>& Rows, float R, float L);
};
