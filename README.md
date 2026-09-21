# Zombie Shooter Engine Source

A Win32/x86 source project for **Zombie Shooter**, intended for preservation, maintenance, study and non-commercial modification.

The repository contains the engine source, Visual Studio project files, the Win32 resource payload required by the executable, and the bundled Win32 Ogg/Vorbis dependencies used by the project. It does **not** include the original game data required to play Zombie Shooter.

## Requirements

- Windows.
- Visual Studio 2022 with the **Desktop development with C++** workload.
- **MSVC v143** toolset.
- **Windows 10 SDK 10.0.19041.0**.
- Win32/x86 build support.
- A legally obtained Zombie Shooter installation for runtime game data.

## Build

1. Open `ZombieShooter.sln` in Visual Studio 2022.
2. Select `Release | Win32` for the normal build, or `Debug | Win32` for a debug build.
3. Build the `ZombieShooter` project.

The project uses the following output directories:

- Release: `o\Release\`
- Debug: `o\Debug\`
- Intermediate files: `b\Release\` or `b\Debug\`

The generated executable is `ZombieShooter.exe`.

The build creates the required Win32 `d3d8.lib` import library from `sources\win\imports\d3d8.def` in the intermediate directory. No proprietary Direct3D import library is stored in the repository.

The required Win32 static Ogg/Vorbis libraries are included under `sources\3rdparty\win\xiph\lib\Win32\Release\`.

## Running

The source release does not contain the original Zombie Shooter game data, maps, music, sounds or other game assets.

For runtime testing, use a legally obtained Zombie Shooter installation and place the built `ZombieShooter.exe` where it can access that installation's game data. Keeping a backup of the original executable is recommended.

## Repository layout

- `sources\core\` — core application, configuration, resource and file services.
- `sources\graphics\` — graphics support code.
- `sources\images\` — image handling.
- `sources\vid\` — VID loading and rendering paths.
- `sources\sound\` — sound and audio support.
- `sources\script\` — script runtime support.
- `sources\game\` — game-side systems.
- `sources\zs1\` — Zombie Shooter-specific systems.
- `sources\win\` — Win32 application layer, entry point, imports, resources and platform sound code.
- `sources\3rdparty\win\` — bundled third-party Ogg/Vorbis headers, licenses and Win32 static libraries.
- `ZombieShooter.sln` / `ZombieShooter.vcxproj` — Visual Studio solution and project.
- `LICENSE.md` — source-code license.
- `GAME_CONTENT_NOTICE.md` — game-content and rights notice.

## Third-party components

The project includes components from **libogg** and **libvorbis**. Their license texts are retained in:

- `sources\3rdparty\win\libogg\COPYING`
- `sources\3rdparty\win\libvorbis\COPYING`

Those components remain subject to their respective licenses.

## Game content

This repository does not grant rights to Zombie Shooter game data, artwork, audio, maps, trademarks, logos or other copyrighted game assets.

The included Win32 resource payload is required by the executable and remains subject to the rights of the Zombie Shooter rights holder. See `GAME_CONTENT_NOTICE.md` for details.

## License

This source project is available for **non-commercial use only** under [LICENSE.md](LICENSE.md).

Commercial use requires a separate written license from the relevant copyright holder. Third-party components remain under their own licenses, and game content is not licensed by this repository.
