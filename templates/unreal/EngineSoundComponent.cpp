#include "EngineSoundComponent.h"

#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"

UEngineSoundComponent::UEngineSoundComponent() { PrimaryComponentTick.bCanEverTick = true; }

void UEngineSoundComponent::BeginPlay()
{
    Super::BeginPlay();
    FString Text;
    const FString Path = FPaths::IsRelative (ProfilePath) ? FPaths::ProjectContentDir() / ProfilePath : ProfilePath;
    if (! FFileHelper::LoadFileToString (Text, *Path)) { UE_LOG (LogTemp, Error, TEXT ("EngineSound: cannot read %s"), *Path); return; }
    TSharedPtr<FJsonObject> Root;
    if (! FJsonSerializer::Deserialize (TJsonReaderFactory<>::Create (Text), Root) || ! Root.IsValid()) return;
    const TSharedPtr<FJsonObject>* Engine;
    if (Root->TryGetObjectField (TEXT ("engine"), Engine)) IdleRpm = (*Engine)->GetNumberField (TEXT ("idle_rpm"));
    const TSharedPtr<FJsonObject> SetsJ = Root->GetObjectField (TEXT ("sets"));
    for (const auto& KV : SetsJ->Values)
    {
        TArray<FRow>& Rows = Sets.Add (KV.Key);
        SetGain.Add (KV.Key, 1.0f);
        for (const auto& RV : KV.Value->AsObject()->GetArrayField (TEXT ("rows")))
        {
            const auto RJ = RV->AsObject();
            FRow Row;
            Row.State = RJ->GetStringField (TEXT ("state"));
            Row.Load = RJ->GetNumberField (TEXT ("load"));
            for (const auto& PV : RJ->GetArrayField (TEXT ("points")))
            {
                const auto PJ = PV->AsObject();
                const FString Asset = FPaths::GetBaseFilename (PJ->GetStringField (TEXT ("file")), false);
                USoundWave* Wave = LoadObject<USoundWave> (nullptr, *(SoundFolder / Asset + TEXT (".") + FPaths::GetBaseFilename (Asset)));
                if (! Wave) continue;
                Wave->bLooping = true;
                UAudioComponent* AC = UGameplayStatics::SpawnSoundAttached (Wave, GetOwner()->GetRootComponent());
                if (! AC) continue;
                AC->SetVolumeMultiplier (0.0f);
                FPoint P;
                P.Rpm = PJ->GetNumberField (TEXT ("rpm"));
                P.Audio = AC;
                Row.Points.Add (P);
            }
            Row.Points.Sort ([] (const FPoint& A, const FPoint& B) { return A.Rpm < B.Rpm; });
            if (Row.Points.Num() > 0) Rows.Add (Row);
        }
    }
}

void UEngineSoundComponent::SetInputs (float InRpm, float InThrottle, float InLoad, int32 InGear, float InSpeedKmh)
{
    Rpm = InRpm; Throttle = InThrottle; Load = InLoad; Gear = InGear; SpeedKmh = InSpeedKmh;
}

void UEngineSoundComponent::SetHybrid (bool bOn, float MotorTorqueFraction) { bEngineOn = bOn; MotorTorque = MotorTorqueFraction; }
void UEngineSoundComponent::Shift (int32 NewGear) { Gear = NewGear; Duck = 0.15f; }

void UEngineSoundComponent::AddRow (FRow& Row, float R, float W)
{
    auto& P = Row.Points;
    if (R <= P[0].Rpm) { P[0].T += W; return; }
    if (R >= P.Last().Rpm) { P.Last().T += W; return; }
    for (int32 i = 0; i + 1 < P.Num(); ++i)
        if (R >= P[i].Rpm && R <= P[i + 1].Rpm)
        {
            const float T = (R - P[i].Rpm) / FMath::Max (P[i + 1].Rpm - P[i].Rpm, 1e-4f);
            P[i].T += W * FMath::Cos (T * PI * 0.5f);
            P[i + 1].T += W * FMath::Sin (T * PI * 0.5f);
            return;
        }
}

void UEngineSoundComponent::RowWeights (TArray<FRow*>& Rows, float R, float L)
{
    if (Rows.Num() == 0) return;
    Rows.Sort ([] (const FRow& A, const FRow& B) { return A.Load < B.Load; });
    FRow* R0 = Rows[0];
    FRow* R1 = Rows[0];
    float U = 0;
    if (L >= Rows.Last()->Load) R0 = R1 = Rows.Last();
    else if (L > Rows[0]->Load)
        for (int32 i = 0; i + 1 < Rows.Num(); ++i)
            if (L >= Rows[i]->Load && L <= Rows[i + 1]->Load) { R0 = Rows[i]; R1 = Rows[i + 1]; U = (L - R0->Load) / FMath::Max (R1->Load - R0->Load, 1e-4f); break; }
    AddRow (*R0, R, R0 == R1 ? 1.0f : 1.0f - U);
    if (R0 != R1) AddRow (*R1, R, U);
}

void UEngineSoundComponent::TickComponent (float Dt, ELevelTick TickType, FActorComponentTickFunction* Fn)
{
    Super::TickComponent (Dt, TickType, Fn);
    const float K = FMath::Clamp (Dt / FMath::Max (Smoothing, 1e-3f), 0.0f, 1.0f);
    float DuckGain = 1.0f;
    if (Duck > 0) { Duck -= Dt; DuckGain = 0.5f; }
    const bool bHybrid = Sets.Contains (TEXT ("engine")) && Sets.Contains (TEXT ("motor"));
    for (auto& KV : Sets)
    {
        float Target = 1.0f, L = Load;
        if (bHybrid)
        {
            if (KV.Key == TEXT ("engine")) Target = bEngineOn ? 1.0f : 0.0f;
            else { Target = (SpeedKmh > 0.5f || FMath::Abs (MotorTorque) > 0.01f) ? FMath::Clamp (FMath::Abs (MotorTorque) * 2.0f + 0.2f, 0.0f, 1.0f) : 0.0f; L = MotorTorque; }
        }
        float& G = SetGain.FindOrAdd (KV.Key);
        G = FMath::Lerp (G, Target, K);
        TArray<FRow*> OnLoad, Other;
        for (FRow& Row : KV.Value)
        {
            for (FPoint& P : Row.Points) P.T = 0;
            (Row.State == TEXT ("on_load") ? OnLoad : Other).Add (&Row);
        }
        if (KV.Key == TEXT ("motor") && L < -0.01f && Other.Num() > 0) RowWeights (Other, Rpm, L);
        else
        {
            RowWeights (OnLoad, Rpm, FMath::Abs (L));
            if (Other.Num() > 0 && KV.Key == TEXT ("engine") && Throttle < 0.05f && Rpm > IdleRpm * 1.3f)
            {
                const float O = FMath::Clamp ((0.05f - Throttle) / 0.05f, 0.0f, 1.0f);
                for (FRow* Row : OnLoad) for (FPoint& P : Row->Points) P.T *= 1.0f - O;
                for (FRow* Row : Other) AddRow (*Row, Rpm, O);
            }
        }
        for (FRow& Row : KV.Value)
            for (FPoint& P : Row.Points)
            {
                P.W = FMath::Lerp (P.W, P.T, K);
                P.Audio->SetVolumeMultiplier (P.W * G * DuckGain * Volume);
                P.Audio->SetPitchMultiplier (FMath::Clamp (FMath::Max (Rpm, 1.0f) / P.Rpm, 0.5f, 2.0f));
            }
    }
}
