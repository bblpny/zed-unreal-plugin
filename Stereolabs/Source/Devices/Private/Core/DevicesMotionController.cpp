//======= Copyright (c) Stereolabs Corporation, All rights reserved. ===============

#include "DevicesPrivatePCH.h"
#include "Devices/Public/Core/DevicesMotionController.h"
#include "Stereolabs/Public/Core/StereolabsBaseTypes.h"
#include "ZED/Public/Utilities/ZEDFunctionLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "ZED/Public/Core/ZEDInitializer.h"

#include <sl_mr_core/latency.hpp>

ADevicesMotionController::ADevicesMotionController()
	:
	LatencyTime(25)
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>("RootComponent");

	MotionController = CreateDefaultSubobject<UMotionControllerComponent>("MotionController");
	MotionController->bDisableLowLatencyUpdate = true;
	MotionController->SetCanEverAffectNavigation(false);
	MotionController->SetupAttachment(RootComponent);

	AddTickPrerequisiteComponent(MotionController);

	TransformBuffer.Reserve(LatencyTime);

	// Get Zed initializer object
	TArray<AActor*> ZedInitializer;
	UGameplayStatics::GetAllActorsOfClass(this, AZEDInitializer::StaticClass(), ZedInitializer);
	if (ZedInitializer.Num() >= 1)
	{
		AZEDInitializer* Initializer = static_cast<AZEDInitializer*>(ZedInitializer[0]);
		if(Initializer->TrackingParameters.TrackingType != ETrackingType::TrT_ZED)
			bHmdTranslationUsed = true;
	}
}

void ADevicesMotionController::BeginPlay()
{
	Super::BeginPlay();

	GSlCameraProxy->OnCameraClosed.AddDynamic(this, &ADevicesMotionController::Stop);
}

void ADevicesMotionController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (GSlCameraProxy)
	{
		GSlCameraProxy->OnCameraClosed.RemoveDynamic(this, &ADevicesMotionController::Stop);
	}
}

void ADevicesMotionController::Tick(float DeltaTime)
{
	if (!isDeviceInitialized)
	{
		AZEDCamera* ZedCameraActor = UZEDFunctionLibrary::GetCameraActor(this);
		if (ZedCameraActor)
		{
			Start();
		}
	}

	if (isDeviceInitialized)
	{
		FTransform Transform;
		if (GetTransform(Transform))
		{
			SetActorTransform(Transform);
		}
	}

	Super::Tick(DeltaTime);
}

void ADevicesMotionController::Start()
{
	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		return;
	}

	UZEDFunctionLibrary::GetCameraActor(this)->OnCameraActorInitialized.RemoveDynamic(this, &ADevicesMotionController::Start);
	
	UpdateTransformBuffer();

	GetWorldTimerManager().SetTimer(TimerHandle, this, &ADevicesMotionController::UpdateTransformBuffer, 0.001f, true);

	isDeviceInitialized = true;
}

void ADevicesMotionController::Stop()
{
	GetWorldTimerManager().ClearTimer(TimerHandle);
	isDeviceInitialized = false;
}

void ADevicesMotionController::ModifyLatencyTime(const int& NewLatencyInMs)
{
	LatencyTime = NewLatencyInMs;
	TransformBuffer.Empty();
	TransformBuffer.Reserve(LatencyTime);
}

int ADevicesMotionController::GetModifiedLatencyTime()
{
	return LatencyTime;
}

FTransform ADevicesMotionController::GetDelayedTransform()
{
	return TransformBuffer[0];
}

void ADevicesMotionController::UpdateTransformBuffer()
{
	check(MotionController);

	if (TransformBuffer.Num() + 1 > LatencyTime)
	{
		TransformBuffer.RemoveAt(0, 1, false);
	}

	TransformBuffer.Add(MotionController->GetRelativeTransform());
}

bool ADevicesMotionController::GetTransform(FTransform& Transform)
{
	FZEDTrackingData TrackingData = UZEDFunctionLibrary::GetTrackingData();
	FTransform DelayedTransform = GetDelayedTransform();
	sl::Transform SlLatencyTransform;

	bool bTransform = sl::mr::latencyCorrectorGetTransform(GSlCameraProxy->GetCamera().getTimestamp(sl::TIME_REFERENCE::TIME_REFERENCE_CURRENT) - sl::timeStamp(LatencyTime * 1000000), SlLatencyTransform, false);
	if (!bTransform)
	{
		return false;
	}

	if (bHmdTranslationUsed)
	{
		Transform = DelayedTransform;
	}
	else
	{
		Transform = (DelayedTransform * sl::unreal::ToUnrealType(SlLatencyTransform).Inverse()) * TrackingData.OffsetZedWorldTransform;
	}	

	return true;
}