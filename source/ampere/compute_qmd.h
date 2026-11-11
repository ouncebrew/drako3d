#pragma once
#include <stdint.h>

namespace maxwell
{

// Compute Queue Metadata, version 03.00
// https://github.com/NVIDIA/open-gpu-doc/blob/master/classes/compute/clc7c0qmd.h
constexpr uint32_t ComputeQmdVersion      = 0;
constexpr uint32_t ComputeQmdMajorVersion = 3;

// NVN uses slots 0-2 for driver data and 3-7 for constbufs
// TODO(S2): user constbufs are still bound at id+2
constexpr uint32_t ComputeQmdFirstUserCbuf = 3;

struct ComputeQmd
{
	// 0x00
	uint32_t outer_put      : 31;
	uint32_t outer_overflow : 1;

	// 0x04
	uint32_t outer_get             : 31;
	uint32_t outer_sticky_overflow : 1;

	// 0x08
	uint32_t inner_get      : 31;
	uint32_t inner_overflow : 1;

	// 0x0C
	uint32_t inner_put             : 31;
	uint32_t inner_sticky_overflow : 1;

	// 0x10
	uint32_t qmd_group_id                         : 6;
	uint32_t sm_global_caching_enable             : 1;
	uint32_t run_cta_in_one_sm_partition          : 1;
	uint32_t is_queue                             : 1;
	uint32_t add_to_head_of_qmd_group_linked_list : 1;
	uint32_t qmd_reserved04a                      : 2;
	uint32_t require_scheduling_pcas              : 1;
	uint32_t qmd_reserved04b                      : 1;
	uint32_t dependence_counter                   : 16;
	uint32_t self_copy_on_completion              : 1;
	uint32_t qmd_reserved04c                      : 1;

	// 0x14
	uint32_t circular_queue_size              : 25;
	uint32_t demote_l2_evict_last             : 1;
	uint32_t invalidate_texture_header_cache  : 1;
	uint32_t invalidate_texture_sampler_cache : 1;
	uint32_t invalidate_texture_data_cache    : 1;
	uint32_t invalidate_shader_data_cache     : 1;
	uint32_t invalidate_instruction_cache     : 1;
	uint32_t invalidate_shader_constant_cache : 1;

	// 0x18
	uint32_t cta_raster_width_resume;

	// 0x1C
	uint32_t cta_raster_height_resume : 16;
	uint32_t cta_raster_depth_resume  : 16;

	// 0x20
	uint32_t program_prefetch_addr_lower_shifted;

	// 0x24
	uint32_t circular_queue_addr_lower;

	// 0x28
	uint32_t circular_queue_addr_upper : 8;
	uint32_t qmd_reserved10a           : 8;
	uint32_t circular_queue_entry_size : 16;

	// 0x2C
	uint32_t cwd_reference_count_id              : 6;
	uint32_t cwd_reference_count_delta_minus_one : 8;
	uint32_t qmd_reserved11a                     : 1;
	uint32_t cwd_reference_count_incr_enable     : 1;
	uint32_t cwd_membar_type                     : 2; // 0=l1_none 1=l1_sysmembar 3=l1_membar
	uint32_t sequentially_run_ctas               : 1;
	uint32_t cwd_reference_count_decr_enable     : 1;
	uint32_t qmd_reserved11b                     : 6;
	uint32_t api_visible_call_limit              : 1; // 0=32 1=no_check
	uint32_t qmd_reserved11c                     : 3;
	uint32_t sampler_index                       : 1; // 0=independently 1=via_header_index
	uint32_t disable_auto_invalidate             : 1;

	// 0x30
	uint32_t cta_raster_width;

	// 0x34
	uint32_t cta_raster_height : 16;
	uint32_t qmd_reserved13a   : 16;

	// 0x38
	uint32_t cta_raster_depth : 16;
	uint32_t qmd_reserved14a  : 16;

	// 0x3C
	uint32_t dependent_qmd0_pointer;

	// 0x40
	uint32_t dependent_qmd0_enable           : 1;
	uint32_t dependent_qmd0_action           : 3;
	uint32_t dependent_qmd0_prefetch         : 1;
	uint32_t dependent_qmd1_enable           : 1;
	uint32_t dependent_qmd1_action           : 3;
	uint32_t dependent_qmd1_prefetch         : 1;
	uint32_t coalesce_waiting_period         : 8;
	uint32_t queue_entries_per_cta_log2      : 5;
	uint32_t occupancy_threshold_shared_mem  : 8;
	uint32_t cta_launch_queue                : 1;

	// 0x44
	uint32_t shared_memory_size             : 18;
	uint32_t min_sm_config_shared_mem_size  : 6;
	uint32_t qmd_reserved17a                : 1;
	uint32_t max_sm_config_shared_mem_size  : 6;
	uint32_t qmd_reserved17b                : 1;

	// 0x48
	uint32_t qmd_version              : 4;
	uint32_t qmd_major_version        : 4;
	uint32_t occupancy_max_shared_mem : 8;
	uint32_t cta_thread_dimension0    : 16;

	// 0x4C
	uint32_t cta_thread_dimension1 : 16;
	uint32_t cta_thread_dimension2 : 16;

	// 0x50
	uint32_t constant_buffer_valid            : 8; // bitmask
	uint32_t register_count_v                 : 9;
	uint32_t target_sm_config_shared_mem_size : 6;
	uint32_t shared_allocation_enable         : 1;
	uint32_t free_cta_slots_empty_sm          : 8;

	// 0x54
	uint32_t sm_disable_mask_lower;

	// 0x58
	uint32_t sm_disable_mask_upper;

	// 0x5C
	uint32_t shader_local_memory_low_size : 24;
	uint32_t qmd_reserved23a              : 3;
	uint32_t barrier_count                : 5;

	// 0x60; 0x70
	struct
	{
		uint32_t address_lower;
		uint32_t control;
		uint32_t payload_lower;
		uint32_t payload_upper;
	} release[2];

	// 0x80
	struct
	{
		// 0x80; 0x88; 0x90; 0x98; 0xA0; 0xA8; 0xB0; 0xB8
		uint32_t addr_lower;

		// 0x84; 0x8C; 0x94; 0x9C; 0xA4; 0xAC; 0xB4; 0xBC
		uint32_t addr_upper     : 17;
		uint32_t prefetch_post  : 1;
		uint32_t invalidate     : 1;
		uint32_t size_shifted4  : 13;
	} constant_buffer[8];

	// 0xC0
	uint32_t program_address_lower;

	// 0xC4
	uint32_t program_address_upper : 17;
	uint32_t qmd_reserved49a       : 15;

	// 0xC8
	uint32_t shader_local_memory_high_size : 24;
	uint32_t qmd_reserved50a               : 8;

	// 0xCC
	uint32_t program_prefetch_addr_upper_shifted : 9;
	uint32_t program_prefetch_size               : 9;
	uint32_t program_prefetch_type               : 2;
	uint32_t qmd_reserved51a                     : 4;
	uint32_t sass_version                        : 8;

	// 0xD0
	uint32_t release2[4];

	// 0xE0
	uint32_t occupancy_threshold_warp     : 8;
	uint32_t occupancy_max_warp           : 8;
	uint32_t occupancy_threshold_register : 8;
	uint32_t occupancy_max_register       : 8;

	// 0xE4
	uint32_t hw_only_inner_get                : 31;
	uint32_t hw_only_require_scheduling_pcas  : 1;

	// 0xE8
	uint32_t hw_only_inner_put : 31;
	uint32_t qmd_reserved58a   : 1;

	// 0xEC
	uint32_t hw_only_span_list_head_index       : 30;
	uint32_t qmd_reserved59a                    : 1;
	uint32_t hw_only_span_list_head_index_valid : 1;

	// 0xF0
	uint32_t hw_only_sked_next_qmd_pointer;

	// 0xF4
	uint32_t hw_only_dependence_counter : 16;
	uint32_t qmd_reserved61a            : 16;

	// 0xF8
	uint32_t debug_id_upper;

	// 0xFC
	uint32_t debug_id_lower;
};

static_assert(sizeof(ComputeQmd)==0x100, "Invalid definition of ComputeQmd");

}
