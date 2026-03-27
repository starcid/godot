/**************************************************************************/
/*  xess.cpp                                                              */
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

#include "xess.h"

#if defined(VULKAN_ENABLED) || defined(D3D12_ENABLED)

#include "core/os/os.h"
#include "core/string/print_string.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_commons.h"

#ifdef VULKAN_ENABLED
#include "drivers/vulkan/rendering_device_driver_vulkan.h"
#endif

#ifdef D3D12_ENABLED
#include "drivers/d3d12/rendering_device_driver_d3d12.h"
#endif

// ============================================================================
// Intel XeSS SDK headers
//
// Included without XESS_SHARED_LIB so that XESS_API expands to nothing,
// making the function declarations plain `extern "C"` prototypes.  We never
// call those prototypes directly — every XeSS entry point is reached via a
// function pointer loaded at run time by _load_library() — so the linker does
// not need libxess.lib at build time.
// ============================================================================

#include <thirdparty/intel-xess/inc/xess/xess.h>

#ifdef VULKAN_ENABLED
#include <thirdparty/intel-xess/inc/xess/xess_vk.h>
#endif

#ifdef D3D12_ENABLED
#include <thirdparty/intel-xess/inc/xess/xess_d3d12.h>
#endif

// ============================================================================
// Function pointer types for dynamic loading
// ============================================================================

typedef xess_result_t (*PFN_xessDestroyContext)(xess_context_handle_t);
typedef xess_result_t (*PFN_xessGetVersion)(xess_version_t *);

#ifdef VULKAN_ENABLED
typedef xess_result_t (*PFN_xessVKCreateContext)(VkInstance, VkPhysicalDevice, VkDevice, xess_context_handle_t *);
typedef xess_result_t (*PFN_xessVKInit)(xess_context_handle_t, const xess_vk_init_params_t *);
typedef xess_result_t (*PFN_xessVKExecute)(xess_context_handle_t, VkCommandBuffer, const xess_vk_execute_params_t *);
#endif

#ifdef D3D12_ENABLED
typedef xess_result_t (*PFN_xessD3D12CreateContext)(ID3D12Device *, xess_context_handle_t *);
typedef xess_result_t (*PFN_xessD3D12Init)(xess_context_handle_t, const xess_d3d12_init_params_t *);
typedef xess_result_t (*PFN_xessD3D12Execute)(xess_context_handle_t, ID3D12GraphicsCommandList *, const xess_d3d12_execute_params_t *);
#endif

// ============================================================================

using namespace RendererRD;

// Pick the XeSS quality preset that best matches the given scale factor
// (render_width / target_width).  Thresholds are derived from XeSS 1.3+
// nominal scale factors.
static xess_quality_settings_t _select_quality_setting(float p_scale) {
	if (p_scale >= 0.90f) {
		return XESS_QUALITY_SETTING_AA;
	} else if (p_scale >= 0.75f) {
		return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
	} else if (p_scale >= 0.65f) {
		return XESS_QUALITY_SETTING_ULTRA_QUALITY;
	} else if (p_scale >= 0.57f) {
		return XESS_QUALITY_SETTING_QUALITY;
	} else if (p_scale >= 0.47f) {
		return XESS_QUALITY_SETTING_BALANCED;
	} else if (p_scale >= 0.40f) {
		return XESS_QUALITY_SETTING_PERFORMANCE;
	}
	return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
}

#ifdef VULKAN_ENABLED
// Fill an xess_vk_image_view_info from native Vulkan handles.
static xess_vk_image_view_info _make_image_view_info(
		VkImage p_image, VkImageView p_view, VkFormat p_format,
		uint32_t p_width, uint32_t p_height, VkImageAspectFlags p_aspect) {
	xess_vk_image_view_info info = {};
	info.image = p_image;
	info.imageView = p_view;
	info.format = p_format;
	info.width = p_width;
	info.height = p_height;
	info.subresourceRange.aspectMask = p_aspect;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	return info;
}
#endif // VULKAN_ENABLED

// ============================================================================
// XeSSContext
// ============================================================================

XeSSContext::~XeSSContext() {
	if (handle && fn_destroy) {
		((PFN_xessDestroyContext)fn_destroy)((xess_context_handle_t)handle);
		handle = nullptr;
	}
}

// ============================================================================
// XeSSEffect
// ============================================================================

XeSSEffect::XeSSEffect() {
#ifdef D3D12_ENABLED
	if (OS::get_singleton()->get_current_rendering_driver_name() == "d3d12") {
		api_d3d12 = true;
	}
#endif
	_load_library();
}

XeSSEffect::~XeSSEffect() {
	_unload_library();
}

bool XeSSEffect::_load_library() {
	if (library_handle) {
		return true;
	}

	// XeSS is Windows-only. libxess.dll is shared between the Vulkan and D3D12 backends.
	// libxess_dx11.dll is for D3D11 only, which Godot does not have a renderer for.
#if defined(WINDOWS_ENABLED)
	const String library_name = "libxess.dll";
#else
	// XeSS is not supported on this platform.
	return false;
#endif

	Error err = OS::get_singleton()->open_dynamic_library(library_name, library_handle);
	if (err != OK) {
		print_verbose("XeSS: Could not load " + library_name + ". XeSS upscaling will not be available.");
		library_handle = nullptr;
		return false;
	}

#define XESS_LOAD_SYMBOL(sym) \
	do { \
		err = OS::get_singleton()->get_dynamic_library_symbol_handle(library_handle, #sym, fn_##sym); \
		if (err != OK) { \
			print_error("XeSS: Failed to load symbol '" #sym "' from XeSS library. XeSS upscaling will not be available."); \
			_unload_library(); \
			return false; \
		} \
	} while (0)

	// Shared symbols present in libxess.dll for all backends.
	XESS_LOAD_SYMBOL(xessDestroyContext);
	XESS_LOAD_SYMBOL(xessGetVersion);

#ifdef VULKAN_ENABLED
	if (!api_d3d12) {
		XESS_LOAD_SYMBOL(xessVKCreateContext);
		XESS_LOAD_SYMBOL(xessVKInit);
		XESS_LOAD_SYMBOL(xessVKExecute);
	}
#endif

#ifdef D3D12_ENABLED
	if (api_d3d12) {
		XESS_LOAD_SYMBOL(xessD3D12CreateContext);
		XESS_LOAD_SYMBOL(xessD3D12Init);
		XESS_LOAD_SYMBOL(xessD3D12Execute);
	}
#endif

#undef XESS_LOAD_SYMBOL

	// Log the loaded XeSS version.
	xess_version_t version = {};
	((PFN_xessGetVersion)fn_xessGetVersion)(&version);
	print_verbose(vformat("XeSS: Loaded version %d.%d.%d (%s backend).",
			(int)version.major, (int)version.minor, (int)version.patch,
			api_d3d12 ? "D3D12" : "Vulkan"));

	return true;
}

void XeSSEffect::_unload_library() {
	if (library_handle) {
		OS::get_singleton()->close_dynamic_library(library_handle);
		library_handle = nullptr;
		fn_xessDestroyContext = nullptr;
		fn_xessGetVersion = nullptr;
#ifdef VULKAN_ENABLED
		fn_xessVKCreateContext = nullptr;
		fn_xessVKInit = nullptr;
		fn_xessVKExecute = nullptr;
#endif
#ifdef D3D12_ENABLED
		fn_xessD3D12CreateContext = nullptr;
		fn_xessD3D12Init = nullptr;
		fn_xessD3D12Execute = nullptr;
#endif
	}
}

// ============================================================================
// Context creation
// ============================================================================

XeSSContext *XeSSEffect::create_context(Size2i p_internal_size, Size2i p_target_size) {
	ERR_FAIL_COND_V_MSG(!is_available(), nullptr, "XeSS: Library not loaded.");

	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL_V(rd, nullptr);

	using RDC = RenderingDeviceCommons;

	float scale = float(p_internal_size.x) / float(p_target_size.x);
	xess_quality_settings_t quality = _select_quality_setting(scale);
	// Both Vulkan and D3D12 renderers use reverse-Z depth.
	uint32_t init_flags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

#ifdef D3D12_ENABLED
	if (api_d3d12) {
		ID3D12Device *d3d12_device = (ID3D12Device *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_LOGICAL_DEVICE);
		ERR_FAIL_COND_V_MSG(!d3d12_device, nullptr, "XeSS: Failed to get ID3D12Device.");

		xess_context_handle_t xess_handle = nullptr;
		xess_result_t result = ((PFN_xessD3D12CreateContext)fn_xessD3D12CreateContext)(d3d12_device, &xess_handle);
		if (result != XESS_RESULT_SUCCESS) {
			print_error(vformat("XeSS: xessD3D12CreateContext failed with code %d.", (int)result));
			return nullptr;
		}

		xess_d3d12_init_params_t init_params = {};
		init_params.outputResolution = { (uint32_t)p_target_size.x, (uint32_t)p_target_size.y };
		init_params.qualitySetting = quality;
		init_params.initFlags = init_flags;
		init_params.creationNodeMask = 1;
		init_params.visibleNodeMask = 1;
		init_params.pTempBufferHeap = nullptr;
		init_params.pTempTextureHeap = nullptr;
		init_params.pPipelineLibrary = nullptr;

		result = ((PFN_xessD3D12Init)fn_xessD3D12Init)(xess_handle, &init_params);
		if (result != XESS_RESULT_SUCCESS) {
			print_error(vformat("XeSS: xessD3D12Init failed with code %d.", (int)result));
			((PFN_xessDestroyContext)fn_xessDestroyContext)(xess_handle);
			return nullptr;
		}

		XeSSContext *ctx = memnew(XeSSContext);
		ctx->handle = (xess_context_handle_t_opaque *)xess_handle;
		ctx->fn_destroy = fn_xessDestroyContext;
		ctx->internal_size = p_internal_size;
		ctx->target_size = p_target_size;
		return ctx;
	}
#endif // D3D12_ENABLED

#ifdef VULKAN_ENABLED
	{
		VkInstance vk_instance = (VkInstance)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_INSTANCE);
		VkPhysicalDevice vk_physical_device = (VkPhysicalDevice)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE);
		VkDevice vk_device = (VkDevice)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_DEVICE);

		ERR_FAIL_COND_V_MSG(vk_instance == VK_NULL_HANDLE, nullptr, "XeSS: Failed to get VkInstance.");
		ERR_FAIL_COND_V_MSG(vk_physical_device == VK_NULL_HANDLE, nullptr, "XeSS: Failed to get VkPhysicalDevice.");
		ERR_FAIL_COND_V_MSG(vk_device == VK_NULL_HANDLE, nullptr, "XeSS: Failed to get VkDevice.");

		xess_context_handle_t xess_handle = nullptr;
		xess_result_t result = ((PFN_xessVKCreateContext)fn_xessVKCreateContext)(
				vk_instance, vk_physical_device, vk_device, &xess_handle);
		if (result != XESS_RESULT_SUCCESS) {
			print_error(vformat("XeSS: xessVKCreateContext failed with code %d.", (int)result));
			return nullptr;
		}

		xess_vk_init_params_t init_params = {};
		init_params.outputResolution = { (uint32_t)p_target_size.x, (uint32_t)p_target_size.y };
		init_params.qualitySetting = quality;
		init_params.initFlags = init_flags;
		init_params.creationNodeMask = 1;
		init_params.visibleNodeMask = 1;
		init_params.tempBufferHeap = VK_NULL_HANDLE;
		init_params.tempTextureHeap = VK_NULL_HANDLE;
		init_params.pipelineCache = VK_NULL_HANDLE;

		result = ((PFN_xessVKInit)fn_xessVKInit)(xess_handle, &init_params);
		if (result != XESS_RESULT_SUCCESS) {
			print_error(vformat("XeSS: xessVKInit failed with code %d.", (int)result));
			((PFN_xessDestroyContext)fn_xessDestroyContext)(xess_handle);
			return nullptr;
		}

		XeSSContext *ctx = memnew(XeSSContext);
		ctx->handle = (xess_context_handle_t_opaque *)xess_handle;
		ctx->fn_destroy = fn_xessDestroyContext;
		ctx->internal_size = p_internal_size;
		ctx->target_size = p_target_size;
		return ctx;
	}
#endif // VULKAN_ENABLED

	return nullptr;
}

// ============================================================================
// Vulkan callback
// ============================================================================
#ifdef VULKAN_ENABLED

void XeSSEffect::callback_vk(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgs *p_userdata) {
	const RenderingDeviceDriverVulkan::CommandBufferInfo *cmd_info =
			(const RenderingDeviceDriverVulkan::CommandBufferInfo *)(p_cmd_buffer.id);
	VkCommandBuffer vk_cmd = cmd_info->vk_command_buffer;

	xess_vk_execute_params_t exec_params = {};
	exec_params.colorTexture = _make_image_view_info(
			p_userdata->color_image, p_userdata->color_view, p_userdata->color_format,
			p_userdata->input_width, p_userdata->input_height, VK_IMAGE_ASPECT_COLOR_BIT);
	exec_params.depthTexture = _make_image_view_info(
			p_userdata->depth_image, p_userdata->depth_view, p_userdata->depth_format,
			p_userdata->input_width, p_userdata->input_height, VK_IMAGE_ASPECT_DEPTH_BIT);
	exec_params.velocityTexture = _make_image_view_info(
			p_userdata->velocity_image, p_userdata->velocity_view, p_userdata->velocity_format,
			p_userdata->input_width, p_userdata->input_height, VK_IMAGE_ASPECT_COLOR_BIT);
	exec_params.outputTexture = _make_image_view_info(
			p_userdata->output_image, p_userdata->output_view, p_userdata->output_format,
			p_userdata->output_width, p_userdata->output_height, VK_IMAGE_ASPECT_COLOR_BIT);
	exec_params.jitterOffsetX = p_userdata->jitter_x;
	exec_params.jitterOffsetY = p_userdata->jitter_y;
	exec_params.exposureScale = 1.0f;
	exec_params.resetHistory = p_userdata->reset ? 1u : 0u;
	exec_params.inputWidth = p_userdata->input_width;
	exec_params.inputHeight = p_userdata->input_height;

	XeSSEffect *owner = p_userdata->owner;
	xess_context_handle_t xess_handle = (xess_context_handle_t)(p_userdata->xess_handle);

	xess_result_t result = ((PFN_xessVKExecute)owner->fn_xessVKExecute)(xess_handle, vk_cmd, &exec_params);
	if (result != XESS_RESULT_SUCCESS) {
		print_error(vformat("XeSS: xessVKExecute failed with code %d.", (int)result));
	}

	CallbackArgs::free_cb(&p_userdata);
}

#endif // VULKAN_ENABLED

// ============================================================================
// D3D12 callback
// ============================================================================
#ifdef D3D12_ENABLED

void XeSSEffect::callback_d3d12(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgsD3D12 *p_userdata) {
	const RenderingDeviceDriverD3D12::CommandBufferInfo *cmd_info =
			(const RenderingDeviceDriverD3D12::CommandBufferInfo *)(p_cmd_buffer.id);
	ID3D12GraphicsCommandList *cmd_list = cmd_info->cmd_list.Get();

	xess_d3d12_execute_params_t exec_params = {};
	exec_params.pColorTexture = (ID3D12Resource *)p_userdata->color;
	exec_params.pDepthTexture = (ID3D12Resource *)p_userdata->depth;
	exec_params.pVelocityTexture = (ID3D12Resource *)p_userdata->velocity;
	exec_params.pOutputTexture = (ID3D12Resource *)p_userdata->output;
	exec_params.jitterOffsetX = p_userdata->jitter_x;
	exec_params.jitterOffsetY = p_userdata->jitter_y;
	exec_params.exposureScale = 1.0f;
	exec_params.resetHistory = p_userdata->reset ? 1u : 0u;
	exec_params.inputWidth = p_userdata->input_width;
	exec_params.inputHeight = p_userdata->input_height;
	exec_params.pDescriptorHeap = nullptr;
	exec_params.descriptorHeapOffset = 0;

	XeSSEffect *owner = p_userdata->owner;
	xess_context_handle_t xess_handle = (xess_context_handle_t)(p_userdata->xess_handle);

	xess_result_t result = ((PFN_xessD3D12Execute)owner->fn_xessD3D12Execute)(xess_handle, cmd_list, &exec_params);
	if (result != XESS_RESULT_SUCCESS) {
		print_error(vformat("XeSS: xessD3D12Execute failed with code %d.", (int)result));
	}

	CallbackArgsD3D12::free_cb(&p_userdata);
}

#endif // D3D12_ENABLED

// ============================================================================
// upscale() — dispatches to the appropriate backend callback
// ============================================================================

void XeSSEffect::upscale(const Parameters &p_params) {
	ERR_FAIL_COND(!is_available());
	ERR_FAIL_NULL(p_params.context);

	RenderingDevice *rd = RenderingDevice::get_singleton();
	using RDC = RenderingDeviceCommons;

#ifdef D3D12_ENABLED
	if (api_d3d12) {
		CallbackArgsD3D12 *args = d3d12_args_allocator.alloc();
		args->owner = this;
		args->xess_handle = p_params.context->handle;
		args->color = (void *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_TEXTURE, p_params.color);
		args->depth = (void *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_TEXTURE, p_params.depth);
		args->velocity = (void *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_TEXTURE, p_params.velocity);
		args->output = (void *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_TEXTURE, p_params.output);
		args->jitter_x = p_params.jitter.x;
		args->jitter_y = p_params.jitter.y;
		args->reset = p_params.reset_accumulation;
		args->input_width = (uint32_t)p_params.internal_size.x;
		args->input_height = (uint32_t)p_params.internal_size.y;
		args->output_width = (uint32_t)p_params.context->target_size.x;
		args->output_height = (uint32_t)p_params.context->target_size.y;

		RD::CallbackResource res[4] = {
			{ .rid = p_params.color, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.depth, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.velocity, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.output, .usage = RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
		};
		rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback_d3d12, args, VectorView<RD::CallbackResource>(res, 4));
		return;
	}
#endif // D3D12_ENABLED

#ifdef VULKAN_ENABLED
	{
		auto fetch_vk = [&](RID p_rid, VkImage &r_image, VkImageView &r_view, VkFormat &r_format) {
			r_image = (VkImage)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE, p_rid);
			r_view = (VkImageView)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE_VIEW, p_rid);
			r_format = (VkFormat)(uint32_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE_NATIVE_TEXTURE_FORMAT, p_rid);
		};

		CallbackArgs *args = vk_args_allocator.alloc();
		args->owner = this;
		args->xess_handle = p_params.context->handle;

		fetch_vk(p_params.color, args->color_image, args->color_view, args->color_format);
		fetch_vk(p_params.depth, args->depth_image, args->depth_view, args->depth_format);
		fetch_vk(p_params.velocity, args->velocity_image, args->velocity_view, args->velocity_format);
		fetch_vk(p_params.output, args->output_image, args->output_view, args->output_format);

		args->jitter_x = p_params.jitter.x;
		args->jitter_y = p_params.jitter.y;
		args->delta_time = p_params.delta_time;
		args->reset = p_params.reset_accumulation;
		args->input_width = (uint32_t)p_params.internal_size.x;
		args->input_height = (uint32_t)p_params.internal_size.y;
		args->output_width = (uint32_t)p_params.context->target_size.x;
		args->output_height = (uint32_t)p_params.context->target_size.y;

		RD::CallbackResource res[4] = {
			{ .rid = p_params.color, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.depth, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.velocity, .usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ .rid = p_params.output, .usage = RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
		};
		rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback_vk, args, VectorView<RD::CallbackResource>(res, 4));
	}
#endif // VULKAN_ENABLED
}

#endif // VULKAN_ENABLED || D3D12_ENABLED
