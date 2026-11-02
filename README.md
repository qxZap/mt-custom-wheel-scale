# Custom Wheel Scale

Plugin to change the scale of the wheels beyond their limits. Also adds wheel width scale..

Raises the garage limits on wheel radius past the game's 0.80 - 1.20, adds a wheel width line per axle, and lets any wheel be fitted to any vehicle, including wheels from other mods.

Settings: `Binaries\Win64\plugins\custom_wheel_scale.ini`, section `[WheelScale]`: `Min`, `Max`, `WidthMin`, `WidthMax`, `Step`.

Single player and hosting only: on someone else's server, that server's limits apply.

## Install

Needs [Plugin Loader](https://github.com/qxZap/mt-plugin-loader) (`version.dll`) in
`MotorTown\Binaries\Win64\`. Put `mt_plugin_CustomWheelScale.dll` in `MotorTown\Binaries\Win64\plugins\`.

## Build

`build.bat` (MSVC 2022, x64) writes `out\mt_plugin_CustomWheelScale.dll`.
