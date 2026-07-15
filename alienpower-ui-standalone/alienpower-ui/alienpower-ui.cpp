#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <PowrProf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "alienfan-SDK.h"
#include "resource.h"

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace {

enum ControlId {
	ID_STATUS = 1001,
	ID_POWER_COMBO,
	ID_POWER_APPLY,
	ID_GMODE_TOGGLE,
	ID_TCC_OFFSET,
	ID_TCC_APPLY,
	ID_CPU_BOOST,
	ID_CPU_APPLY,
	ID_REFRESH
};

HINSTANCE appInstance = NULL;
HFONT uiFont = NULL;
AlienFan_SDK::Control* acpi = NULL;
bool supported = false;

HWND statusLabel = NULL;
HWND powerCombo = NULL;
HWND powerApply = NULL;
HWND gmodeToggle = NULL;
HWND tccOffsetEdit = NULL;
HWND tccApply = NULL;
HWND cpuBoostCombo = NULL;
HWND cpuApply = NULL;
HWND refreshButton = NULL;
HWND tccRangeLabel = NULL;

std::wstring Format(const wchar_t* fmt, ...) {
	wchar_t buffer[512];
	va_list args;
	va_start(args, fmt);
	vswprintf(buffer, ARRAYSIZE(buffer), fmt, args);
	va_end(args);
	return buffer;
}

HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
	int x, int y, int w, int h, int id) {
	HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
		x, y, w, h, parent, (HMENU)(INT_PTR)id, appInstance, NULL);
	if (control && uiFont)
		SendMessage(control, WM_SETFONT, (WPARAM)uiFont, TRUE);
	return control;
}

void SetStatus(const std::wstring& text) {
	SetWindowTextW(statusLabel, text.c_str());
}

int CurrentPowerIndex(int rawPower) {
	if (!acpi)
		return -1;
	for (int i = 0; i < (int)acpi->powers.size(); i++)
		if (acpi->powers[i] == rawPower)
			return i;
	return -1;
}

void SetControlsEnabled(bool enabled) {
	EnableWindow(powerCombo, enabled);
	EnableWindow(powerApply, enabled);
	EnableWindow(gmodeToggle, enabled && acpi && acpi->isGmode);
	EnableWindow(tccOffsetEdit, enabled && acpi && acpi->isTcc);
	EnableWindow(tccApply, enabled && acpi && acpi->isTcc);
	EnableWindow(cpuBoostCombo, enabled);
	EnableWindow(cpuApply, enabled);
}

void UpdateGModeButton() {
	if (!acpi || !acpi->isGmode) {
		SetWindowTextW(gmodeToggle, L"G 模式不支持");
		return;
	}

	SetWindowTextW(gmodeToggle, acpi->GetGMode() ? L"关闭 G 模式" : L"开启 G 模式");
}

std::wstring PowerModeName(int index, int rawValue) {
	switch (rawValue) {
	case 0xa0:
		return L"平衡";
	case 0xa1:
		return L"性能";
	case 0xa3:
		return L"静音";
	case 0xa5:
		return L"电池";
	}
	return Format(L"BIOS 档位 %d", index);
}

std::wstring CurrentPowerName(int rawValue) {
	if (rawValue == 0x00)
		return L"自定义";
	if (rawValue == 0xab)
		return L"G 模式";

	int index = CurrentPowerIndex(rawValue);
	return index >= 0 ? PowerModeName(index, rawValue) : L"未知电源模式";
}

bool IsPowerModeVisible(int rawValue) {
	return rawValue != 0x00 && rawValue != 0xab;
}

int VisiblePowerModeCount() {
	if (!acpi)
		return 0;

	int count = 0;
	for (int rawValue : acpi->powers)
		if (IsPowerModeVisible(rawValue))
			count++;
	return count;
}

int SelectedPowerRaw() {
	int selected = (int)SendMessageW(powerCombo, CB_GETCURSEL, 0, 0);
	if (selected < 0)
		return -1;

	LRESULT rawValue = SendMessageW(powerCombo, CB_GETITEMDATA, selected, 0);
	return rawValue == CB_ERR ? -1 : (int)rawValue;
}

void SelectPowerRaw(int rawValue) {
	int count = (int)SendMessageW(powerCombo, CB_GETCOUNT, 0, 0);
	for (int i = 0; i < count; i++) {
		LRESULT itemRaw = SendMessageW(powerCombo, CB_GETITEMDATA, i, 0);
		if (itemRaw != CB_ERR && (int)itemRaw == rawValue) {
			SendMessageW(powerCombo, CB_SETCURSEL, i, 0);
			return;
		}
	}
}

void PopulatePowerModes() {
	SendMessageW(powerCombo, CB_RESETCONTENT, 0, 0);

	if (!acpi || acpi->powers.empty())
		return;

	int rawPower = acpi->GetPower(true);
	int current = -1;
	int visibleIndex = 0;

	for (int i = 0; i < (int)acpi->powers.size(); i++) {
		int rawValue = acpi->powers[i];
		if (!IsPowerModeVisible(rawValue))
			continue;

		std::wstring name = PowerModeName(i, rawValue);
		std::wstring label = name;
		int comboIndex = (int)SendMessageW(powerCombo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
		if (comboIndex >= 0) {
			SendMessageW(powerCombo, CB_SETITEMDATA, comboIndex, rawValue);
			if (rawValue == rawPower)
				current = comboIndex;
		}
		visibleIndex++;
	}

	if (visibleIndex > 0)
		SendMessageW(powerCombo, CB_SETCURSEL, current >= 0 ? current : 0, 0);
}

void UpdateTccFields() {
	if (!acpi || !acpi->isTcc) {
		SetWindowTextW(tccOffsetEdit, L"");
		SetWindowTextW(tccRangeLabel, L"TCC 不支持");
		return;
	}

	int current = acpi->GetTCC();
	if (current < 0) {
		SetWindowTextW(tccOffsetEdit, L"");
		SetWindowTextW(tccRangeLabel, L"TCC 读取失败");
		return;
	}

	int offset = acpi->maxTCC - current;
	SetWindowTextW(tccOffsetEdit, Format(L"%d", offset).c_str());
	SetWindowTextW(tccRangeLabel, Format(L"范围 0..%d，当前 TCC %d", acpi->maxOffset, current).c_str());
}

void UpdateStatus() {
	if (!supported || !acpi) {
		SetStatus(L"未检测到兼容硬件。");
		SetControlsEnabled(false);
		return;
	}

	int rawPower = acpi->GetPower(true);
	std::wstring text = Format(L"系统 %lu | 电源模式: %u 种",
		acpi->systemID,
		(unsigned)VisiblePowerModeCount());

	if (rawPower >= 0) {
		text += Format(L" | 当前: %ls", CurrentPowerName(rawPower).c_str());
	}
	else {
		text += L" | 当前电源模式读取失败";
	}

	if (acpi->isGmode)
		text += acpi->GetGMode() ? L" | G 模式开" : L" | G 模式关";
	else
		text += L" | G 模式不支持";

	if (acpi->isTcc) {
		int tcc = acpi->GetTCC();
		if (tcc >= 0)
			text += Format(L" | TCC 偏移 %d", acpi->maxTCC - tcc);
	}
	else {
		text += L" | TCC 不支持";
	}

	SetStatus(text);
	SetControlsEnabled(true);
	UpdateGModeButton();
	UpdateTccFields();
}

void ReleaseAcpi() {
	delete acpi;
	acpi = NULL;
	supported = false;
}

void ProbeHardware() {
	SetStatus(L"正在检测硬件...");
	SetControlsEnabled(false);

	ReleaseAcpi();
	acpi = new AlienFan_SDK::Control();
	supported = acpi->Probe(false);

	PopulatePowerModes();
	UpdateStatus();
}

void ApplyPowerMode(HWND parent) {
	if (!supported || !acpi)
		return;

	int rawPower = SelectedPowerRaw();
	if (rawPower < 0) {
		MessageBoxW(parent, L"请先选择电源模式。", L"AlienPower", MB_ICONWARNING);
		return;
	}

	if (acpi->SetPower((byte)rawPower) < 0) {
		MessageBoxW(parent, L"电源模式切换失败。", L"AlienPower", MB_ICONERROR);
		return;
	}

	UpdateStatus();
}

void ToggleGMode(HWND parent) {
	if (!supported || !acpi || !acpi->isGmode) {
		MessageBoxW(parent, L"这台机器不支持 G 模式。", L"AlienPower", MB_ICONINFORMATION);
		return;
	}

	bool nextState = !acpi->GetGMode();
	if (acpi->SetGMode(nextState) < 0) {
		MessageBoxW(parent, L"G 模式切换失败。", L"AlienPower", MB_ICONERROR);
		return;
	}

	if (!nextState) {
		const int performancePower = 0xa1;
		if (acpi->SetPower((byte)performancePower) < 0) {
			MessageBoxW(parent, L"G 模式已关闭，但切回性能模式失败。",
				L"AlienPower", MB_ICONWARNING);
		}
		else {
			SelectPowerRaw(performancePower);
		}
	}

	UpdateStatus();
}

void ApplyTccOffset(HWND parent) {
	if (!supported || !acpi || !acpi->isTcc) {
		MessageBoxW(parent, L"这台机器不支持 TCC 偏移。", L"AlienPower", MB_ICONINFORMATION);
		return;
	}

	wchar_t buffer[32];
	GetWindowTextW(tccOffsetEdit, buffer, ARRAYSIZE(buffer));
	wchar_t* end = NULL;
	long offset = wcstol(buffer, &end, 10);

	if (!end || *end || offset < 0 || offset > acpi->maxOffset) {
		MessageBoxW(parent, Format(L"TCC 偏移必须在 0..%d 之间。", acpi->maxOffset).c_str(),
			L"AlienPower", MB_ICONWARNING);
		return;
	}

	int tcc = acpi->maxTCC - (int)offset;
	if (acpi->SetTCC((byte)tcc) < 0) {
		MessageBoxW(parent, L"TCC 偏移设置失败。", L"AlienPower", MB_ICONERROR);
		return;
	}

	UpdateStatus();
}

const wchar_t* CpuBoostName(int mode) {
	switch (mode) {
	case 0: return L"关闭";
	case 1: return L"开启";
	case 2: return L"高性能";
	case 3: return L"高效率";
	case 4: return L"高性能高效率";
	case 5: return L"积极且有保障";
	case 6: return L"高效、积极且有保障";
	default: return L"自定义";
	}
}

const GUID processorBoostMode = {
	0xbe337238, 0x0d82, 0x4146,
	{ 0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7 }
};

int GetCurrentCpuBoostMode() {
	GUID* scheme = NULL;
	DWORD result = PowerGetActiveScheme(NULL, &scheme);
	if (result != ERROR_SUCCESS || !scheme)
		return 1;

	DWORD acMode = 1;
	result = PowerReadACValueIndex(NULL, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &processorBoostMode, &acMode);
	LocalFree(scheme);

	if (result != ERROR_SUCCESS || acMode > 6)
		return 1;
	return (int)acMode;
}

DWORD SetCpuBoostMode(int mode) {
	GUID* scheme = NULL;
	DWORD result = PowerGetActiveScheme(NULL, &scheme);
	if (result != ERROR_SUCCESS || !scheme)
		return result ? result : ERROR_INVALID_HANDLE;

	result = PowerWriteACValueIndex(NULL, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &processorBoostMode, (DWORD)mode);
	if (result == ERROR_SUCCESS)
		result = PowerWriteDCValueIndex(NULL, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &processorBoostMode, (DWORD)mode);
	if (result == ERROR_SUCCESS)
		result = PowerSetActiveScheme(NULL, scheme);

	LocalFree(scheme);
	return result;
}

void ApplyCpuBoost(HWND parent) {
	int mode = (int)SendMessageW(cpuBoostCombo, CB_GETCURSEL, 0, 0);
	if (mode < 0 || mode > 6) {
		MessageBoxW(parent, L"请先选择 CPU 加速模式。", L"AlienPower", MB_ICONWARNING);
		return;
	}

	DWORD result = SetCpuBoostMode(mode);
	if (result != ERROR_SUCCESS) {
		MessageBoxW(parent, Format(L"CPU 加速模式设置失败 (%lu)。", result).c_str(),
			L"AlienPower", MB_ICONERROR);
		return;
	}

	MessageBoxW(parent, Format(L"CPU 加速模式已设置为：%ls。", CpuBoostName(mode)).c_str(),
		L"AlienPower", MB_ICONINFORMATION);
}

void PopulateCpuBoostModes() {
	SendMessageW(cpuBoostCombo, CB_RESETCONTENT, 0, 0);
	for (int i = 0; i <= 6; i++)
		SendMessageW(cpuBoostCombo, CB_ADDSTRING, 0, (LPARAM)Format(L"%d - %ls", i, CpuBoostName(i)).c_str());
	SendMessageW(cpuBoostCombo, CB_SETCURSEL, GetCurrentCpuBoostMode(), 0);
}

void CreateMainControls(HWND hwnd) {
	uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	statusLabel = AddControl(hwnd, L"STATIC", L"正在启动...", SS_LEFT | SS_NOPREFIX,
		14, 14, 500, 44, ID_STATUS);

	AddControl(hwnd, L"STATIC", L"电源模式", SS_LEFT, 14, 74, 96, 22, 0);
	powerCombo = AddControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
		120, 70, 230, 180, ID_POWER_COMBO);
	powerApply = AddControl(hwnd, L"BUTTON", L"应用", BS_PUSHBUTTON,
		365, 70, 120, 26, ID_POWER_APPLY);

	AddControl(hwnd, L"STATIC", L"G 模式", SS_LEFT, 14, 112, 96, 22, 0);
	gmodeToggle = AddControl(hwnd, L"BUTTON", L"G 模式", BS_PUSHBUTTON,
		120, 108, 180, 28, ID_GMODE_TOGGLE);

	AddControl(hwnd, L"STATIC", L"TCC 偏移", SS_LEFT, 14, 154, 96, 22, 0);
	tccOffsetEdit = AddControl(hwnd, L"EDIT", L"", ES_NUMBER | WS_BORDER | ES_AUTOHSCROLL,
		120, 150, 72, 24, ID_TCC_OFFSET);
	tccApply = AddControl(hwnd, L"BUTTON", L"应用", BS_PUSHBUTTON,
		205, 149, 95, 27, ID_TCC_APPLY);
	tccRangeLabel = AddControl(hwnd, L"STATIC", L"", SS_LEFT,
		315, 154, 190, 22, 0);

	AddControl(hwnd, L"STATIC", L"CPU 加速", SS_LEFT, 14, 196, 96, 22, 0);
	cpuBoostCombo = AddControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
		120, 192, 230, 170, ID_CPU_BOOST);
	cpuApply = AddControl(hwnd, L"BUTTON", L"应用", BS_PUSHBUTTON,
		365, 192, 120, 26, ID_CPU_APPLY);

	refreshButton = AddControl(hwnd, L"BUTTON", L"刷新", BS_PUSHBUTTON,
		365, 236, 120, 28, ID_REFRESH);

	PopulateCpuBoostModes();
	SetControlsEnabled(false);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_CREATE:
		CreateMainControls(hwnd);
		ProbeHardware();
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_POWER_APPLY:
			ApplyPowerMode(hwnd);
			return 0;
		case ID_GMODE_TOGGLE:
			ToggleGMode(hwnd);
			return 0;
		case ID_TCC_APPLY:
			ApplyTccOffset(hwnd);
			return 0;
		case ID_CPU_APPLY:
			ApplyCpuBoost(hwnd);
			return 0;
		case ID_REFRESH:
			ProbeHardware();
			return 0;
		default:
			return 0;
		}
	case WM_DESTROY:
		ReleaseAcpi();
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	appInstance = hInstance;

	const wchar_t* className = L"AlienPowerSmallUI";
	WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
	HICON appIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
		IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	HICON appIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
		IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = className;
	wc.hIcon = appIcon ? appIcon : LoadIcon(NULL, IDI_APPLICATION);
	wc.hIconSm = appIconSmall ? appIconSmall : wc.hIcon;

	if (!RegisterClassExW(&wc))
		return 1;

	HWND hwnd = CreateWindowExW(0, className, L"AMCC",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 540, 320,
		NULL, NULL, hInstance, NULL);
	if (!hwnd)
		return 1;

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}
