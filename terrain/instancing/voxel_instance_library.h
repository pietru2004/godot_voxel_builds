#ifndef VOXEL_INSTANCE_MODEL_LIBRARY_H
#define VOXEL_INSTANCE_MODEL_LIBRARY_H

#include "voxel_instance_library_item.h"

namespace zylann::voxel {

// Contains a list of items that can be used by VoxelInstancer, associated with a unique ID
class VoxelInstanceLibrary : public Resource, public VoxelInstanceLibraryItem::IListener {
	GDCLASS(VoxelInstanceLibrary, Resource)
public:
	static const int MAX_ID = 0xffff;

	class IListener {
	public:
		virtual void on_library_item_changed(int id, VoxelInstanceLibraryItem::ChangeType change) = 0;
	};

	~VoxelInstanceLibrary();

	int get_next_available_id();
	void add_item(int id, Ref<VoxelInstanceLibraryItem> item);
	void remove_item(int id);
	void clear();
	int find_item_by_name(String name) const;
	int get_item_count() const;

	// Internal

	const VoxelInstanceLibraryItem *get_item_const(int id) const;
	VoxelInstanceLibraryItem *get_item(int id);

	template <typename F>
	void for_each_item(F f) {
		for (Map<int, Ref<VoxelInstanceLibraryItem>>::Element *E = _items.front(); E != nullptr; E = E->next()) {
			CRASH_COND(E->value().is_null());
			f(E->key(), **E->value());
		}
	}

	void add_listener(IListener *listener);
	void remove_listener(IListener *listener);

protected:
	Ref<VoxelInstanceLibraryItem> _b_get_item(int id);

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

private:
	void on_library_item_changed(int id, VoxelInstanceLibraryItem::ChangeType change) override;
	void notify_listeners(int item_id, VoxelInstanceLibraryItem::ChangeType change);

	static void _bind_methods();

	// ID => Item
	// Using a Map keeps items ordered, so the last item has highest ID
	Map<int, Ref<VoxelInstanceLibraryItem>> _items;

	std::vector<IListener *> _listeners;
};

} // namespace zylann::voxel

#endif // VOXEL_INSTANCER_LIBRARY_H
