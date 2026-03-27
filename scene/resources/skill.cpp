/**************************************************************************/
/*  skill.cpp                                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "skill.h"

#include "core/object/class_db.h"

void Skill::set_skill_name(const String &p_name) {
	skill_name = p_name;
	emit_changed();
}

String Skill::get_skill_name() const {
	return skill_name;
}

void Skill::set_description(const String &p_description) {
	description = p_description;
	emit_changed();
}

String Skill::get_description() const {
	return description;
}

void Skill::set_icon(const Ref<Texture2D> &p_icon) {
	icon = p_icon;
	emit_changed();
}

Ref<Texture2D> Skill::get_icon() const {
	return icon;
}

void Skill::set_cooldown(float p_cooldown) {
	cooldown = MAX(0.0f, p_cooldown);
	emit_changed();
}

float Skill::get_cooldown() const {
	return cooldown;
}

void Skill::set_energy_cost(float p_energy_cost) {
	energy_cost = MAX(0.0f, p_energy_cost);
	emit_changed();
}

float Skill::get_energy_cost() const {
	return energy_cost;
}

void Skill::set_damage(float p_damage) {
	damage = MAX(0.0f, p_damage);
	emit_changed();
}

float Skill::get_damage() const {
	return damage;
}

void Skill::set_range(float p_range) {
	range = MAX(0.0f, p_range);
	emit_changed();
}

float Skill::get_range() const {
	return range;
}

void Skill::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_skill_name", "name"), &Skill::set_skill_name);
	ClassDB::bind_method(D_METHOD("get_skill_name"), &Skill::get_skill_name);

	ClassDB::bind_method(D_METHOD("set_description", "description"), &Skill::set_description);
	ClassDB::bind_method(D_METHOD("get_description"), &Skill::get_description);

	ClassDB::bind_method(D_METHOD("set_icon", "icon"), &Skill::set_icon);
	ClassDB::bind_method(D_METHOD("get_icon"), &Skill::get_icon);

	ClassDB::bind_method(D_METHOD("set_cooldown", "cooldown"), &Skill::set_cooldown);
	ClassDB::bind_method(D_METHOD("get_cooldown"), &Skill::get_cooldown);

	ClassDB::bind_method(D_METHOD("set_energy_cost", "energy_cost"), &Skill::set_energy_cost);
	ClassDB::bind_method(D_METHOD("get_energy_cost"), &Skill::get_energy_cost);

	ClassDB::bind_method(D_METHOD("set_damage", "damage"), &Skill::set_damage);
	ClassDB::bind_method(D_METHOD("get_damage"), &Skill::get_damage);

	ClassDB::bind_method(D_METHOD("set_range", "range"), &Skill::set_range);
	ClassDB::bind_method(D_METHOD("get_range"), &Skill::get_range);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "skill_name"), "set_skill_name", "get_skill_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_MULTILINE_TEXT), "set_description", "get_description");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_icon", "get_icon");

	ADD_GROUP("Stats", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cooldown", PROPERTY_HINT_RANGE, "0,3600,0.01,suffix:s"), "set_cooldown", "get_cooldown");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_cost", PROPERTY_HINT_RANGE, "0,10000,0.01"), "set_energy_cost", "get_energy_cost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damage", PROPERTY_HINT_RANGE, "0,10000,0.01"), "set_damage", "get_damage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "range", PROPERTY_HINT_RANGE, "0,10000,0.01,suffix:px"), "set_range", "get_range");
}
