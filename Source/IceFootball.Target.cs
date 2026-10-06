using UnrealBuildTool;

public class IceFootballTarget : TargetRules
{
	public IceFootballTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("IceFootball");
	}
}
