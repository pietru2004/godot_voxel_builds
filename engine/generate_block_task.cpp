#include "generate_block_task.h"
#include "../generators/multipass/generate_block_multipass_task.h"
#include "../storage/voxel_buffer_internal.h"
#include "../storage/voxel_data.h"
#include "../util/godot/funcs.h"
#include "../util/log.h"
#include "../util/math/conv.h"
#include "../util/profiling.h"
#include "../util/string_funcs.h"
#include "../util/tasks/async_dependency_tracker.h"
#include "save_block_data_task.h"
#include "voxel_engine.h"

namespace zylann::voxel {

namespace {
std::atomic_int g_debug_generate_tasks_count = { 0 };
}

GenerateBlockTask::GenerateBlockTask() {
	int64_t v = ++g_debug_generate_tasks_count;
	ZN_PROFILE_PLOT("GenerateBlockTasks", v);
}

GenerateBlockTask::~GenerateBlockTask() {
	int64_t v = --g_debug_generate_tasks_count;
	ZN_PROFILE_PLOT("GenerateBlockTasks", v);
}

int GenerateBlockTask::debug_get_running_count() {
	return g_debug_generate_tasks_count;
}

void GenerateBlockTask::run(zylann::ThreadedTaskContext &ctx) {
	ZN_PROFILE_SCOPE();

	CRASH_COND(stream_dependency == nullptr);
	Ref<VoxelGenerator> generator = stream_dependency->generator;
	ERR_FAIL_COND(generator.is_null());

	Ref<VoxelGeneratorMultipass> multipass_generator = generator;
	if (multipass_generator.is_valid()) {
		ZN_ASSERT_RETURN(multipass_generator->get_pass_count() > 0);
		std::shared_ptr<VoxelGeneratorMultipass::Map> map = multipass_generator->get_map();
		const int final_subpass_index =
				VoxelGeneratorMultipass::get_subpass_count_from_pass_count(multipass_generator->get_pass_count()) - 1;

		{
			VoxelSpatialLockRead srlock(map->spatial_lock, BoxBounds3i::from_position(position));
			std::shared_ptr<VoxelGeneratorMultipass::Block> block;
			{
				MutexLock mlock(map->mutex);
				auto block_it = map->blocks.find(position);
				if (block_it == map->blocks.end()) {
					// Drop, for some reason it wasn't available
					return;
				}
				block = block_it->second;
			}

			// Null not allowed
			ZN_ASSERT(block != nullptr);

			if (block->subpass_index != final_subpass_index) {
				// The block isn't finished

				if (block->pending_subpass_tasks_mask != 0) {
					// Some tasks are working on the chunk, so we may try querying it again later.
					ctx.status = ThreadedTaskContext::STATUS_TAKEN_OUT;
					block->final_pending_task = this;

				} else {
					// No tasks working on it. That's a drop.
				}

			} else {
				// The block is ready

				// TODO Take out voxel data from this block, it must not be touched by generation anymore
				voxels = make_shared_instance<VoxelBufferInternal>();
				voxels->create(block->voxels.get_size());
				voxels->copy_from(block->voxels);

				run_stream_saving_and_finish();
			}
		}

		return;

#if 0 // Initial naive implementation concept
		if (_stage == 0) {
			ZN_ASSERT_RETURN(multipass_generator->get_pass_count() > 0);
			std::shared_ptr<std::atomic_int> counter = make_shared_instance<std::atomic_int>(1);
			const unsigned int subpass_count =
					VoxelGeneratorMultipass::get_subpass_count_from_pass_count(multipass_generator->get_pass_count());
			ZN_ASSERT_RETURN(subpass_count > 0);
			GenerateBlockMultipassTask *task = ZN_NEW(GenerateBlockMultipassTask(
					position, block_size, subpass_count - 1, multipass_generator, this, counter));
			VoxelEngine::get_singleton().push_async_task(task);

			ctx.status = ThreadedTaskContext::STATUS_TAKEN_OUT;

			_stage = 1;

		} else {
			std::shared_ptr<VoxelGeneratorMultipass::Map> map = multipass_generator->get_map();
			std::shared_ptr<VoxelGeneratorMultipass::Block> block;
			{
				MutexLock mlock(map->mutex);
				auto it = map->blocks.find(position);
				if (it == map->blocks.end()) {
					ZN_PRINT_ERROR(format("Could not generate block {} with multipass", position));
				}
				block = it->second;
			}
			// TODO Take out voxel data from this block, it must not be touched by generation anymore
			voxels = make_shared_instance<VoxelBufferInternal>();
			voxels->create(block->voxels.get_size());
			voxels->copy_from(block->voxels);

			run_stream_saving_and_finish();
		}
		return;
#endif
	}

	if (voxels == nullptr) {
		voxels = make_shared_instance<VoxelBufferInternal>();
		voxels->create(block_size, block_size, block_size);
	}

	if (use_gpu) {
		if (_stage == 0) {
			run_gpu_task(ctx);
		}
		if (_stage == 1) {
			run_gpu_conversion();
		}
		if (_stage == 2) {
			run_stream_saving_and_finish();
		}
	} else {
		run_cpu_generation();
		run_stream_saving_and_finish();
	}
}

void GenerateBlockTask::run_gpu_task(zylann::ThreadedTaskContext &ctx) {
	Ref<VoxelGenerator> generator = stream_dependency->generator;
	ERR_FAIL_COND(generator.is_null());

	// TODO Broad-phase to avoid the GPU part entirely?
	// Implement and call `VoxelGenerator::generate_broad_block()`

	std::shared_ptr<ComputeShader> generator_shader = generator->get_block_rendering_shader();
	ERR_FAIL_COND(generator_shader == nullptr);

	const Vector3i origin_in_voxels = (position << lod_index) * block_size;

	ZN_ASSERT(voxels != nullptr);
	VoxelGenerator::VoxelQueryData generator_query{ *voxels, origin_in_voxels, lod_index };
	if (generator->generate_broad_block(generator_query)) {
		_stage = 2;
		return;
	}

	const Vector3i resolution = Vector3iUtil::create(block_size);

	GenerateBlockGPUTask *gpu_task = memnew(GenerateBlockGPUTask);
	gpu_task->boxes_to_generate.push_back(Box3i(Vector3i(), resolution));
	gpu_task->generator_shader = generator_shader;
	gpu_task->generator_shader_params = generator->get_block_rendering_shader_parameters();
	gpu_task->generator_shader_outputs = generator->get_block_rendering_shader_outputs();
	gpu_task->lod_index = lod_index;
	gpu_task->origin_in_voxels = origin_in_voxels;
	gpu_task->consumer_task = this;

	if (data != nullptr) {
		const AABB aabb_voxels(to_vec3(origin_in_voxels), to_vec3(resolution << lod_index));
		std::vector<VoxelModifier::ShaderData> modifiers_shader_data;
		const VoxelModifierStack &modifiers = data->get_modifiers();
		modifiers.apply_for_gpu_rendering(modifiers_shader_data, aabb_voxels, VoxelModifier::ShaderData::TYPE_BLOCK);
		for (const VoxelModifier::ShaderData &d : modifiers_shader_data) {
			gpu_task->modifiers.push_back(GenerateBlockGPUTask::ModifierData{
					d.shader_rids[VoxelModifier::ShaderData::TYPE_BLOCK], d.params });
		}
	}

	ctx.status = ThreadedTaskContext::STATUS_TAKEN_OUT;

	// Start GPU task, we'll continue after it
	VoxelEngine::get_singleton().push_gpu_task(gpu_task);
}

void GenerateBlockTask::set_gpu_results(std::vector<GenerateBlockGPUTaskResult> &&results) {
	_gpu_generation_results = std::move(results);
	_stage = 1;
}

void GenerateBlockTask::run_gpu_conversion() {
	GenerateBlockGPUTaskResult::convert_to_voxel_buffer(to_span(_gpu_generation_results), *voxels);
	_stage = 2;
}

void GenerateBlockTask::run_cpu_generation() {
	const Vector3i origin_in_voxels = (position << lod_index) * block_size;

	Ref<VoxelGenerator> generator = stream_dependency->generator;

	VoxelGenerator::VoxelQueryData query_data{ *voxels, origin_in_voxels, lod_index };
	const VoxelGenerator::Result result = generator->generate_block(query_data);
	max_lod_hint = result.max_lod_hint;

	if (data != nullptr) {
		data->get_modifiers().apply(query_data.voxel_buffer,
				AABB(query_data.origin_in_voxels, query_data.voxel_buffer.get_size() << lod_index));
	}
}

void GenerateBlockTask::run_stream_saving_and_finish() {
	if (stream_dependency->valid) {
		Ref<VoxelStream> stream = stream_dependency->stream;

		// TODO In some cases we don't want this to run all the time, do we?
		// Like in full load mode, where non-edited blocks remain generated on the fly...
		if (stream.is_valid() && stream->get_save_generator_output()) {
			ZN_PRINT_VERBOSE(
					format("Requesting save of generator output for block {} lod {}", position, int(lod_index)));

			// TODO Optimization: `voxels` doesn't actually need to be shared
			std::shared_ptr<VoxelBufferInternal> voxels_copy = make_shared_instance<VoxelBufferInternal>();
			voxels->duplicate_to(*voxels_copy, true);

			// No instances, generators are not designed to produce them at this stage yet.
			// No priority data, saving doesn't need sorting.

			SaveBlockDataTask *save_task = memnew(SaveBlockDataTask(
					volume_id, position, lod_index, block_size, voxels_copy, stream_dependency, nullptr));

			VoxelEngine::get_singleton().push_async_io_task(save_task);
		}
	}

	has_run = true;
}

TaskPriority GenerateBlockTask::get_priority() {
	float closest_viewer_distance_sq;
	const TaskPriority p = priority_dependency.evaluate(
			lod_index, constants::TASK_PRIORITY_GENERATE_BAND2, &closest_viewer_distance_sq);
	too_far = drop_beyond_max_distance && closest_viewer_distance_sq > priority_dependency.drop_distance_squared;
	return p;
}

bool GenerateBlockTask::is_cancelled() {
	return !stream_dependency->valid || too_far; // || stream_dependency->stream->get_fallback_generator().is_null();
}

void GenerateBlockTask::apply_result() {
	bool aborted = true;

	if (VoxelEngine::get_singleton().is_volume_valid(volume_id)) {
		// TODO Comparing pointer may not be guaranteed
		// The request response must match the dependency it would have been requested with.
		// If it doesn't match, we are no longer interested in the result.
		if (stream_dependency->valid) {
			Ref<VoxelStream> stream = stream_dependency->stream;

			VoxelEngine::BlockDataOutput o;
			o.voxels = voxels;
			o.position = position;
			o.lod_index = lod_index;
			o.dropped = !has_run;
			if (stream.is_valid() && stream->get_save_generator_output()) {
				// We can't consider the block as "generated" since there is no state to tell that once saved,
				// so it has to be considered an edited block
				o.type = VoxelEngine::BlockDataOutput::TYPE_LOADED;
			} else {
				o.type = VoxelEngine::BlockDataOutput::TYPE_GENERATED;
			}
			o.max_lod_hint = max_lod_hint;
			o.initial_load = false;

			VoxelEngine::VolumeCallbacks callbacks = VoxelEngine::get_singleton().get_volume_callbacks(volume_id);
			ERR_FAIL_COND(callbacks.data_output_callback == nullptr);
			callbacks.data_output_callback(callbacks.data, o);

			aborted = !has_run;
		}

	} else {
		// This can happen if the user removes the volume while requests are still about to return
		ZN_PRINT_VERBOSE("Gemerated data request response came back but volume wasn't found");
	}

	// TODO We could complete earlier inside run() if we had access to the data structure to write the block into.
	// This would reduce latency a little. The rest of things the terrain needs to do with the generated block could
	// run later.
	if (tracker != nullptr) {
		if (aborted) {
			tracker->abort();
		} else {
			tracker->post_complete();
		}
	}
}

} // namespace zylann::voxel
