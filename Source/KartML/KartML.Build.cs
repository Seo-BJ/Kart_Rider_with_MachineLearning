// Copyright Epic Games, Inc. All Rights Reserved.

using System.Runtime.InteropServices;
using UnrealBuildTool;

public class KartML : ModuleRules
{
	public KartML(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ChaosVehicles", "PhysicsCore", "NNE" });
	}
}