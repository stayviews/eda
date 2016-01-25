// Fill out your copyright notice in the Description page of Project Settings.

#include "EDA.h"
#include "Weapon.h"


// Sets default values
AWeapon::AWeapon()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	
	weaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	
	bReplicates = true;
	bNetUseOwnerRelevancy = true;


	CurrentAmmo = 30;
	CurrentAmmoInClip = 10;
	BurstCounter = 0;
	LastFireTime = 0.0f;
}


// Called when the game starts or when spawned
void AWeapon::BeginPlay()
{
	Super::BeginPlay();
	
}

USkeletalMeshComponent* AWeapon::GetWeaponMesh() const
{
	if (weaponMesh)
	{
		return  weaponMesh;
	}
}

void AWeapon::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeapon, MyPawn);

	DOREPLIFETIME_CONDITION(AWeapon, CurrentAmmo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWeapon, CurrentAmmoInClip, COND_OwnerOnly);

	DOREPLIFETIME_CONDITION(AWeapon, BurstCounter, COND_SkipOwner);
}
void AWeapon::DetermineWeaponState(EWeaponState NewState)
{
	switch (NewState)
	{
	case Idle:
		SetWeaponState(EWeaponState::Idle);
		break;
	case Firing:
		SetWeaponState(EWeaponState::Firing);
		break;
	case Reloading:
		break;
	case Equipping:
		break;
	default:
		break;
	}
}

/************************************************************************/
/* read weapondata                                                                     */
/************************************************************************/

EWeaponState AWeapon::GetCurrentState() const
{
	return CurrentState;
}

int32 AWeapon::GetCurrentAmmo() const
{
	return CurrentAmmo;
}

int32 AWeapon::GetCurrentAmmoInClip() const
{
	return CurrentAmmoInClip;
}

int32 AWeapon::GetAmmoPerClip() const
{
	
	return WeaponConfig.AmmoPerClip;
}

int32 AWeapon::GetMaxAmmo() const
{
	return WeaponConfig.MaxAmmo;
}



void AWeapon::OnRep_MyPawn()
{

}




/************************************************************************/
/* Equip owner                                                          */
/************************************************************************/
void AWeapon::SetOwningPawn(class AEDACharacter* NewOwner)
{
	if (MyPawn != NewOwner)
	{
		Instigator = NewOwner;
		MyPawn = NewOwner;
		// net owner for RPC calls
		SetOwner(NewOwner);
	}
}
void AWeapon::OnEnterInventory(class AEDACharacter* NewOwner)
{
	SetOwningPawn(NewOwner);
}


void AWeapon::OnEquip(bool bPlayAnimation)
{
	bPendingEquip = true;
	DetermineWeaponState(EWeaponState::Equipping);
	if (bPlayAnimation)
	{
		float Duration = PlayWeaponAnimation(EquipAnim);
		if (Duration <= 0.0f)
		{
			// Failsafe in case animation is missing
			Duration = NoEquipAnimDuration;
		}
		EquipStartedTime = GetWorld()->TimeSeconds;
		EquipDuration = Duration;

		GetWorldTimerManager().SetTimer(EquipFinishedTimerHandle, this, &AWeapon::OnEquipFinished, Duration, false);
	}
	else
	{
		/* Immediately finish equipping */
		OnEquipFinished();
	}
// 	if (MyPawn && MyPawn->IsLocallyControlled())
// 	{
// 		PlayWeaponSound(EquipSound);
// 	}
}

void AWeapon::OnEquipFinished()
{
	DetermineWeaponState(EWeaponState::Idle);
}

void AWeapon::OnUnEquip()
{

}

/************************************************************************/
/* targeting                                                                     */
/************************************************************************/
FVector AWeapon::GetAdjustedAim() const
{
	AEDAPlayerController* const PlayerController = Instigator ? Cast<AEDAPlayerController>(Instigator->Controller) : NULL;
	FVector FinalAim = FVector::ZeroVector;
	// If we have a player controller use it for the aim
	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}

	return FinalAim;
}

FVector AWeapon::GetCameraDamageStartLocation(const FVector& AimDir) const
{
	AEDAPlayerController* PC = MyPawn ? Cast<AEDAPlayerController>(MyPawn->Controller) : NULL;
	AEDAPlayerController* AIPC = MyPawn ? Cast<AEDAPlayerController>(MyPawn->Controller) : NULL;
	FVector OutStartTrace = FVector::ZeroVector;

	if (PC)
	{
		// use player's camera
		FRotator UnusedRot;
		PC->GetPlayerViewPoint(OutStartTrace, UnusedRot);

		// Adjust trace so there is nothing blocking the ray between the camera and the pawn, and calculate distance from adjusted start
		OutStartTrace = OutStartTrace + AimDir * ((Instigator->GetActorLocation() - OutStartTrace) | AimDir);
	}
	else if (AIPC)
	{
		OutStartTrace = GetMuzzleLocation();
	}

	return OutStartTrace;
}

/************************************************************************/
/* Fire                                                                     */
/************************************************************************/

bool AWeapon::CanFire() const
{
	bool bCanFire = MyPawn && MyPawn->CanFire();
	bool bStateOKToFire = ((CurrentState == EWeaponState::Idle) || (CurrentState == EWeaponState::Firing));
	return ((bCanFire == true) && (bStateOKToFire == true));
}

void AWeapon::StartFire()
{
	if (Role < ROLE_Authority)
	{
		ServerStartFire();
	}
	DetermineWeaponState(EWeaponState::Firing);
	UE_LOG(LogTemp, Warning, TEXT("StartFire"));
}
void AWeapon::StopFire()
{
	if (Role < ROLE_Authority)
	{
		ServerStopFire();
	}

	DetermineWeaponState(EWeaponState::Idle);
	UE_LOG(LogTemp, Warning, TEXT("AWeapon::StopFire()"));
}



void AWeapon::SetWeaponState(EWeaponState NewState)
{
	const EWeaponState PrevState = CurrentState;

	if (PrevState == EWeaponState::Firing && NewState != EWeaponState::Firing)
	{
		OnBurstFinished();
	}

	CurrentState = NewState;

	if (PrevState != EWeaponState::Firing && NewState == EWeaponState::Firing)
	{
		OnBurstStarted();
	}
	
}

void AWeapon::OnBurstStarted()
{
	// start firing, can be delayed to satisfy TimeBetweenShots
	const float GameTime = GetWorld()->GetTimeSeconds();
	if (LastFireTime > 0 && WeaponConfig.TimeBetweenShots > 0.0f &&
		LastFireTime + WeaponConfig.TimeBetweenShots > GameTime)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &AWeapon::HandleFiring, LastFireTime + WeaponConfig.TimeBetweenShots - GameTime, false);
	}
	else
	{
		HandleFiring();
	}
}

void AWeapon::OnBurstFinished()
{
	// stop firing FX on remote clients
	BurstCounter = 0;

	// stop firing FX locally, unless it's a dedicated server
	if (GetNetMode() != NM_DedicatedServer)
	{
		StopSimulatingWeaponFire();
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_HandleFiring);
	bRefiring = false;
}



void AWeapon::HandleFiring()
{
	if ((CurrentAmmoInClip > 0) && CanFire())
	{
		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (MyPawn && MyPawn->IsLocallyControlled())
		{
			FireWeapon();

			UseAmmo();

			// update firing FX on remote clients if function was called on server
			BurstCounter++;
		}
	}
	if (MyPawn && MyPawn->IsLocallyControlled())
	{
		// local client will notify server
		if (Role < ROLE_Authority)
		{
			ServerHandleFiring();
		}

		// reload after firing last round
// 		if (CurrentAmmoInClip <= 0 && CanReload())
// 		{
// 			StartReload();
// 		}

		// setup refire timer
		bRefiring = (CurrentState == EWeaponState::Firing && WeaponConfig.TimeBetweenShots > 0.0f);
		if (bRefiring)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &AWeapon::HandleFiring, WeaponConfig.TimeBetweenShots, false);
		}
	}
	LastFireTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Warning, TEXT("HandleFiring"));
}
/************************************************************************/
/* ammo                                                                     */
/************************************************************************/
void AWeapon::UseAmmo()
{

}


/************************************************************************/
/* effect                                                                     */
/************************************************************************/
FVector AWeapon::GetMuzzleDirection() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketRotation(MuzzleAttachPoint).Vector();
}

FVector AWeapon::GetMuzzleLocation() const
{
	USkeletalMeshComponent* UseMesh = GetWeaponMesh();
	return UseMesh->GetSocketLocation(MuzzleAttachPoint);
}



void AWeapon::SimulateWeaponFire()
{
	if (bLoopedMuzzleFX)
	{
		if (MuzzlePSC != NULL)
		{
			MuzzlePSC->DeactivateSystem();
			MuzzlePSC = NULL;
		}
		if (MuzzlePSCSecondary != NULL)
		{
			MuzzlePSCSecondary->DeactivateSystem();
			MuzzlePSCSecondary = NULL;
		}
	}

	if (bLoopedFireAnim && bPlayingFireAnim)
	{
		StopWeaponAnimation(FireAnim);
		bPlayingFireAnim = false;
	}

	if (FireAC)
	{
		FireAC->FadeOut(0.1f, 0.0f);
		FireAC = NULL;

	}
}

void AWeapon::StopSimulatingWeaponFire()
{

}
float AWeapon::PlayWeaponAnimation(const FWeaponAnim& Animation)
{
	float Duration = 0.0f;
	if (MyPawn)
	{
		UAnimMontage* UseAnim = MyPawn->IsFirstPerson() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			Duration = MyPawn->PlayAnimMontage(UseAnim);
		}
	}

	return Duration;
}

void AWeapon::StopWeaponAnimation(const FWeaponAnim& Animation)
{
	if (MyPawn)
	{
		UAnimMontage* UseAnim = MyPawn->IsFirstPerson() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			MyPawn->StopAnimMontage(UseAnim);
		}
	}
}
/************************************************************************/
/* net working                                                            */
/************************************************************************/
void AWeapon::ServerStartFire_Implementation()
{
	StartFire();
}

bool AWeapon::ServerStartFire_Validate()
{
	return true;
}
void AWeapon::ServerStopFire_Implementation()
{
	StopFire();
}

bool AWeapon::ServerStopFire_Validate()
{
	return true;
}
void AWeapon::OnRep_BurstCounter()
{
	if (BurstCounter > 0)
	{
		SimulateWeaponFire();
	}
}
void AWeapon::ServerHandleFiring_Implementation()
{
	const bool bShouldUpdateAmmo = (CurrentAmmoInClip > 0 && CanFire());

	HandleFiring();

	if (bShouldUpdateAmmo)
	{
		// update ammo
		UseAmmo();

		// update firing FX on remote clients
		BurstCounter++;
	}
	UE_LOG(LogTemp, Warning, TEXT("HandleFiring"));
}

bool AWeapon::ServerHandleFiring_Validate()
{
	return true;
}