// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Skald : ModuleRules
{
        public Skald(ReadOnlyTargetRules Target) : base(Target)
        {
                PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

                PublicDependencyModuleNames.AddRange(new string[]
                {
                        "Core",
                        "Engine",
                        "InputCore",
                        "UMG",
                        "Slate",
                        "SlateCore",
                        // Required for ENetworkFailure enum used in USkaldGameInstance
                        "NetCore"
                });

                PrivateDependencyModuleNames.AddRange(new string[]
                {
                        "CoreUObject",
                        "Engine",
                        "Slate",
                        "SlateCore",
                        "UMG",
                        "OnlineSubsystem",
                        "Landscape"
                });

                PrivateIncludePaths.AddRange(new string[]
                {
                        ModuleDirectory
                });

                if (Target.bBuildEditor)
                {
                        PrivateDependencyModuleNames.Add("UnrealEd");
                }

                // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
        }
}
