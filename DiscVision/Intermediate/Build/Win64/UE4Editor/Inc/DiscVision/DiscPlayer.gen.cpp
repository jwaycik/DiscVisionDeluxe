// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "DiscVision/DiscPlayer.h"
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4883)
#endif
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeDiscPlayer() {}
// Cross Module References
	DISCVISION_API UClass* Z_Construct_UClass_ADiscPlayer_NoRegister();
	DISCVISION_API UClass* Z_Construct_UClass_ADiscPlayer();
	ENGINE_API UClass* Z_Construct_UClass_ACharacter();
	UPackage* Z_Construct_UPackage__Script_DiscVision();
// End Cross Module References
	void ADiscPlayer::StaticRegisterNativesADiscPlayer()
	{
	}
	UClass* Z_Construct_UClass_ADiscPlayer_NoRegister()
	{
		return ADiscPlayer::StaticClass();
	}
	struct Z_Construct_UClass_ADiscPlayer_Statics
	{
		static UObject* (*const DependentSingletons[])();
#if WITH_METADATA
		static const UE4CodeGen_Private::FMetaDataPairParam Class_MetaDataParams[];
#endif
		static const FCppClassTypeInfoStatic StaticCppClassTypeInfo;
		static const UE4CodeGen_Private::FClassParams ClassParams;
	};
	UObject* (*const Z_Construct_UClass_ADiscPlayer_Statics::DependentSingletons[])() = {
		(UObject* (*)())Z_Construct_UClass_ACharacter,
		(UObject* (*)())Z_Construct_UPackage__Script_DiscVision,
	};
#if WITH_METADATA
	const UE4CodeGen_Private::FMetaDataPairParam Z_Construct_UClass_ADiscPlayer_Statics::Class_MetaDataParams[] = {
		{ "HideCategories", "Navigation" },
		{ "IncludePath", "DiscPlayer.h" },
		{ "ModuleRelativePath", "DiscPlayer.h" },
	};
#endif
	const FCppClassTypeInfoStatic Z_Construct_UClass_ADiscPlayer_Statics::StaticCppClassTypeInfo = {
		TCppClassTypeTraits<ADiscPlayer>::IsAbstract,
	};
	const UE4CodeGen_Private::FClassParams Z_Construct_UClass_ADiscPlayer_Statics::ClassParams = {
		&ADiscPlayer::StaticClass,
		"Game",
		&StaticCppClassTypeInfo,
		DependentSingletons,
		nullptr,
		nullptr,
		nullptr,
		UE_ARRAY_COUNT(DependentSingletons),
		0,
		0,
		0,
		0x009000A4u,
		METADATA_PARAMS(Z_Construct_UClass_ADiscPlayer_Statics::Class_MetaDataParams, UE_ARRAY_COUNT(Z_Construct_UClass_ADiscPlayer_Statics::Class_MetaDataParams))
	};
	UClass* Z_Construct_UClass_ADiscPlayer()
	{
		static UClass* OuterClass = nullptr;
		if (!OuterClass)
		{
			UE4CodeGen_Private::ConstructUClass(OuterClass, Z_Construct_UClass_ADiscPlayer_Statics::ClassParams);
		}
		return OuterClass;
	}
	IMPLEMENT_CLASS(ADiscPlayer, 413729466);
	template<> DISCVISION_API UClass* StaticClass<ADiscPlayer>()
	{
		return ADiscPlayer::StaticClass();
	}
	static FCompiledInDefer Z_CompiledInDefer_UClass_ADiscPlayer(Z_Construct_UClass_ADiscPlayer, &ADiscPlayer::StaticClass, TEXT("/Script/DiscVision"), TEXT("ADiscPlayer"), false, nullptr, nullptr, nullptr);
	DEFINE_VTABLE_PTR_HELPER_CTOR(ADiscPlayer);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#ifdef _MSC_VER
#pragma warning (pop)
#endif
