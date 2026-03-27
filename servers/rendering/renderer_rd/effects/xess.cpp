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

#ifdef VULKAN_ENABLED

#include "core/os/os.h"
#include "core/string/print_string.h"
#include "drivers/vulkan/rendering_device_driver_vulkan.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_commons.h"

// ============================================================================
// Minimal XeSS API type definitions
//
// These declarations mirror the public Intel XeSS SDK C API so that we can
// call into the dynamically loaded library without bundling Intel's
// proprietary SDK headers.  The names, types, and values are taken from the
// publicly documented XeSS 3.0 API.
// ============================================================================

typedef struct _xess_context_handle_t *xess_context_handle_t;

typedef struct _xess_2d_t {
	uint32_t x, y;
} xess_2d_t;

typedef enum _xess_quality_settings_t {
	XESS_QUALITY_SETTING_ULTRA_PERFORMANCE = 100,
	XESS_QUALITY_SETTING_PERFORMANCE = 101,
	XESS_QUALITY_SETTING_BALANCED = 102,
	XESS_QUALITY_SETTING_QUALITY = 103,
	XESS_QUALITY_SETTING_ULTRA_QUALITY = 104,
	XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS = 105,
	XESS_QUALITY_SETTING_AA = 106,
} xess_quality_settings_t;

typedef enum _xess_result_t {
	XESS_RESULT_SUCCESS = 0,
	XESS_RESULT_ERROR_UNSUPPORTED_DEVICE = -1,
	XESS_RESULT_ERROR_UNSUPPORTED_DRIVER = -2,
	XESS_RESULT_ERROR_UNINITIALIZED = -3,
	XESS_RESULT_ERROR_INVALID_ARGUMENT = -4,
	XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY = -5,
	XESS_RESULT_ERROR_DEVICE = -6,
	XESS_RESULT_ERROR_NOT_IMPLEMENTED = -7,
	XESS_RESULT_ERROR_INVALID_CONTEXT = -8,
	XESS_RESULT_ERROR_OPERATION_IN_PROGRESS = -9,
	XESS_RESULT_ERROR_UNSUPPORTED = -10,
	XESS_RESULT_ERROR_CANT_LOAD_LIBRARY = -11,
	XESS_RESULT_ERROR_WRONG_CALL_ORDER = -12,
	XESS_RESULT_ERROR_UNKNOWN = -1000,
} xess_result_t;

typedef enum _xess_init_flags_t {
	XESS_INIT_FLAG_NONE = 0,
	XESS_INIT_FLAG_HIGH_RES_MV = 1 << 0,
	XESS_INIT_FLAG_INVERTED_DEPTH = 1 << 1,
	XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE = 1 << 2,
	XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK = 1 << 3,
	XESS_INIT_FLAG_USE_NDC_VELOCITY = 1 << 4,
	XESS_INIT_FLAG_EXTERNAL_DESCRIPTOR_HEAP = 1 << 5,
	XESS_INIT_FLAG_LDR_INPUT_COLOR = 1 << 6,
	XESS_INIT_FLAG_JITTERED_MV = 1 << 7,
	XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE = 1 << 8,
} xess_init_flags_t;

typedef struct _xess_version_t {
	uint16_t major, minor, patch, reserved;
} xess_version_t;

// Vulkan-specific structs (matching xess_vk.h from the XeSS SDK).
typedef struct _xess_vk_image_view_info {
	VkImageView imageView;
	VkImage image;
	VkImageSubresourceRange subresourceRange;
	VkFormat format;
	unsigned int width;
	unsigned int height;
} xess_vk_image_view_info;

typedef struct _xess_coord_t {
	uint32_t x, y;
} xess_coord_t;

typedef struct _xess_vk_execute_params_t {
	xess_vk_image_view_info colorTexture;
	xess_vk_image_view_info velocityTexture;
	xess_vk_image_view_info depthTexture;
	xess_vk_image_view_info exposureScaleTexture;
	xess_vk_image_view_info responsivePixelMaskTexture;
	xess_vk_image_view_info outputTexture;
	float jitterOffsetX;
	float jitterOffsetY;
	float exposureScale;
	uint32_t resetHistory;
	uint32_t inputWidth;
	uint32_t inputHeight;
	xess_coord_t inputColorBase;
	xess_coord_t inputMotionVectorBase;
	xess_coord_t inputDepthBase;
	xess_coord_t inputResponsiveMaskBase;
	xess_coord_t reserved0;
	xess_coord_t outputColorBase;
} xess_vk_execute_params_t;

typedef struct _xess_vk_init_params_t {
	xess_2d_t outputResolution;
	xess_quality_settings_t qualitySetting;
	uint32_t initFlags;
	uint32_t creationNodeMask;
	uint32_t visibleNodeMask;
	VkDeviceMemory tempBufferHeap;
	uint64_t bufferHeapOffset;
	VkDeviceMemory tempTextureHeap;
	uint64_t textureHeapOffset;
	VkPipelineCache pipelineCache;
} xess_vk_init_params_t;

// Function pointer types.
typedef xess_result_t (*PFN_xessVKCreateContext)(VkInstance, VkPhysicalDevice, VkDevice, xess_context_handle_t *);
typedef xess_result_t (*PFN_xessVKInit)(xess_context_handle_t, const xess_vk_init_params_t *);
typedef xess_result_t (*PFN_xessVKExecute)(xess_context_handle_t, VkCommandBuffer, const xess_vk_execute_params_t *);
typedef xess_result_t (*PFN_xessDestroyContext)(xess_context_handle_t);
typedef xess_result_t (*PFN_xessGetVersion)(xess_version_t *);

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

// ============================================================================
// XeSSContext
// ============================================================================

XeSSContext::~XeSSContext() {
	if (handle && fn_destroy) {
		typedef xess_result_t (*PFN_xessDestroyContext)(xess_context_handle_t);
		((PFN_xessDestroyContext)fn_destroy)((xess_context_handle_t)handle);
		handle = nullptr;
	}
}

// ============================================================================
// XeSSEffect
// ============================================================================

XeSSEffect::XeSSEffect() {
	_load_library();
}

XeSSEffect::~XeSSEffect() {
	_unload_library();
}

bool XeSSEffect::_load_library() {
	if (library_handle) {
		return true;
	}

#if defined(WINDOWS_ENABLED)
	const String library_name = "libxess.dll";
#elif defined(LINUXBSD_ENABLED)
	const String library_name = "libxess.so";
#else
	// XeSS Vulkan backend not supported on this platform.
	return false;
#endif

	Error err = OS::get_singleton()->open_dynamic_library(library_name, library_handle);
	if (err != OK) {
		print_verbose("XeSS: Could not load " + library_name + ". XeSS upscaling will not be available.");
		library_handle = nullptr;
		return false;
	}

#define XESS_LOAD_SYMBOL(sym)                                                                                                              \
	do {                                                                                                                                   \
		err = OS::get_singleton()->get_dynamic_library_symbol_handle(library_handle, #sym, fn_##sym);                                      \
		if (err != OK) {                                                                                                                   \
			print_error("XeSS: Failed to load symbol '" #sym "' from XeSS library. XeSS upscaling will not be available.");               \
			_unload_library();                                                                                                             \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (0)

	XESS_LOAD_SYMBOL(xessVKCreateContext);
	XESS_LOAD_SYMBOL(xessVKInit);
	XESS_LOAD_SYMBOL(xessVKExecute);
	XESS_LOAD_SYMBOL(xessDestroyContext);
	XESS_LOAD_SYMBOL(xessGetVersion);

#undef XESS_LOAD_SYMBOL

	// Log the loaded XeSS version.
	xess_version_t version = {};
	((PFN_xessGetVersion)fn_xessGetVersion)(&version);
	print_verbose(vformat("XeSS: Loaded version %d.%d.%d.", (int)version.major, (int)version.minor, (int)version.patch));

	return true;
}

void XeSSEffect::_unload_library() {
	if (library_handle) {
		OS::get_singleton()->close_dynamic_library(library_handle);
		library_handle = nullptr;
		fn_xessVKCreateContext = nullptr;
		fn_xessVKInit = nullptr;
		fn_xessVKExecute = nullptr;
		fn_xessDestroyContext = nullptr;
		fn_xessGetVersion = nullptr;
	}
}

XeSSContext *XeSSEffect::create_context(Size2i p_internal_size, Size2i p_target_size) {
	ERR_FAIL_COND_V_MSG(!is_available(), nullptr, "XeSS: Library not loaded.");

	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL_V(rd, nullptr);

	// Retrieve native Vulkan handles through the driver-resource API.
	VkInstance vk_instance = (VkInstance)(uintptr_t)rd->get_driver_resource(
			RenderingDeviceCommons::DRIVER_RESOURCE_VULKAN_INSTANCE);
	VkPhysicalDevice vk_physical_device = (VkPhysicalDevice)(uintptr_t)rd->get_driver_resource(
			RenderingDeviceCommons::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE);
	VkDevice vk_device = (VkDevice)(uintptr_t)rd->get_driver_resource(
			RenderingDeviceCommons::DRIVER_RESOURCE_VULKAN_DEVICE);

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

	float scale = float(p_internal_size.x) / float(p_target_size.x);
	xess_quality_settings_t quality = _select_quality_setting(scale);

	// Godot's Vulkan renderer uses reverse-Z depth.
	uint32_t init_flags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

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

void XeSSEffect::callback(RDD *p_driver, RDD::CommandBufferID p_cmd_buffer, CallbackArgs *p_userdata) {
	// Retrieve the native VkCommandBuffer.
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

void XeSSEffect::upscale(const Parameters &p_params) {
	ERR_FAIL_COND(!is_available());
	ERR_FAIL_NULL(p_params.context);

	RenderingDevice *rd = RenderingDevice::get_singleton();
	using RDC = RenderingDeviceCommons;

	// Helper lambda to fetch native Vulkan handles for a texture RID.
	auto fetch_vk = [&](RID p_rid, VkImage &r_image, VkImageView &r_view, VkFormat &r_format) {
		r_image = (VkImage)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE, p_rid);
		r_view = (VkImageView)(uintptr_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE_VIEW, p_rid);
		r_format = (VkFormat)(uint32_t)rd->get_driver_resource(RDC::DRIVER_RESOURCE_VULKAN_IMAGE_NATIVE_TEXTURE_FORMAT, p_rid);
	};

	CallbackArgs *args = args_allocator.alloc();
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
	rd->driver_callback_add((RDD::DriverCallback)XeSSEffect::callback, args, VectorView<RD::CallbackResource>(res, 4));
}

#endif // VULKAN_ENABLED
