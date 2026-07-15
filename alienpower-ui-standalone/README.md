# AlienPower UI 独立版

这是一个需要管理员权限运行的 Win32 小窗口工具，用来控制 Alienware BIOS/ACPI 电源相关功能。

## 功能

- 电源模式下拉框：显示当前机器检测到的全部电源模式，并给出中文占位名。
- 状态栏：显示检测到的电源模式数量，例如 `电源模式: 3 种`。
- G 模式按钮：支持时可开启/关闭。关闭时会切回下拉框当前选中的电源模式。
- TCC 偏移输入框：支持时可设置 TCC offset。
- CPU 加速下拉框：设置 Windows 当前电源计划的 CPU boost mode。
- 刷新按钮：重新检测硬件。

## 电源模式数量

电源模式数量不是源码里固定写死的，而是运行时由 Alienware BIOS/WMI 返回。

SDK 逻辑是：

- 先加入 `Manual` 手动模式。
- 再枚举 BIOS/WMI 返回的 power modes。
- 老接口/R7 兜底通常是 3 种：`Manual + 1 + 2`。

所以不同机型可能不一样，实际数量以 UI 状态栏显示为准。

BIOS/WMI 通常只返回原始编号，不返回官方中文名称。UI 会显示类似：

- `0 - 手动 (原始值 0x00)`
- `1 - BIOS 模式 1 (原始值 0x...)`
- `最后一个 - 最高/性能档 (原始值 0x...)`

这些中文名是为了便于识别，不代表 AWCC 官方命名；真正写入 BIOS 的仍是括号里的原始值。

## 编译

打开 `alienpower-ui.sln`，选择 `Release|x64` 编译。

项目自带最小 `alienfan-SDK_v2` 源码副本，不依赖完整 `alienfx-tools` 主工程。

如果 Visual Studio 提示没有 `v145` toolset，在项目属性里把 Platform Toolset 改成你本机已有版本即可。
