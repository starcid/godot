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

#ifdef VULKAN_ENABLED

#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/templates/paged_allocator.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_driver.h"

// Vulkan types needed for callback args (VkImage, VkImageView, VkFormat).
#include <thirdparty/vulkan/include/vulkan/vulkan_core.h>

namespace RendererRD {

// Opaque pointer for the XeSS context handle (avoids pulling in xess.h).
typedef struct _xess_context_handle_t *xess_context_handle_t_opaque;

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

	XeSSContext *create_context(Size2i p_internal_size, Size2i p_target_size);
	void upscale(const Parameters &p_params);

private:
	// Callback args for driver_callback_add.
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
			(*p_args)->owner->args_allocator.free(*p_args);
			*p_args = nullptr;
		}
	};

	static void callback(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgs *p_userdata);

	PagedAllocator<CallbackArgs, true, 16> args_allocator;
	void *library_handle = nullptr;

	// Function pointers loaded from the XeSS shared library.
	void *fn_xessVKCreateContext = nullptr;
	void *fn_xessVKInit = nullptr;
	void *fn_xessVKExecute = nullptr;
	void *fn_xessDestroyContext = nullptr;
	void *fn_xessGetVersion = nullptr;

	bool _load_library();
	void _unload_library();
};

} // namespace RendererRD

#endif // VULKAN_ENABLED
