# Godot Engine — Copilot Instructions

## Project Overview

This is a fork of [Godot Engine](https://godotengine.org), a free, open-source, MIT-licensed 2D and 3D game engine written in C++. The current version under development is **4.7.0-dev**. The active development branch for this fork is `dev/xess`.

## Repository Layout

```
godot/
├── core/           # Core engine: object system, memory, math, containers, I/O
├── drivers/        # Low-level platform drivers (audio, display, etc.)
├── editor/         # In-engine editor (only compiled with target=editor)
├── main/           # Engine entry point and main loop
├── modules/        # Optional, toggleable feature modules
├── platform/       # Platform-specific backends (linux, windows, macos, android, ios, web)
├── scene/          # Scene system: nodes, resources, 2D/3D
│   ├── 2d/         # 2D nodes (Sprite2D, Camera2D, physics, etc.)
│   ├── 3d/         # 3D nodes (Node3D, Camera3D, MeshInstance3D, etc.)
│   ├── animation/  # AnimationPlayer, AnimationTree
│   ├── audio/      # AudioStreamPlayer, bus system
│   ├── gui/        # UI controls (Button, Label, Container, etc.)
│   ├── main/       # SceneTree, Viewport, Window
│   └── resources/  # Resource types (Texture, Mesh, Material, Shader, etc.)
├── servers/        # Server singletons: RenderingServer, PhysicsServer, AudioServer
├── tests/          # Unit and integration tests (doctest-based)
├── thirdparty/     # Vendored third-party libraries
└── .github/        # CI workflows, composite actions, issue/PR templates
```

## Build System

Godot uses **SCons** (not CMake). Never generate CMakeLists.txt or Makefile build files.

### Common build commands

```sh
# Editor build (development)
scons platform=linuxbsd target=editor dev_mode=yes

# Export template — release
scons platform=linuxbsd target=template_release

# Export template — debug
scons platform=linuxbsd target=template_debug

# Enable a module explicitly
scons platform=linuxbsd target=editor module_mono_enabled=yes

# Disable a module
scons platform=linuxbsd target=editor module_gdscript_enabled=no
```

Key SCons flags: `platform`, `target`, `arch`, `dev_mode`, `use_asan`, `use_ubsan`, `module_*_enabled`.

### Adding a new source file

For files under `scene/resources/`, `scene/2d/`, etc., **no SCsub edit is needed** — the existing `env.add_source_files(scene_obj, "*.cpp")` glob picks them up automatically.

For a brand-new subdirectory, add a `SCsub` file and call `SConscript()` from the parent `SCsub`.

## Code Style

- **Language**: C++17
- **Formatter**: `clang-format` (config in `.clang-format`, based on LLVM style)
- **Linter**: `clang-tidy` (config in `.clang-tidy`)
- **Pre-commit hooks**: defined in `.pre-commit-config.yaml`; run `pre-commit run --all-files` before committing
- **Static checks CI**: `.github/workflows/static_checks.yml` — runs clang-format, clang-tidy, and doc checks

### Key conventions

- Indent with **tabs**, not spaces
- `#pragma once` instead of include guards
- Class member variables use `snake_case` (no leading underscores unless required by Godot macros)
- `GDCLASS(ClassName, ParentClass)` macro in every GDObject subclass
- `OBJ_SAVE_TYPE(ClassName)` for saveable Resource subclasses
- `RES_BASE_EXTENSION("ext")` to give a Resource a default file extension
- `static void _bind_methods()` to expose methods/properties to GDScript and the editor
- Use `ClassDB::bind_method(D_METHOD(...), &Class::method)` for method binding
- Use `ADD_PROPERTY(PropertyInfo(...), "setter", "getter")` to expose properties
- Use `ADD_GROUP("GroupName", "prefix_")` to organise inspector groups
- Emit `emit_changed()` in setters for Resource subclasses

### Property hint examples

```cpp
// String with multiline editor widget
PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_MULTILINE_TEXT)

// Float with range and unit suffix
PropertyInfo(Variant::FLOAT, "cooldown", PROPERTY_HINT_RANGE, "0,3600,0.01,suffix:s")

// Resource reference
PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")

// Enum
PropertyInfo(Variant::INT, "interpolation_mode", PROPERTY_HINT_ENUM, "Linear,Constant,Cubic")
```

## Object/Resource System

- Everything that integrates with the engine inherits from `Object` → `RefCounted` → `Resource` or `Object` → `Node`
- Resources are serialised to `.tres` (text) or `.res` (binary) files
- Nodes live in the scene tree and are serialised to `.tscn` / `.scn` files
- Register new classes in the appropriate `register_*_types.cpp` file using `GDREGISTER_CLASS(ClassName)`
- Include the header alongside the other headers in that file, **in alphabetical order**

### Minimal new Resource skeleton

**`scene/resources/my_resource.h`**
```cpp
#pragma once
#include "core/io/resource.h"

class MyResource : public Resource {
    GDCLASS(MyResource, Resource);
    OBJ_SAVE_TYPE(MyResource);

    String data;

protected:
    static void _bind_methods();

public:
    void set_data(const String &p_data);
    String get_data() const;
};
```

**`scene/resources/my_resource.cpp`**
```cpp
#include "my_resource.h"
#include "core/object/class_db.h"

void MyResource::set_data(const String &p_data) { data = p_data; emit_changed(); }
String MyResource::get_data() const { return data; }

void MyResource::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_data", "data"), &MyResource::set_data);
    ClassDB::bind_method(D_METHOD("get_data"), &MyResource::get_data);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "data"), "set_data", "get_data");
}
```

**`scene/register_scene_types.cpp`** — add in alphabetical order:
```cpp
#include "scene/resources/my_resource.h"   // among includes
// ...
GDREGISTER_CLASS(MyResource);              // in register_scene_types()
```

## Modules

Each module lives in `modules/<name>/` and must contain:
- `SCsub` — build script
- `config.py` — `can_build()` / `configure()` / `get_doc_classes()`
- `register_types.h` / `register_types.cpp` — `initialize_*_module()` / `uninitialize_*_module()`

Modules are enabled by default unless `can_build()` returns `False` or the user passes `module_<name>_enabled=no`.

## Testing

Tests use the [doctest](https://github.com/doctest/doctest) framework and live in `tests/`. Run tests by building with `tests=yes` and executing the resulting binary with `--test`.

```sh
scons platform=linuxbsd target=editor tests=yes dev_mode=yes
./bin/godot.linuxbsd.editor.x86_64 --test
```

## CI Workflows (`.github/workflows/`)

| File | Purpose |
|---|---|
| `runner.yml` | Top-level dispatcher — triggers the others |
| `static_checks.yml` | clang-format, clang-tidy, doc checks |
| `linux_builds.yml` | Linux editor + template builds, unit tests |
| `windows_builds.yml` | Windows builds |
| `macos_builds.yml` | macOS builds |
| `android_builds.yml` | Android builds |
| `ios_builds.yml` | iOS builds |
| `web_builds.yml` | Web (Emscripten) builds |

Composite actions are in `.github/actions/`.

## Active Modules of Interest

| Module | Notes |
|---|---|
| `gdscript` | GDScript language implementation |
| `mono` | C# / .NET support |
| `jolt_physics` | Alternative physics engine (Jolt) |
| `openxr` | XR/VR support |
| `multiplayer` | High-level multiplayer API |
| `navigation_2d` / `navigation_3d` | Pathfinding |
| `gltf` | glTF 2.0 import/export |
| `noise` | FastNoiseLite procedural noise |

## Pull Request Guidelines

- Target the **`dev/xess`** branch for this fork
- Upstream Godot PRs target `master`
- Follow the checklist in `.github/PULL_REQUEST_TEMPLATE.md`
- All new C++ files must pass `clang-format` and `clang-tidy` checks
- New public API must be documented (XML docs in `doc/classes/`)
