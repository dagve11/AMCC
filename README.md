# alienpower-cli standalone

Small Administrator-only console utility for Alienware BIOS/ACPI power features:

- BIOS/Alienware power mode
- G-mode
- TCC level and TCC offset
- Windows CPU boost mode

## Build

Open `alienpower-cli.sln` in Visual Studio and build `Release|x64`.

The project includes a minimal copy of `alienfan-SDK_v2`, so it does not depend on the full `alienfx-tools` solution.

## Usage

```
alienpower-cli command[=value] [command[=value] ...]
```

Commands:

- `status` - show detected hardware, current power mode, G-mode state, and TCC.
- `list` - list detected BIOS/Alienware power mode indexes.
- `power=<index>` - set BIOS/Alienware power mode by detected index.
- `power=manual` - set the first manual/raw-0 power mode if present.
- `power=performance` - set the last detected power mode.
- `gmode` - show G-mode state.
- `gmode=<0|1|toggle>` - disable, enable, or toggle G-mode.
- `tcc` - show current TCC level and offset.
- `tcc=<level>` - set raw TCC level.
- `tccoffset=<degrees>` - set TCC offset from the maximum TCC.
- `perf=<mode>` - set Windows CPU boost mode for AC and DC.
- `perf=<ac>,<dc>` - set Windows CPU boost modes separately.

CPU boost modes:

- `0` - disabled
- `1` - enabled
- `2` - aggressive
- `3` - efficient
- `4` - efficient aggressive

Examples:

```
alienpower-cli list
alienpower-cli power=performance
alienpower-cli gmode=toggle
alienpower-cli tccoffset=15
alienpower-cli perf=2,0
alienpower-cli power=2 gmode=1 tccoffset=10
```
