---
name: add-godot-resource
description: Guide for creating a new Resource subclass in Godot Engine (C++). Use this when asked to add a new resource type, asset type, or data container class that should be saveable and editable in the Godot inspector.
---

# Adding a New Resource Class to Godot Engine

`Resource` is the base class for all serialisable data objects in Godot. Resources can be saved as `.tres` (text) or `.res` (binary) files, and their properties appear in the Inspector.

## File locations

New built-in resources typically go in one of:
- `scene/resources/` — scene-related resources (materials, meshes, curves, etc.)
- `core/io/` — I/O-related resources
- A module's own directory if the resource is module-specific

## Minimal skeleton

### Header — `scene/resources/my_resource.h`

```cpp
#pragma once
#include "core/io/resource.h"

class MyResource : public Resource {
    GDCLASS(MyResource, Resource);
    OBJ_SAVE_TYPE(MyResource);    // enables correct type name when saving
    RES_BASE_EXTENSION("myres");  // default file extension (optional)

    String data;

protected:
    static void _bind_methods();

public:
    void set_data(const String &p_data);
    String get_data() const;
};
```

### Implementation — `scene/resources/my_resource.cpp`

```cpp
#include "my_resource.h"
#include "core/object/class_db.h"

void MyResource::set_data(const String &p_data) {
    data = p_data;
    emit_changed();  // required for Resource subclasses
}

String MyResource::get_data() const {
    return data;
}

void MyResource::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_data", "data"), &MyResource::set_data);
    ClassDB::bind_method(D_METHOD("get_data"), &MyResource::get_data);

    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "data", PROPERTY_HINT_MULTILINE_TEXT),
        "set_data", "get_data"
    );
}
```

## Registration

In `scene/register_scene_types.cpp`, add **in alphabetical order**:

```cpp
// Among the #include block (alphabetical order):
#include "scene/resources/my_resource.h"

// Inside register_scene_types():
GDREGISTER_CLASS(MyResource);
```

> If the resource belongs to a module, register it in the module's `register_types.cpp` instead.

## Property hints reference

```cpp
// Plain string
PropertyInfo(Variant::STRING, "name")

// Multiline text editor
PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_MULTILINE_TEXT)

// Float with range [min, max, step] and unit suffix
PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,1000,0.1,suffix:px/s")

// Integer enum
PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Linear,Constant,Cubic")

// Reference to another Resource
PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")

// Color (no alpha)
PropertyInfo(Variant::COLOR, "tint", PROPERTY_HINT_COLOR_NO_ALPHA)

// Array of floats
PropertyInfo(Variant::ARRAY, "weights", PROPERTY_HINT_ARRAY_TYPE, "float")
```

## Inspector groups

Use `ADD_GROUP` to organise properties visually in the Inspector:

```cpp
ADD_GROUP("Appearance", "appearance_");
ADD_PROPERTY(PropertyInfo(Variant::COLOR, "appearance_color"), "set_color", "get_color");
ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "appearance_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_opacity", "get_opacity");
```

## Key rules

- Always call `emit_changed()` in every setter — this triggers ResourceSaver and editor updates.
- Use `#pragma once` instead of include guards.
- Indent with **tabs**, not spaces.
- `OBJ_SAVE_TYPE(ClassName)` ensures correct class name appears in saved files.
- `RES_BASE_EXTENSION("ext")` sets the default extension shown in the save dialog.

## XML documentation

Every public class needs an entry in `doc/classes/<ClassName>.xml`. Generate stubs after building:

```sh
./bin/godot.linuxbsd.editor.x86_64 --doctool doc/classes --no-docbase
```

Then fill in the `<description>` and each `<member>` description.

## Verify

```sh
# Build the editor
scons platform=linuxbsd target=editor dev_mode=yes

# Run the editor to confirm the class appears in the Create Resource dialog
./bin/godot.linuxbsd.editor.x86_64
```
