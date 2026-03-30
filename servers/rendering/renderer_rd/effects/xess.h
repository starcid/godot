/**************************************************************************/
/*  xess.h                                                                */
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

#pragma once

#if defined(WINDOWS_ENABLED)

#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/templates/paged_allocator.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_driver.h"

#ifdef VULKAN_ENABLED
// Vulkan types needed for callback args (VkImage, VkImageView, VkFormat).
#include <vulkan/vulkan_core.h>
#endif

namespace RendererRD {

// Opaque pointer for the XeSS context handle (avoids pulling in xess.h).
typedef struct _xess_context_handle_t *xess_context_handle_t_opaque;

// XeSS quality preset, mirrors xess_quality_settings_t from the XeSS SDK.
// Values are kept identical so they can be cast directly without a mapping table.
enum XeSSQuality {
	XESS_QUALITY_ULTRA_PERFORMANCE = 100,
	XESS_QUALITY_PERFORMANCE = 101,
	XESS_QUALITY_BALANCED = 102,
	XESS_QUALITY_QUALITY = 103,
	XESS_QUALITY_ULTRA_QUALITY = 104,
	XESS_QUALITY_ULTRA_QUALITY_PLUS = 105,
	XESS_QUALITY_AA = 106,
};

// Opaque context holding the per-viewport XeSS handle.
struct XeSSContext {
	xess_context_handle_t_opaque *handle = nullptr; // xess_context_handle_t
	void *fn_destroy = nullptr; // PFN_xessDestroyContext — stored to avoid dependency on XeSSEffect
	Size2i internal_size;
	Size2i target_size;

	XeSSContext() = default;
	~XeSSContext();
};

class XeSSEffect {
public:
	struct Parameters {
		XeSSContext *context = nullptr;
		Size2i internal_size;
		RID color;
		RID depth;
		RID velocity;
		RID output;
		Vector2 jitter;
		float delta_time = 0.0f;
		bool reset_accumulation = false;
	};

	// Returns true if the XeSS shared library was loaded successfully.
	bool is_available() const { return library_handle != nullptr; }

	XeSSEffect();
	~XeSSEffect();

	// Creates a raw XeSS context handle. No GPU resources are allocated yet.
	// Call init_by_ratio() or init_by_quality() afterwards (and whenever the
	// scale or quality needs to change at runtime).
	XeSSContext *create_context();

	// Returns the XeSS quality preset that best matches p_scale (render/target ratio)
	// by querying xessGetOptimalInputResolution for every preset and picking the one
	// whose optimal input ratio is closest to p_scale.  Requires a valid context and
	// a non-zero target size.  Falls back to QUALITY on error.
	XeSSQuality quality_for_ratio(XeSSContext *p_ctx, Size2i p_target_size, float p_scale) const;

	// Initialise (or re-initialise) a context by specifying the render-to-output
	// scale factor (render_width / target_width).  Automatically selects the XeSS
	// quality preset that best matches the requested ratio (via xessGetOptimalInputResolution),
	// waits for GPU idle, and calls xessInit.  Returns the recommended internal
	// (render) resolution, or Size2i(0,0) on failure.
	Size2i init_by_ratio(XeSSContext *p_ctx, float p_upscale_ratio, Size2i p_target_size);

	// Initialise (or re-initialise) a context with an explicit quality preset.
	// Queries the recommended input resolution, waits for GPU idle, and calls
	// xessInit.  Returns the recommended internal (render) resolution, or
	// Size2i(0,0) on failure.
	Size2i init_by_quality(XeSSContext *p_ctx, XeSSQuality p_quality, Size2i p_target_size);

	void upscale(const Parameters &p_params);

private:
#ifdef VULKAN_ENABLED
	// Callback args for the Vulkan execution path.
	struct CallbackArgs {
		XeSSEffect *owner = nullptr;
		xess_context_handle_t_opaque *xess_handle = nullptr;
		// Color texture components.
		VkImage color_image = VK_NULL_HANDLE;
		VkImageView color_view = VK_NULL_HANDLE;
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		// Depth texture components.
		VkImage depth_image = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		// Velocity texture components.
		VkImage velocity_image = VK_NULL_HANDLE;
		VkImageView velocity_view = VK_NULL_HANDLE;
		VkFormat velocity_format = VK_FORMAT_UNDEFINED;
		// Output texture components.
		VkImage output_image = VK_NULL_HANDLE;
		VkImageView output_view = VK_NULL_HANDLE;
		VkFormat output_format = VK_FORMAT_UNDEFINED;
		// Per-frame execution parameters.
		float jitter_x = 0.0f;
		float jitter_y = 0.0f;
		float delta_time = 0.0f;
		bool reset = false;
		uint32_t input_width = 0;
		uint32_t input_height = 0;
		uint32_t output_width = 0;
		uint32_t output_height = 0;

		static void free_cb(CallbackArgs **p_args) {
			(*p_args)->owner->vk_args_allocator.free(*p_args);
			*p_args = nullptr;
		}
	};

	static void callback_vk(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgs *p_userdata);
	PagedAllocator<CallbackArgs, true, 16> vk_args_allocator;
#endif // VULKAN_ENABLED

#ifdef D3D12_ENABLED
	// Callback args for the D3D12 execution path.
	// ID3D12Resource* stored as void* to avoid including Windows D3D12 headers here.
	struct CallbackArgsD3D12 {
		XeSSEffect *owner = nullptr;
		xess_context_handle_t_opaque *xess_handle = nullptr;
		void *color = nullptr; // ID3D12Resource*
		void *depth = nullptr; // ID3D12Resource*
		void *velocity = nullptr; // ID3D12Resource*
		void *output = nullptr; // ID3D12Resource*
		float jitter_x = 0.0f;
		float jitter_y = 0.0f;
		bool reset = false;
		uint32_t input_width = 0;
		uint32_t input_height = 0;
		uint32_t output_width = 0;
		uint32_t output_height = 0;

		static void free_cb(CallbackArgsD3D12 **p_args) {
			(*p_args)->owner->d3d12_args_allocator.free(*p_args);
			*p_args = nullptr;
		}
	};

	static void callback_d3d12(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgsD3D12 *p_userdata);
	PagedAllocator<CallbackArgsD3D12, true, 16> d3d12_args_allocator;
#endif // D3D12_ENABLED

	void *library_handle = nullptr;
	bool api_d3d12 = false; // true when running under the D3D12 renderer

	// Shared function pointers (Vulkan and D3D12 share the same libxess.dll).
	void *fn_xessDestroyContext = nullptr;
	void *fn_xessGetVersion = nullptr;
	void *fn_xessSetVelocityScale = nullptr;
	void *fn_xessGetOptimalInputResolution = nullptr;

#ifdef VULKAN_ENABLED
	// Vulkan-specific function pointers.
	void *fn_xessVKCreateContext = nullptr;
	void *fn_xessVKInit = nullptr;
	void *fn_xessVKExecute = nullptr;
#endif

#ifdef D3D12_ENABLED
	// D3D12-specific function pointers.
	void *fn_xessD3D12CreateContext = nullptr;
	void *fn_xessD3D12Init = nullptr;
	void *fn_xessD3D12Execute = nullptr;
#endif

	bool _load_library();
	void _unload_library();

	// Shared init implementation: waits for GPU idle, queries optimal input size via
	// xessGetOptimalInputResolution, calls the backend-specific xessInit, sets velocity
	// scale, and updates the context's cached sizes.  Returns the recommended internal
	// resolution, or Size2i(0,0) on failure.
	Size2i _xess_do_init(XeSSContext *p_ctx, XeSSQuality p_quality, Size2i p_target_size);
};

} // namespace RendererRD

#endif // WINDOWS_ENABLED
