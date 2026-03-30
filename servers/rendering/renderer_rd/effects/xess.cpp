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

#if defined(WINDOWS_ENABLED)

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

#if !defined(_MSC_VER)
// With MinGW, GUID symbols are only declared extern in D3D12 headers.
// Including dxguids.h provides the actual definitions needed by the linker.
#include <thirdparty/directx_headers/include/dxguids/dxguids.h>

#include <guiddef.h>
#endif
#endif

// ============================================================================
// Function pointer types for dynamic loading
// ============================================================================

typedef xess_result_t (*PFN_xessDestroyContext)(xess_context_handle_t);
typedef xess_result_t (*PFN_xessGetVersion)(xess_version_t *);
typedef xess_result_t (*PFN_xessSetVelocityScale)(xess_context_handle_t, float, float);
typedef xess_result_t (*PFN_xessGetOptimalInputResolution)(xess_context_handle_t, const xess_2d_t *, xess_quality_settings_t, xess_2d_t *, xess_2d_t *, xess_2d_t *);

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
		// xessDestroyContext releases internal GPU resources; the GPU must be idle first.
		RenderingDevice *rd = RenderingDevice::get_singleton();
		if (rd) {
			using RDC = RenderingDeviceCommons;
#ifdef D3D12_ENABLED
			if (api_d3d12) {
				ID3D12Device *d3d12_device = (ID3D12Device *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_LOGICAL_DEVICE);
				ID3D12CommandQueue *d3d12_queue = (ID3D12CommandQueue *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_COMMAND_QUEUE);
				if (d3d12_device && d3d12_queue) {
					Microsoft::WRL::ComPtr<ID3D12Fence> fence;
					if (SUCCEEDED(d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))) {
						HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
						if (event) {
							d3d12_queue->Signal(fence.Get(), 1);
							fence->SetEventOnCompletion(1, event);
							WaitForSingleObject(event, INFINITE);
							CloseHandle(event);
						}
					}
				}
			}
#endif // D3D12_ENABLED
#ifdef VULKAN_ENABLED
			if (!api_d3d12) {
				VkDevice vk_device = (VkDevice)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_DEVICE);
				if (vk_device != VK_NULL_HANDLE) {
					vkDeviceWaitIdle(vk_device);
				}
			}
#endif // VULKAN_ENABLED
		}
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
	XESS_LOAD_SYMBOL(xessSetVelocityScale);
	XESS_LOAD_SYMBOL(xessGetOptimalInputResolution);

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
		fn_xessSetVelocityScale = nullptr;
		fn_xessGetOptimalInputResolution = nullptr;
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

XeSSContext *XeSSEffect::create_context() {
	ERR_FAIL_COND_V_MSG(!is_available(), nullptr, "XeSS: Library not loaded.");

	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL_V(rd, nullptr);

	using RDC = RenderingDeviceCommons;

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

		XeSSContext *ctx = memnew(XeSSContext);
		ctx->handle = (xess_context_handle_t_opaque *)xess_handle;
		ctx->fn_destroy = fn_xessDestroyContext;
		ctx->api_d3d12 = true;
		return ctx;
	}
#endif // D3D12_ENABLED

#ifdef VULKAN_ENABLED
	if (!api_d3d12) {
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

		XeSSContext *ctx = memnew(XeSSContext);
		ctx->handle = (xess_context_handle_t_opaque *)xess_handle;
		ctx->fn_destroy = fn_xessDestroyContext;
		ctx->api_d3d12 = false;
		return ctx;
	}
#endif // VULKAN_ENABLED

	ERR_FAIL_V_MSG(nullptr, "XeSS: create_context called but no matching runtime API backend (neither D3D12 nor Vulkan is active).");
}

// ============================================================================
// Shared init helper — waits for GPU idle, calls xessInit, updates context.
// ============================================================================

Size2i XeSSEffect::_xess_do_init(XeSSContext *p_ctx, XeSSQuality p_quality, Size2i p_target_size) {
	ERR_FAIL_NULL_V(p_ctx, Size2i());
	ERR_FAIL_NULL_V_MSG(p_ctx->handle, Size2i(), "XeSS: context handle is null.");
	ERR_FAIL_COND_V_MSG(!is_available(), Size2i(), "XeSS: Library not loaded.");

	xess_context_handle_t xess_handle = (xess_context_handle_t)p_ctx->handle;
	xess_quality_settings_t quality = (xess_quality_settings_t)p_quality;

	// Query the optimal input (render) resolution for the requested quality setting.
	ERR_FAIL_NULL_V_MSG(fn_xessGetOptimalInputResolution, Size2i(), "XeSS: fn_xessGetOptimalInputResolution is null.");
	xess_2d_t output_res = { (uint32_t)p_target_size.x, (uint32_t)p_target_size.y };
	xess_2d_t input_optimal = {}, input_min = {}, input_max = {};
	xess_result_t result = ((PFN_xessGetOptimalInputResolution)fn_xessGetOptimalInputResolution)(
			xess_handle, &output_res, quality, &input_optimal, &input_min, &input_max);
	if (result != XESS_RESULT_SUCCESS) {
		print_error(vformat("XeSS: xessGetOptimalInputResolution failed with code %d.", (int)result));
		return Size2i();
	}
	Size2i internal_size = Size2i((int)input_optimal.x, (int)input_optimal.y);

	// Both Vulkan and D3D12 renderers use reverse-Z depth.
	uint32_t init_flags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL_V(rd, Size2i());
	using RDC = RenderingDeviceCommons;

#ifdef D3D12_ENABLED
	if (api_d3d12) {
		ID3D12Device *d3d12_device = (ID3D12Device *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_LOGICAL_DEVICE);
		ERR_FAIL_COND_V_MSG(!d3d12_device, Size2i(), "XeSS: Failed to get ID3D12Device.");

		// xessD3D12Init allocates internal GPU resources; the command queue must be idle.
		ID3D12CommandQueue *d3d12_queue = (ID3D12CommandQueue *)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_COMMAND_QUEUE);
		if (d3d12_queue) {
			Microsoft::WRL::ComPtr<ID3D12Fence> fence;
			if (SUCCEEDED(d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))) {
				HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (event) {
					d3d12_queue->Signal(fence.Get(), 1);
					fence->SetEventOnCompletion(1, event);
					WaitForSingleObject(event, INFINITE);
					CloseHandle(event);
				}
			}
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
			return Size2i();
		}

		ERR_FAIL_NULL_V_MSG(fn_xessSetVelocityScale, Size2i(), "XeSS: fn_xessSetVelocityScale is null.");
		((PFN_xessSetVelocityScale)fn_xessSetVelocityScale)(xess_handle, float(internal_size.x), float(internal_size.y));

		p_ctx->internal_size = internal_size;
		p_ctx->target_size = p_target_size;
		return internal_size;
	}
#endif // D3D12_ENABLED

#ifdef VULKAN_ENABLED
	if (!api_d3d12) {
		VkDevice vk_device = (VkDevice)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_DEVICE);
		ERR_FAIL_COND_V_MSG(vk_device == VK_NULL_HANDLE, Size2i(), "XeSS: Failed to get VkDevice.");

		// xessVKInit allocates internal GPU resources; all pending GPU work must be done first.
		vkDeviceWaitIdle(vk_device);

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
			return Size2i();
		}

		ERR_FAIL_NULL_V_MSG(fn_xessSetVelocityScale, Size2i(), "XeSS: fn_xessSetVelocityScale is null.");
		((PFN_xessSetVelocityScale)fn_xessSetVelocityScale)(xess_handle, float(internal_size.x), float(internal_size.y));

		p_ctx->internal_size = internal_size;
		p_ctx->target_size = p_target_size;
		return internal_size;
	}
#endif // VULKAN_ENABLED

	ERR_FAIL_V_MSG(Size2i(), "XeSS: _xess_do_init called but no matching runtime API backend.");
}

// ============================================================================
// Public init interfaces
// ============================================================================

XeSSQuality XeSSEffect::quality_for_ratio(XeSSContext *p_ctx, Size2i p_target_size, float p_scale) const {
	ERR_FAIL_NULL_V(p_ctx, XESS_QUALITY_BALANCED);
	ERR_FAIL_NULL_V(p_ctx->handle, XESS_QUALITY_BALANCED);
	ERR_FAIL_COND_V_MSG(p_target_size.x <= 0 || p_target_size.y <= 0, XESS_QUALITY_BALANCED,
			"XeSS: quality_for_ratio requires a non-zero target size.");
	ERR_FAIL_NULL_V_MSG(fn_xessGetOptimalInputResolution, XESS_QUALITY_BALANCED,
			"XeSS: fn_xessGetOptimalInputResolution is null.");

	static const xess_quality_settings_t k_presets[] = {
		XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,
		XESS_QUALITY_SETTING_PERFORMANCE,
		XESS_QUALITY_SETTING_BALANCED,
		XESS_QUALITY_SETTING_QUALITY,
		XESS_QUALITY_SETTING_ULTRA_QUALITY,
		XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS,
		XESS_QUALITY_SETTING_AA,
	};

	xess_context_handle_t xess_handle = (xess_context_handle_t)p_ctx->handle;
	xess_2d_t output_res = { (uint32_t)p_target_size.x, (uint32_t)p_target_size.y };

	float best_diff = 1e30f; // sentinel; any valid ratio diff will be smaller
	xess_quality_settings_t best = XESS_QUALITY_SETTING_BALANCED;

	for (xess_quality_settings_t preset : k_presets) {
		xess_2d_t input_optimal = {}, input_min = {}, input_max = {};
		xess_result_t res = ((PFN_xessGetOptimalInputResolution)fn_xessGetOptimalInputResolution)(
				xess_handle, &output_res, preset, &input_optimal, &input_min, &input_max);
		if (res != XESS_RESULT_SUCCESS) {
			print_verbose(vformat("XeSS: quality_for_ratio: xessGetOptimalInputResolution failed for preset %d (code %d).",
					(int)preset, (int)res));
			continue;
		}
		// Compute what scale ratio this preset produces and pick the closest to p_scale.
		float preset_ratio = float(input_optimal.x) / float(p_target_size.x);
		float diff = Math::abs(preset_ratio - p_scale);
		if (diff < best_diff) {
			best_diff = diff;
			best = preset;
		}
	}

	return (XeSSQuality)(int)best;
}

Size2i XeSSEffect::init_by_ratio(XeSSContext *p_ctx, float p_upscale_ratio, Size2i p_target_size) {
	ERR_FAIL_NULL_V(p_ctx, Size2i());
	ERR_FAIL_COND_V_MSG(p_upscale_ratio <= 0.0f, Size2i(), "XeSS: upscale_ratio must be positive.");

	// Select the quality preset that best matches the requested scale factor using the SDK.
	XeSSQuality quality = quality_for_ratio(p_ctx, p_target_size, p_upscale_ratio);
	return _xess_do_init(p_ctx, quality, p_target_size);
}

Size2i XeSSEffect::init_by_quality(XeSSContext *p_ctx, XeSSQuality p_quality, Size2i p_target_size) {
	ERR_FAIL_NULL_V(p_ctx, Size2i());
	return _xess_do_init(p_ctx, p_quality, p_target_size);
}

// ============================================================================
// Vulkan callback
// ============================================================================
#ifdef VULKAN_ENABLED

void XeSSEffect::callback_vk(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgs *p_userdata) {
	XeSSEffect *owner = p_userdata->owner;
	xess_context_handle_t xess_handle = (xess_context_handle_t)(p_userdata->xess_handle);
	VkCommandBuffer vk_cmd = RenderingDeviceDriverVulkan::command_buffer_vk(p_cmd_buffer);

	if (owner->fn_xessVKExecute == nullptr) {
		print_error("XeSS: fn_xessVKExecute is null; cannot execute upscaling.");
		CallbackArgs::free_cb(&p_userdata);
		return;
	}
	if (xess_handle == nullptr) {
		print_error("XeSS: xess_context_handle is null in VK execute callback; cannot execute upscaling.");
		CallbackArgs::free_cb(&p_userdata);
		return;
	}
	if (vk_cmd == VK_NULL_HANDLE) {
		print_error("XeSS: VkCommandBuffer is null in VK execute callback; cannot execute upscaling.");
		CallbackArgs::free_cb(&p_userdata);
		return;
	}

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
	XeSSEffect *owner = p_userdata->owner;
	xess_context_handle_t xess_handle = (xess_context_handle_t)(p_userdata->xess_handle);
	ID3D12GraphicsCommandList *cmd_list = RenderingDeviceDriverD3D12::command_buffer_d3d12(p_cmd_buffer);

	if (owner->fn_xessD3D12Execute == nullptr) {
		print_error("XeSS: fn_xessD3D12Execute is null; cannot execute upscaling.");
		CallbackArgsD3D12::free_cb(&p_userdata);
		return;
	}
	if (xess_handle == nullptr) {
		print_error("XeSS: xess_context_handle is null in D3D12 execute callback; cannot execute upscaling.");
		CallbackArgsD3D12::free_cb(&p_userdata);
		return;
	}
	if (cmd_list == nullptr) {
		print_error("XeSS: ID3D12GraphicsCommandList is null in D3D12 execute callback; cannot execute upscaling.");
		CallbackArgsD3D12::free_cb(&p_userdata);
		return;
	}

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
	ERR_FAIL_NULL_MSG(p_params.context->handle, "XeSS: xess_context_handle is null; upscale skipped.");
	ERR_FAIL_COND_MSG(p_params.color.is_null(), "XeSS: color texture RID is invalid; upscale skipped.");
	ERR_FAIL_COND_MSG(p_params.depth.is_null(), "XeSS: depth texture RID is invalid; upscale skipped.");
	ERR_FAIL_COND_MSG(p_params.velocity.is_null(), "XeSS: velocity texture RID is invalid; upscale skipped.");
	ERR_FAIL_COND_MSG(p_params.output.is_null(), "XeSS: output texture RID is invalid; upscale skipped.");

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

		if (!args->color || !args->depth || !args->velocity || !args->output) {
			print_error(vformat("XeSS: D3D12 texture resource is null (color=0x%x depth=0x%x velocity=0x%x output=0x%x); upscale skipped.",
					(uint64_t)(uintptr_t)args->color, (uint64_t)(uintptr_t)args->depth,
					(uint64_t)(uintptr_t)args->velocity, (uint64_t)(uintptr_t)args->output));
			d3d12_args_allocator.free(args);
			return;
		}

		args->jitter_x = p_params.jitter.x;
		args->jitter_y = p_params.jitter.y;
		args->reset = p_params.reset_accumulation;
		args->input_width = (uint32_t)p_params.internal_size.x;
		args->input_height = (uint32_t)p_params.internal_size.y;
		args->output_width = (uint32_t)p_params.context->target_size.x;
		args->output_height = (uint32_t)p_params.context->target_size.y;

		RD::CallbackResource res[4] = {
			{ p_params.color, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.depth, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.velocity, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.output, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
		};
		rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback_d3d12, args, VectorView<RD::CallbackResource>(res, 4));
		return;
	}
#endif // D3D12_ENABLED

#ifdef VULKAN_ENABLED
	if (!api_d3d12) {
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

		if (args->color_image == VK_NULL_HANDLE || args->depth_image == VK_NULL_HANDLE ||
				args->velocity_image == VK_NULL_HANDLE || args->output_image == VK_NULL_HANDLE) {
			print_error(vformat("XeSS: VkImage handle is null (color=0x%x depth=0x%x velocity=0x%x output=0x%x); upscale skipped.",
					(uint64_t)(uintptr_t)args->color_image, (uint64_t)(uintptr_t)args->depth_image,
					(uint64_t)(uintptr_t)args->velocity_image, (uint64_t)(uintptr_t)args->output_image));
			vk_args_allocator.free(args);
			return;
		}

		args->jitter_x = p_params.jitter.x;
		args->jitter_y = p_params.jitter.y;
		args->delta_time = p_params.delta_time;
		args->reset = p_params.reset_accumulation;
		args->input_width = (uint32_t)p_params.internal_size.x;
		args->input_height = (uint32_t)p_params.internal_size.y;
		args->output_width = (uint32_t)p_params.context->target_size.x;
		args->output_height = (uint32_t)p_params.context->target_size.y;

		RD::CallbackResource res[4] = {
			{ p_params.color, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.depth, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.velocity, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE },
			{ p_params.output, RD::CALLBACK_RESOURCE_TYPE_TEXTURE, RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE },
		};
		rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback_vk, args, VectorView<RD::CallbackResource>(res, 4));
		return;
	}
#endif // VULKAN_ENABLED

	ERR_FAIL_MSG("XeSS: upscale called but no matching runtime API backend (neither D3D12 nor Vulkan is active).");
}

#endif // WINDOWS_ENABLED
