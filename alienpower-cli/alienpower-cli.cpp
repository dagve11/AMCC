#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <PowrProf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "alienfan-SDK.h"

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace {

AlienFan_SDK::Control* acpi = NULL;

AlienFan_SDK::Control& Acpi() {
	return *acpi;
}

struct CommandArg {
	std::string name;
	std::string value;
	bool hasValue;
};

std::string ToLower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return (char)tolower(c); });
	return value;
}

bool ParseInt(const std::string& text, int* value) {
	if (text.empty())
		return false;

	char* end = NULL;
	long parsed = strtol(text.c_str(), &end, 0);
	if (!end || *end)
		return false;

	*value = (int)parsed;
	return true;
}

std::vector<int> ParseCsvInts(const std::string& text, bool* ok) {
	std::vector<int> values;
	size_t start = 0;
	*ok = true;

	while (start <= text.size()) {
		size_t pos = text.find(',', start);
		std::string part = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
		int parsed = 0;
		if (!ParseInt(part, &parsed)) {
			*ok = false;
			return values;
		}
		values.push_back(parsed);
		if (pos == std::string::npos)
			break;
		start = pos + 1;
	}

	return values;
}

CommandArg ParseCommand(const char* rawArg) {
	std::string arg = rawArg;
	size_t split = arg.find('=');
	if (split == std::string::npos)
		return { ToLower(arg), "", false };

	return { ToLower(arg.substr(0, split)), arg.substr(split + 1), true };
}

const char* CpuBoostName(int mode) {
	switch (mode) {
	case 0: return "disabled";
	case 1: return "enabled";
	case 2: return "aggressive";
	case 3: return "efficient";
	case 4: return "efficient aggressive";
	default: return "custom";
	}
}

int CurrentPowerIndex(int rawPower) {
	for (int i = 0; i < (int)Acpi().powers.size(); i++)
		if (Acpi().powers[i] == rawPower)
			return i;
	return -1;
}

void PrintPowerModes() {
	int rawPower = Acpi().GetPower(true);
	int current = CurrentPowerIndex(rawPower);

	if (Acpi().powers.empty()) {
		printf("No power modes were reported by this system.\n");
		return;
	}

	printf("Power modes:\n");
	for (int i = 0; i < (int)Acpi().powers.size(); i++) {
		printf("  %d: raw 0x%02x%s%s\n",
			i,
			Acpi().powers[i],
			Acpi().powers[i] == 0 ? " (manual)" : "",
			i == current ? " *" : "");
	}

	if (rawPower >= 0 && current < 0)
		printf("Current raw power mode: 0x%02x (not in detected list)\n", rawPower);
}

void PrintTcc() {
	if (!Acpi().isTcc) {
		printf("TCC offset is not supported on this system.\n");
		return;
	}

	int current = Acpi().GetTCC();
	if (current < 0) {
		printf("TCC read failed.\n");
		return;
	}

	printf("TCC: %d, offset: %d, supported offset range: 0..%d\n",
		current, Acpi().maxTCC - current, Acpi().maxOffset);
}

void PrintStatus() {
	printf("System: %s (%lu), fans: %u, sensors: %u, power states: %u%s%s.\n",
		Acpi().isAlienware ? "Alienware" : "compatible",
		Acpi().systemID,
		(unsigned)Acpi().fans.size(),
		(unsigned)Acpi().sensors.size(),
		(unsigned)Acpi().powers.size(),
		Acpi().isGmode ? ", G-mode" : "",
		Acpi().isTcc ? ", TCC" : "");

	int rawPower = Acpi().GetPower(true);
	if (rawPower < 0) {
		printf("Power mode: read failed.\n");
	}
	else {
		int powerIndex = CurrentPowerIndex(rawPower);
		if (powerIndex >= 0)
			printf("Power mode: %d (raw 0x%02x)\n", powerIndex, rawPower);
		else
			printf("Power mode: raw 0x%02x\n", rawPower);
	}

	if (Acpi().isGmode) {
		int state = Acpi().GetGMode();
		printf("G-mode: %s\n", state > 0 ? "on" : "off");
	}

	PrintTcc();
}

void Usage() {
	printf("AlienPower-CLI\n\n");
	printf("Usage: alienpower-cli command[=value] [command[=value] ...]\n\n");
	printf("Commands:\n");
	printf("  status                  Show detected system, power mode, G-mode, and TCC.\n");
	printf("  list                    List detected BIOS/Alienware power modes.\n");
	printf("  power                   Show power modes.\n");
	printf("  power=<index>           Set BIOS/Alienware power mode by detected index.\n");
	printf("  power=manual            Set the first manual/raw-0 power mode if present.\n");
	printf("  power=performance       Set the last detected power mode.\n");
	printf("  gmode                   Show G-mode state.\n");
	printf("  gmode=<0|1|toggle>      Disable, enable, or toggle G-mode.\n");
	printf("  tcc                     Show current TCC and offset.\n");
	printf("  tcc=<level>             Set raw TCC level, for example 85.\n");
	printf("  tccoffset=<degrees>     Set TCC offset from max, for example 15.\n");
	printf("  perf=<mode>             Set Windows CPU boost for AC and DC.\n");
	printf("  perf=<ac>,<dc>          Set Windows CPU boost separately.\n\n");
	printf("CPU boost modes: 0 disabled, 1 enabled, 2 aggressive, 3 efficient, 4 efficient aggressive.\n");
	printf("Run this tool as Administrator.\n");
}

bool ResolvePowerValue(const std::string& text, int* powerIndex) {
	std::string value = ToLower(text);

	if (value == "performance" || value == "perf" || value == "turbo") {
		if (Acpi().powers.empty())
			return false;
		*powerIndex = (int)Acpi().powers.size() - 1;
		return true;
	}

	if (value == "manual") {
		for (int i = 0; i < (int)Acpi().powers.size(); i++)
			if (Acpi().powers[i] == 0) {
				*powerIndex = i;
				return true;
			}
		*powerIndex = 0;
		return !Acpi().powers.empty();
	}

	if (!ParseInt(value, powerIndex))
		return false;

	return *powerIndex >= 0 && *powerIndex < (int)Acpi().powers.size();
}

int SetPowerMode(const CommandArg& cmd) {
	if (!cmd.hasValue) {
		PrintPowerModes();
		return 0;
	}

	int powerIndex = -1;
	if (!ResolvePowerValue(cmd.value, &powerIndex)) {
		printf("Invalid power mode '%s'. Use 'list' to see available indexes.\n", cmd.value.c_str());
		return 1;
	}

	int result = Acpi().SetPower(Acpi().powers[powerIndex]);
	if (result < 0) {
		printf("Power mode set failed.\n");
		return 1;
	}

	printf("Power mode set to %d (raw 0x%02x).\n", powerIndex, Acpi().powers[powerIndex]);
	return 0;
}

int SetGMode(const CommandArg& cmd) {
	if (!Acpi().isGmode) {
		printf("G-mode is not supported on this system.\n");
		return 1;
	}

	if (!cmd.hasValue) {
		printf("G-mode: %s\n", Acpi().GetGMode() ? "on" : "off");
		return 0;
	}

	std::string value = ToLower(cmd.value);
	int state = 0;
	if (value == "toggle")
		state = Acpi().GetGMode() ? 0 : 1;
	else if (!ParseInt(value, &state) || (state != 0 && state != 1)) {
		printf("Invalid G-mode value '%s'. Use 0, 1, or toggle.\n", cmd.value.c_str());
		return 1;
	}

	int result = Acpi().SetGMode(state != 0);
	if (result < 0) {
		printf("G-mode change failed.\n");
		return 1;
	}

	printf("G-mode: %s\n", state ? "on" : "off");
	return 0;
}

bool ValidateTccLevel(int level) {
	int minTcc = Acpi().maxTCC - Acpi().maxOffset;
	return level >= minTcc && level <= Acpi().maxTCC;
}

int SetTccLevel(int level) {
	if (!ValidateTccLevel(level)) {
		printf("Invalid TCC level %d. Allowed level range is %d..%d, offset range is 0..%d.\n",
			level, Acpi().maxTCC - Acpi().maxOffset, Acpi().maxTCC, Acpi().maxOffset);
		return 1;
	}

	int result = Acpi().SetTCC((byte)level);
	if (result < 0) {
		printf("TCC set failed.\n");
		return 1;
	}

	printf("TCC set to %d (offset %d).\n", level, Acpi().maxTCC - level);
	return 0;
}

int SetTcc(const CommandArg& cmd, bool byOffset) {
	if (!Acpi().isTcc) {
		printf("TCC offset is not supported on this system.\n");
		return 1;
	}

	if (!cmd.hasValue) {
		PrintTcc();
		return 0;
	}

	int value = 0;
	if (!ParseInt(cmd.value, &value)) {
		printf("Invalid TCC value '%s'.\n", cmd.value.c_str());
		return 1;
	}

	if (byOffset) {
		if (value < 0 || value > Acpi().maxOffset) {
			printf("Invalid TCC offset %d. Allowed offset range is 0..%d.\n", value, Acpi().maxOffset);
			return 1;
		}
		value = Acpi().maxTCC - value;
	}

	return SetTccLevel(value);
}

int SetCpuBoost(const CommandArg& cmd) {
	if (!cmd.hasValue) {
		printf("CPU boost requires a value. Use perf=<mode> or perf=<ac>,<dc>.\n");
		return 1;
	}

	bool ok = false;
	std::vector<int> modes = ParseCsvInts(cmd.value, &ok);
	if (!ok || modes.empty() || modes.size() > 2) {
		printf("Invalid CPU boost value '%s'. Use perf=<mode> or perf=<ac>,<dc>.\n", cmd.value.c_str());
		return 1;
	}

	int acMode = modes[0];
	int dcMode = modes.size() == 2 ? modes[1] : modes[0];
	if (acMode < 0 || acMode > 4 || dcMode < 0 || dcMode > 4) {
		printf("CPU boost mode should be in 0..4.\n");
		return 1;
	}

	GUID* scheme = NULL;
	DWORD result = PowerGetActiveScheme(NULL, &scheme);
	if (result != ERROR_SUCCESS || !scheme) {
		printf("Failed to read active Windows power scheme (%lu).\n", result);
		return 1;
	}

	static const GUID processorBoostMode = {
		0xbe337238, 0x0d82, 0x4146,
		{ 0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7 }
	};

	result = PowerWriteACValueIndex(NULL, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &processorBoostMode, (DWORD)acMode);
	if (result == ERROR_SUCCESS)
		result = PowerWriteDCValueIndex(NULL, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &processorBoostMode, (DWORD)dcMode);
	if (result == ERROR_SUCCESS)
		result = PowerSetActiveScheme(NULL, scheme);

	LocalFree(scheme);

	if (result != ERROR_SUCCESS) {
		printf("CPU boost change failed (%lu).\n", result);
		return 1;
	}

	printf("CPU boost set to AC %d (%s), DC %d (%s).\n",
		acMode, CpuBoostName(acMode), dcMode, CpuBoostName(dcMode));
	return 0;
}

int RunCommand(const CommandArg& cmd) {
	if (cmd.name == "help" || cmd.name == "-h" || cmd.name == "--help" || cmd.name == "/?")
	{
		Usage();
		return 0;
	}
	if (cmd.name == "status")
	{
		PrintStatus();
		return 0;
	}
	if (cmd.name == "list")
	{
		PrintPowerModes();
		return 0;
	}
	if (cmd.name == "power" || cmd.name == "setpower")
		return SetPowerMode(cmd);
	if (cmd.name == "gmode" || cmd.name == "setgmode")
		return SetGMode(cmd);
	if (cmd.name == "tcc" || cmd.name == "settcc")
		return SetTcc(cmd, false);
	if (cmd.name == "tccoffset" || cmd.name == "offset")
		return SetTcc(cmd, true);
	if (cmd.name == "perf" || cmd.name == "boost" || cmd.name == "setperf")
		return SetCpuBoost(cmd);

	printf("Unknown command '%s'. Run without arguments for help.\n", cmd.name.c_str());
	return 1;
}

}

int main(int argc, char* argv[]) {
	printf("AlienPower-CLI v1.0\n");

	if (argc == 2) {
		CommandArg first = ParseCommand(argv[1]);
		if (first.name == "help" || first.name == "-h" || first.name == "--help" || first.name == "/?") {
			Usage();
			return 0;
		}
	}

	AlienFan_SDK::Control control;
	acpi = &control;

	if (!Acpi().Probe(false)) {
		printf("%sHardware not compatible.\n", Acpi().isAlienware ? "Alienware system, " : "");
		return 2;
	}

	if (argc < 2) {
		Usage();
		printf("\n");
		PrintStatus();
		return 0;
	}

	int exitCode = 0;
	for (int i = 1; i < argc; i++) {
		int result = RunCommand(ParseCommand(argv[i]));
		if (result)
			exitCode = result;
	}

	return exitCode;
}
