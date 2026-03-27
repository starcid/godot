---
name: add-godot-module
description: Step-by-step guide for creating a new optional module in Godot Engine. Use this when asked to add, scaffold, or create a new engine module under the modules/ directory.
---

# Adding a New Godot Engine Module

Godot modules live in `modules/<module_name>/`. Each module is self-contained and can be toggled with `module_<name>_enabled=yes/no`.

## Required files

Every module **must** contain these files:

| File | Purpose |
|---|---|
| `SCsub` | SCons build script — compiles the module's sources |
| `config.py` | Declares `can_build()`, `configure()`, `get_doc_classes()` |
| `register_types.h` | Declares `initialize_<name>_module()` / `uninitialize_<name>_module()` |
| `register_types.cpp` | Implements the above; calls `GDREGISTER_CLASS(...)` for each class |

## Step-by-step

### 1. Create the directory

```sh
mkdir modules/<module_name>
```

Use lowercase and underscores for the directory name (e.g., `my_feature`).

### 2. Write `config.py`

```python
def can_build(env, platform):
    return True  # or False to disable on certain platforms

def configure(env):
    pass

def get_doc_classes():
    return ["MyClass"]  # list all public classes in this module

def get_doc_path():
    return "doc_classes"
```

### 3. Write `SCsub`

```python
Import("env")
Import("env_modules")

env_module = env_modules.Clone()
env_module.add_source_files(env.modules_sources, "*.cpp")
```

If the module has subdirectories, add `SConscript("subdir/SCsub")`.

### 4. Write `register_types.h`

```cpp
#pragma once
#include "modules/register_module_types.h"

void initialize_my_feature_module(ModuleInitializationLevel p_level);
void uninitialize_my_feature_module(ModuleInitializationLevel p_level);
```

### 5. Write `register_types.cpp`

```cpp
#include "register_types.h"
#include "core/object/class_db.h"
#include "my_class.h"

void initialize_my_feature_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(MyClass);
}

void uninitialize_my_feature_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}
```

### 6. Create your class files

Follow the standard Godot C++ class pattern:

**`my_class.h`**
```cpp
#pragma once
#include "core/object/ref_counted.h"

class MyClass : public RefCounted {
    GDCLASS(MyClass, RefCounted);

protected:
    static void _bind_methods();

public:
    // your API
};
```

**`my_class.cpp`**
```cpp
#include "my_class.h"

void MyClass::_bind_methods() {
    // ClassDB::bind_method(...)
    // ADD_PROPERTY(...)
}
```

### 7. Add XML documentation

Create `doc_classes/<ClassName>.xml` stubs or regenerate them after building:

```sh
./bin/godot.linuxbsd.editor.x86_64 --doctool doc/classes --no-docbase
```

## Key conventions

- Use `GDCLASS(ClassName, ParentClass)` in every class that extends `Object`.
- Use `emit_changed()` in setters of `Resource` subclasses.
- Indent with **tabs**, not spaces.
- Use `#pragma once` instead of `#ifndef` include guards.
- Register the module in the module system automatically — Godot's SCons discovers modules in `modules/` automatically; no top-level edit is needed.

## Building and testing

```sh
# Build editor with your new module
scons platform=linuxbsd target=editor dev_mode=yes

# Disable your module to verify the engine still builds without it
scons platform=linuxbsd target=editor module_my_feature_enabled=no
```
