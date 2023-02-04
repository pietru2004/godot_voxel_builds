#ifndef ZN_GODOT_EDITOR_INSPECTOR_PLUGIN_H
#define ZN_GODOT_EDITOR_INSPECTOR_PLUGIN_H

#if defined(ZN_GODOT)
#include <editor/editor_inspector.h>
#elif defined(ZN_GODOT_EXTENSION)
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
using namespace godot;
// TODO GDX: GodotCpp doesn't seem to define casts for global bitfield enums, so implementing virtuals or registering
// methods with such bitfields in them does not compile.
VARIANT_BITFIELD_CAST(PropertyUsageFlags)
#endif

namespace zylann {

class ZN_EditorInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(ZN_EditorInspectorPlugin, EditorInspectorPlugin)
public:
#if defined(ZN_GODOT)
	bool can_handle(Object *obj) override;
	bool parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint,
			const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide = false) override;
#elif defined(ZN_GODOT_EXTENSION)
	bool _can_handle(const Variant &obj_v) const override;
	bool _parse_property(Object *p_object, Variant::Type p_type, const String &p_path, PropertyHint p_hint,
			const String &p_hint_text, BitField<PropertyUsageFlags> p_usage, const bool p_wide = false) override;
#endif

protected:
	virtual bool _zn_can_handle(const Object *obj) const;
	virtual bool _zn_parse_property(Object *p_object, const Variant::Type p_type, const String &p_path,
			const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage,
			const bool p_wide);

private:
	// When compiling with GodotCpp, `_bind_methods` is not optional
	static void _bind_methods() {}
};

} // namespace zylann

#endif // ZN_GODOT_EDITOR_INSPECTOR_PLUGIN_H
