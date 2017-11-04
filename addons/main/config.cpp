class CfgPatches
{
	class dedmen_arma_script_profiler
	{
		name = "Arma Script Profiler";
		units[] = {};
		weapons[] = {};
		requiredVersion = 1.76;
		requiredAddons[] = {"intercept_core"};
		author = "Dedmen";
		authors[] = {"Dedmen"};
		url = "https://github.com/dedmen/ArmaScriptProfiler";
		version = "1.0";
		versionStr = "1.0";
		versionAr[] = {1,0};
	};
};
class Intercept {
    class Dedmen {
        class ArmaScriptProfiler {
			certificate = "core";
            pluginName = "ArmaScriptProfiler";
        };
    };
};