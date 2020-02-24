#ifndef VOXEL_GRAPH_NODE_DB_H
#define VOXEL_GRAPH_NODE_DB_H

#include "voxel_generator_graph.h"
#include <core/object.h>

class VoxelGraphNodeDB {
public:
	struct Port {
		String name;

		Port(String p_name) :
				name(p_name) {}
	};

	struct Param {
		String name;
		Variant default_value;
		Variant::Type type;
		String class_name;

		Param(String p_name, Variant::Type p_type, Variant p_default_value = Variant()) :
				name(p_name),
				type(p_type),
				default_value(p_default_value) {}

		Param(String p_name, String p_class_name) :
				name(p_name),
				type(Variant::OBJECT),
				class_name(p_class_name) {}
	};

	struct NodeType {
		String name;
		std::vector<Port> inputs;
		std::vector<Port> outputs;
		std::vector<Param> params;
	};

	VoxelGraphNodeDB();

	static VoxelGraphNodeDB *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	int get_type_count() const { return _types.size(); }
	const NodeType &get_type(int id) const { return _types[id]; }
	Dictionary get_type_info_dict(int id) const;

private:
	FixedArray<NodeType, VoxelGeneratorGraph::NODE_TYPE_COUNT> _types;
};

#endif // VOXEL_GRAPH_NODE_DB_H
