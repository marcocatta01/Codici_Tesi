// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class PROXSIMA : ModuleRules
{
	private string ThirdPartyPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "ThirdParty")); }
	}

	private string FMIPath
	{
		get { return Path.Combine(ThirdPartyPath, "FMI"); }
	}
	private string rapidJsonPath
	{
		get { return Path.Combine(ThirdPartyPath, "rapidjson"); }
	}
	public PROXSIMA(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = true;

		// Add the include path
		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(FMIPath, "include"),
				Path.Combine(rapidJsonPath, "include"),
				"/usr/include"
			}
		);

		// Add defines needed by RapidJSON
		PublicDefinitions.Add("RAPIDJSON_HAS_STDSTRING=1");
		// Disable RapidJSON assertions in shipping builds
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			PublicDefinitions.Add("RAPIDJSON_ASSERT=");
		}

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"UMG",
			"zlib",
			"XmlParser",
			"Json",
			"JsonUtilities",
			"WebSockets",
			"UE_Assimp",
			"ProceduralMeshComponent",
			"ImageWrapper"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicSystemLibraryPaths.AddRange(new string[] { });
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicSystemLibraries.AddRange(new string[] {
			"dl",    // For dynamic loading
		});
		};

		// Uncomment if you are using Slate UI
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "DesktopPlatform" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
