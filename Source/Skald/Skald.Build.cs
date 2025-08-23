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
                        "CoreUObject",
                        "Engine",
                        "InputCore",
                        "UMG",
                        "Slate",
                        "SlateCore"
                });

                PrivateDependencyModuleNames.AddRange(new string[]
                {
                        "Slate",
                        "SlateCore",
                        "UMG",
                        "OnlineSubsystem"
                });

                // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
        }
}
