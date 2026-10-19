/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <getopt.h>
#include <poor_poll.h>
#include <poor_stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-client.h>

#include "engine.h"
#include "scene.h"
#include "tracker.h"
#include "xdg-shell-client-protocol.h"

/*
 * Draws the test scene with Vulkan, redrawing only what the acquired image is missing. It draws into a Wayland window,
 * a headless swapchain, or offscreen images that the harness engine hands out. With --verify every frame is compared
 * with a frame drawn from scratch, and --flash tints what was redrawn.
 */

static const uint32_t vertex_code[] =
#include "box.vert.spv.h"
;

static const uint32_t fragment_code[] =
#include "box.frag.spv.h"
;

constexpr size_t FRAMES = 2;
constexpr uint32_t FLASH = 0x40ff00ff;

#define check(call) checked(call, #call, __LINE__)

enum target { TARGET_WAYLAND, TARGET_HEADLESS, TARGET_OFFSCREEN };

enum mode { MODE_SOLID, MODE_BOARD };

struct options {
	enum target target;
	struct tracker_options tracker;
	enum engine_order order;
	VkPresentModeKHR present;
	uint32_t images, frames, resize, seed, device;
	int32_t width, height;
	bool verify, flash;
};

struct draw {
	int32_t rect[4];
	int32_t size[2];
	uint32_t colors[2];
	uint32_t mode;
};

struct image {
	VkImage image;
	VkImageView view;
	VkDeviceMemory memory;
	VkImageLayout layout;
	VkSemaphore rendered;
};

struct app {
	struct options options;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	int32_t configured_width, configured_height;
	bool configured, closed;

	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	uint32_t family;
	VkQueue queue;
	VkSurfaceKHR window;
	VkSwapchainKHR swapchain;
	bool incremental, stale;
	VkFormat format;
	int32_t width, height;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkCommandPool pool;
	VkCommandBuffer commands[FRAMES];
	VkFence fences[FRAMES];
	VkSemaphore acquired[FRAMES];
	size_t count;
	struct image images[TRACKER_MAX_BUFFERS];
	struct image reference;
	VkBuffer readback;
	VkDeviceMemory readback_memory;
	uint32_t *mapped;

	struct scene scene;
	struct engine engine;
	struct tracker tracker;
	region flashed;
	bool resized;
	uint64_t frame, resized_at;
	struct {
		uint64_t frames, wrong, wholes, drawn, pixels, rects;
	} stats;
};

static void checked(VkResult result, const char *call, int line)
{
	if (result >= VK_SUCCESS)
		return;
	printerrln("vk-damage:", line, ": ", call, " failed with ", (int)result);
	exit(EXIT_FAILURE);
}

[[noreturn]] static void fail(const char *message)
{
	printerrln("vk-damage: ", message);
	exit(EXIT_FAILURE);
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
			    uint32_t version)
{
	struct app *app = data;

	if (!strcmp(interface, wl_compositor_interface.name) && version >= 4)
		app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	if (!strcmp(interface, xdg_wm_base_interface.name))
		app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct app *app = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	app->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
			       struct wl_array *states)
{
	struct app *app = data;

	if (width > 0 && height > 0) {
		app->configured_width = width;
		app->configured_height = height;
	}
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct app *app = data;

	app->closed = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void init_wayland(struct app *app)
{
	app->display = wl_display_connect(nullptr);
	if (!app->display)
		fail("no Wayland display");
	app->registry = wl_display_get_registry(app->display);
	wl_registry_add_listener(app->registry, &registry_listener, app);
	wl_display_roundtrip(app->display);
	if (!app->compositor || !app->wm_base)
		fail("the compositor lacks wl_compositor 4 or xdg_wm_base");
	xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
	app->surface = wl_compositor_create_surface(app->compositor);
	app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
	xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
	app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
	xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
	xdg_toplevel_set_title(app->toplevel, "vk-damage");
	xdg_toplevel_set_app_id(app->toplevel, "vk-damage");
	app->configured_width = app->options.width;
	app->configured_height = app->options.height;
	wl_surface_commit(app->surface);
	while (!app->configured && wl_display_dispatch(app->display) != -1)
		;
}

static void dispatch(struct app *app)
{
	struct pollfd fds[] = { { .fd = wl_display_get_fd(app->display), .events = POLLIN } };

	while (wl_display_prepare_read(app->display))
		wl_display_dispatch_pending(app->display);
	wl_display_flush(app->display);
	if (poll_array(fds, 0) > 0)
		wl_display_read_events(app->display);
	else
		wl_display_cancel_read(app->display);
	wl_display_dispatch_pending(app->display);
}

static bool has_extension(VkPhysicalDevice physical, const char *name)
{
	uint32_t count;

	check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
	VkExtensionProperties extensions[count + 1];
	check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions));
	foreach_array_const_ref(arrview_first(count, extensions), extension)
		if (!strcmp(extension->extensionName, name))
			return true;
	return false;
}

/* Whether the device supports Vulkan 1.3 and has a graphics queue that can present to our surface */
static bool suitable(struct app *app, VkPhysicalDevice physical, uint32_t *family)
{
	VkPhysicalDeviceProperties properties;
	uint32_t count;

	vkGetPhysicalDeviceProperties(physical, &properties);
	if (properties.apiVersion < VK_API_VERSION_1_3)
		return false;
	if (app->options.target != TARGET_OFFSCREEN && !has_extension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		return false;
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
	VkQueueFamilyProperties families[count + 1];
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
	foreach_array_index(arrview_first(count, families), i) {
		VkBool32 presents = app->options.target == TARGET_OFFSCREEN;

		if (!presents)
			check(vkGetPhysicalDeviceSurfaceSupportKHR(physical, (uint32_t)i, app->window, &presents));
		if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presents) {
			*family = (uint32_t)i;
			return true;
		}
	}
	return false;
}

static void pick_device(struct app *app)
{
	VkPhysicalDeviceProperties properties;
	uint32_t count, family;

	check(vkEnumeratePhysicalDevices(app->instance, &count, nullptr));
	VkPhysicalDevice physicals[count + 1];
	check(vkEnumeratePhysicalDevices(app->instance, &count, physicals));
	app->physical = VK_NULL_HANDLE;
	foreach_array_index(arrview_first(count, physicals), i) {
		if ((app->options.device != UINT32_MAX && i != app->options.device) || !suitable(app, physicals[i], &family))
			continue;
		vkGetPhysicalDeviceProperties(physicals[i], &properties);
		if (!app->physical || properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
			app->physical = physicals[i];
			app->family = family;
		}
		if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
			break;
	}
	if (!app->physical)
		fail("no Vulkan 1.3 device that can draw there");
	vkGetPhysicalDeviceProperties(app->physical, &properties);
	println("vk-damage: ", properties.deviceName);
}

static void pick_format(struct app *app)
{
	uint32_t count;

	app->format = VK_FORMAT_B8G8R8A8_UNORM;
	if (app->options.target == TARGET_OFFSCREEN)
		return;
	check(vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->window, &count, nullptr));
	VkSurfaceFormatKHR formats[count + 1];
	check(vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->window, &count, formats));
	app->format = formats[0].format;
	foreach_array_const_ref(arrview_first(count, formats), format)
		if (format->format == VK_FORMAT_B8G8R8A8_UNORM || format->format == VK_FORMAT_R8G8B8A8_UNORM)
			app->format = format->format;
}

static void create_pipeline(struct app *app)
{
	VkShaderModule vertex, fragment;
	const VkPushConstantRange range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.size = sizeof(struct draw),
	};

	check(vkCreateShaderModule(app->device, &(VkShaderModuleCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(vertex_code),
		.pCode = vertex_code,
	}, nullptr, &vertex));
	check(vkCreateShaderModule(app->device, &(VkShaderModuleCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(fragment_code),
		.pCode = fragment_code,
	}, nullptr, &fragment));
	check(vkCreatePipelineLayout(app->device, &(VkPipelineLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &range,
	}, nullptr, &app->layout));
	check(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &(VkGraphicsPipelineCreateInfo){
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &(VkPipelineRenderingCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &app->format,
		},
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]){
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vertex,
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = fragment,
				.pName = "main",
			},
		},
		.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		},
		.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
		},
		.pViewportState = &(VkPipelineViewportStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		},
		.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_NONE,
			.lineWidth = 1,
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &(VkPipelineColorBlendAttachmentState){
				.blendEnable = VK_TRUE,
				.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
				.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.colorBlendOp = VK_BLEND_OP_ADD,
				.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.alphaBlendOp = VK_BLEND_OP_ADD,
				.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
			},
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = (VkDynamicState[]){ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
		},
		.layout = app->layout,
	}, nullptr, &app->pipeline));
	vkDestroyShaderModule(app->device, vertex, nullptr);
	vkDestroyShaderModule(app->device, fragment, nullptr);
}

static void init_vulkan(struct app *app)
{
	const char *wayland[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME };
	const char *headless[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME };
	const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME };

	check(vkCreateInstance(&(VkInstanceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &(VkApplicationInfo){
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "vk-damage",
			.apiVersion = VK_API_VERSION_1_3,
		},
		.enabledExtensionCount = app->options.target == TARGET_OFFSCREEN ? 0 : 2,
		.ppEnabledExtensionNames = app->options.target == TARGET_WAYLAND ? wayland : headless,
	}, nullptr, &app->instance));
	if (app->options.target == TARGET_WAYLAND)
		check(vkCreateWaylandSurfaceKHR(app->instance, &(VkWaylandSurfaceCreateInfoKHR){
			.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
			.display = app->display,
			.surface = app->surface,
		}, nullptr, &app->window));
	if (app->options.target == TARGET_HEADLESS) {
		PFN_vkCreateHeadlessSurfaceEXT create_headless_surface =
			(PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(app->instance, "vkCreateHeadlessSurfaceEXT");

		check(create_headless_surface(app->instance, &(VkHeadlessSurfaceCreateInfoEXT){
			.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
		}, nullptr, &app->window));
	}
	pick_device(app);
	app->incremental = app->options.target != TARGET_OFFSCREEN &&
			   has_extension(app->physical, VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME);
	check(vkCreateDevice(app->physical, &(VkDeviceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &(VkPhysicalDeviceVulkan13Features){
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.synchronization2 = VK_TRUE,
			.dynamicRendering = VK_TRUE,
		},
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = app->family,
			.queueCount = 1,
			.pQueuePriorities = (float[]){ 1 },
		},
		.enabledExtensionCount = app->options.target == TARGET_OFFSCREEN ? 0 : app->incremental ? 2 : 1,
		.ppEnabledExtensionNames = device_extensions,
	}, nullptr, &app->device));
	vkGetDeviceQueue(app->device, app->family, 0, &app->queue);
	pick_format(app);
	create_pipeline(app);
	check(vkCreateCommandPool(app->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = app->family,
	}, nullptr, &app->pool));
	check(vkAllocateCommandBuffers(app->device, &(VkCommandBufferAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = app->pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = FRAMES,
	}, app->commands));
	foreach_array_index(app->fences, i) {
		check(vkCreateFence(app->device, &(VkFenceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		}, nullptr, &app->fences[i]));
		check(vkCreateSemaphore(app->device, &(VkSemaphoreCreateInfo){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		}, nullptr, &app->acquired[i]));
	}
}

static uint32_t memory_type(struct app *app, uint32_t types, VkMemoryPropertyFlags flags)
{
	VkPhysicalDeviceMemoryProperties properties;

	vkGetPhysicalDeviceMemoryProperties(app->physical, &properties);
	foreach_array_index(arrview_first(properties.memoryTypeCount, properties.memoryTypes), i)
		if (types & 1u << i && (properties.memoryTypes[i].propertyFlags & flags) == flags)
			return (uint32_t)i;
	fail("no memory type for an image or a buffer");
}

static void create_view(struct app *app, struct image *image)
{
	check(vkCreateImageView(app->device, &(VkImageViewCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = app->format,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	}, nullptr, &image->view));
	image->layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

static void create_image(struct app *app, struct image *image)
{
	VkMemoryRequirements requirements;

	check(vkCreateImage(app->device, &(VkImageCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = app->format,
		.extent = { (uint32_t)app->width, (uint32_t)app->height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	}, nullptr, &image->image));
	vkGetImageMemoryRequirements(app->device, image->image, &requirements);
	check(vkAllocateMemory(app->device, &(VkMemoryAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memory_type(app, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	}, nullptr, &image->memory));
	check(vkBindImageMemory(app->device, image->image, image->memory, 0));
	create_view(app, image);
}

static void destroy_image(struct app *app, struct image *image)
{
	vkDestroyImageView(app->device, image->view, nullptr);
	vkDestroyImage(app->device, image->image, nullptr);
	vkFreeMemory(app->device, image->memory, nullptr);
}

static void create_swapchain(struct app *app)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSwapchainKHR old = app->swapchain;
	uint32_t alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t count, images;

	check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical, app->window, &capabilities));
	if (capabilities.currentExtent.width != UINT32_MAX) {
		app->width = app->configured_width = (int32_t)capabilities.currentExtent.width;
		app->height = app->configured_height = (int32_t)capabilities.currentExtent.height;
	}
	images = app->options.images ? app->options.images : capabilities.minImageCount + 1;
	images = images > capabilities.minImageCount ? images : capabilities.minImageCount;
	images = capabilities.maxImageCount && images > capabilities.maxImageCount ? capabilities.maxImageCount : images;
	while (!(capabilities.supportedCompositeAlpha & alpha))
		alpha <<= 1;
	if (app->options.verify && !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
		fail("swapchain images can't be read back to verify them");
	check(vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical, app->window, &count, nullptr));
	VkPresentModeKHR modes[count + 1];
	check(vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical, app->window, &count, modes));
	foreach_array_const_ref(arrview_first(count, modes), mode)
		present = *mode == app->options.present ? *mode : present;

	check(vkCreateSwapchainKHR(app->device, &(VkSwapchainCreateInfoKHR){
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = app->window,
		.minImageCount = images,
		.imageFormat = app->format,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = { (uint32_t)app->width, (uint32_t)app->height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			      (app->options.verify ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)alpha,
		.presentMode = present,
		/* Keep pixels that other windows cover: images are repaired, not redrawn from scratch */
		.clipped = VK_FALSE,
		.oldSwapchain = old,
	}, nullptr, &app->swapchain));
	vkDestroySwapchainKHR(app->device, old, nullptr);

	check(vkGetSwapchainImagesKHR(app->device, app->swapchain, &count, nullptr));
	if (count > ARRAY_SIZE(app->images))
		fail("too many swapchain images");
	VkImage handles[count];
	check(vkGetSwapchainImagesKHR(app->device, app->swapchain, &count, handles));
	app->count = count;
	foreach_array_index(handles, i) {
		app->images[i].image = handles[i];
		create_view(app, &app->images[i]);
		check(vkCreateSemaphore(app->device, &(VkSemaphoreCreateInfo){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		}, nullptr, &app->images[i].rendered));
	}
}

static void create_readback(struct app *app)
{
	VkMemoryRequirements requirements;
	void *mapped;

	create_image(app, &app->reference);
	check(vkCreateBuffer(app->device, &(VkBufferCreateInfo){
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 2 * (VkDeviceSize)app->width * (VkDeviceSize)app->height * sizeof(uint32_t),
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	}, nullptr, &app->readback));
	vkGetBufferMemoryRequirements(app->device, app->readback, &requirements);
	check(vkAllocateMemory(app->device, &(VkMemoryAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memory_type(app, requirements.memoryTypeBits,
					       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
	}, nullptr, &app->readback_memory));
	check(vkBindBufferMemory(app->device, app->readback, app->readback_memory, 0));
	check(vkMapMemory(app->device, app->readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
	app->mapped = mapped;
}

/* Create new, empty images or a new swapchain, and resize the scene to match */
static void create_targets(struct app *app)
{
	region everything;

	app->width = app->configured_width;
	app->height = app->configured_height;
	if (app->options.target == TARGET_OFFSCREEN) {
		app->count = app->options.images;
		foreach_array_ref(arrview_first(app->count, app->images), image)
			create_image(app, image);
		engine_init(&app->engine, app->options.order, app->count, app->options.seed);
	} else {
		create_swapchain(app);
	}
	if (app->options.verify)
		create_readback(app);
	tracker_init(&app->tracker, &app->options.tracker, app->count, app->width, app->height);
	if (app->frame)
		scene_resize(&app->scene, app->width, app->height, &everything);
	else
		scene_init(&app->scene, app->width, app->height, app->options.seed);
	poor_damage_region_init(&app->flashed);
	app->resized = true;
	app->stale = false;
}

static void destroy_targets(struct app *app)
{
	check(vkDeviceWaitIdle(app->device));
	if (app->options.verify) {
		destroy_image(app, &app->reference);
		vkDestroyBuffer(app->device, app->readback, nullptr);
		vkFreeMemory(app->device, app->readback_memory, nullptr);
	}
	foreach_array_ref(arrview_first(app->count, app->images), image) {
		if (app->options.target == TARGET_OFFSCREEN) {
			destroy_image(app, image);
		} else {
			vkDestroyImageView(app->device, image->view, nullptr);
			vkDestroySemaphore(app->device, image->rendered, nullptr);
		}
	}
}

static void transition(VkCommandBuffer command, struct image *image, VkImageLayout layout)
{
	vkCmdPipelineBarrier2(command, &(VkDependencyInfo){
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &(VkImageMemoryBarrier2){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
			.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
					 VK_ACCESS_2_TRANSFER_READ_BIT,
			.oldLayout = image->layout,
			.newLayout = layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image->image,
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
		},
	});
	image->layout = layout;
}

static void push(struct app *app, VkCommandBuffer command, struct poor_damage_rect rect, enum mode mode,
		 uint32_t color, uint32_t other)
{
	const struct draw draw = {
		.rect = { rect.x, rect.y, rect.width, rect.height },
		.size = { app->width, app->height },
		.colors = { color, other },
		.mode = mode,
	};

	vkCmdPushConstants(command, app->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			   sizeof(draw), &draw);
	vkCmdDraw(command, 4, 1, 0, 0);
}

static int32_t lesser(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static int32_t greater(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static VkRect2D vk_rect(const struct poor_damage_rect *rect)
{
	return (VkRect2D){ { rect->x, rect->y }, { (uint32_t)rect->width, (uint32_t)rect->height } };
}

static bool overlap(const struct poor_damage_rect *a, const struct poor_damage_rect *b)
{
	return a->x < b->x + b->width && b->x < a->x + a->width && a->y < b->y + b->height && b->y < a->y + a->height;
}

static uint64_t area(const region *region)
{
	uint64_t pixels = 0;

	foreach_array_const_ref(arrview_first(region->count, region->rects), rect)
		pixels += (uint64_t)rect->width * (uint64_t)rect->height;
	return pixels;
}

/* Draw the scene into 'image' inside 'region' only, keeping the rest of the image as it is */
static void draw(struct app *app, VkCommandBuffer command, struct image *image, const region *region,
		 bool flash)
{
	struct poor_damage_rect bounds = region->rects[0];
	bool whole = area(region) == (uint64_t)app->width * (uint64_t)app->height;

	if (!region->count)
		return;
	foreach_array_const_ref(arrview_first(region->count, region->rects), rect) {
		int32_t right = greater(bounds.x + bounds.width, rect->x + rect->width);
		int32_t bottom = greater(bounds.y + bounds.height, rect->y + rect->height);

		bounds.x = lesser(bounds.x, rect->x);
		bounds.y = lesser(bounds.y, rect->y);
		bounds.width = right - bounds.x;
		bounds.height = bottom - bounds.y;
	}
	vkCmdBeginRendering(command, &(VkRenderingInfo){
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = vk_rect(&bounds),
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &(VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = image->view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = whole ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		},
	});
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline);
	vkCmdSetViewport(command, 0, 1, &(VkViewport){ 0, 0, (float)app->width, (float)app->height, 0, 1 });
	foreach_array_const_ref(arrview_first(region->count, region->rects), rect) {
		VkRect2D scissor = vk_rect(rect);

		vkCmdSetScissor(command, 0, 1, &scissor);
		push(app, command, *rect, MODE_BOARD, app->scene.board[0], app->scene.board[1]);
		foreach_array_const_ref(app->scene.boxes, box)
			if (box->shown && overlap(&box->rect, rect))
				push(app, command, box->rect, MODE_SOLID, box->color, 0);
		if (flash)
			push(app, command, *rect, MODE_SOLID, FLASH, 0);
	}
	vkCmdEndRendering(command);
}

static void copy(struct app *app, VkCommandBuffer command, struct image *image, size_t part)
{
	transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkCmdCopyImageToBuffer(command, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, app->readback, 1,
			       &(VkBufferImageCopy){
		.bufferOffset = part * (VkDeviceSize)app->width * (VkDeviceSize)app->height * sizeof(uint32_t),
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.imageExtent = { (uint32_t)app->width, (uint32_t)app->height, 1 },
	});
}

static void record(struct app *app, VkCommandBuffer command, struct image *image, const region *repair)
{
	region whole;

	check(vkResetCommandBuffer(command, 0));
	check(vkBeginCommandBuffer(command, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	}));
	transition(command, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	draw(app, command, image, repair, app->options.flash);
	if (app->options.verify) {
		poor_damage_region_whole(&whole, app->width, app->height);
		transition(command, &app->reference, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		draw(app, command, &app->reference, &whole, false);
		copy(app, command, image, 0);
		copy(app, command, &app->reference, 1);
		vkCmdPipelineBarrier2(command, &(VkDependencyInfo){
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.memoryBarrierCount = 1,
			.pMemoryBarriers = &(VkMemoryBarrier2){
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
				.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
				.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
			},
		});
	}
	if (app->options.target != TARGET_OFFSCREEN)
		transition(command, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	check(vkEndCommandBuffer(command));
}

static void submit(struct app *app, size_t slot, struct image *image)
{
	bool swapchain = app->options.target != TARGET_OFFSCREEN;

	check(vkQueueSubmit(app->queue, 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = swapchain,
		.pWaitSemaphores = &app->acquired[slot],
		.pWaitDstStageMask = (VkPipelineStageFlags[]){ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
		.commandBufferCount = 1,
		.pCommandBuffers = &app->commands[slot],
		.signalSemaphoreCount = swapchain,
		.pSignalSemaphores = &image->rendered,
	}, app->fences[slot]));
}

/* Present with the damage of this frame. Pass one empty rect if nothing changed, since no rects means everything. */
static void present(struct app *app, uint32_t index, const region *damage)
{
	VkRectLayerKHR rects[REGION_RECTS] = { { .extent = { 0, 0 } } };
	VkResult result;

	foreach_array_index(arrview_first(damage->count, damage->rects), i)
		rects[i] = (VkRectLayerKHR){
			.offset = { damage->rects[i].x, damage->rects[i].y },
			.extent = { (uint32_t)damage->rects[i].width, (uint32_t)damage->rects[i].height },
		};
	result = vkQueuePresentKHR(app->queue, &(VkPresentInfoKHR){
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = !app->incremental ? nullptr : &(VkPresentRegionsKHR){
			.sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
			.swapchainCount = 1,
			.pRegions = &(VkPresentRegionKHR){
				.rectangleCount = damage->count ? (uint32_t)damage->count : 1,
				.pRectangles = rects,
			},
		},
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &app->images[index].rendered,
		.swapchainCount = 1,
		.pSwapchains = &app->swapchain,
		.pImageIndices = &index,
	});
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		app->stale = true;
	else
		check(result);
}

static void verify(struct app *app, size_t slot)
{
	make_array_ptr(both, app->mapped, 2 * (size_t)app->width * (size_t)app->height);
	make_arrview_dim(images, (size_t)app->width * (size_t)app->height, both);

	check(vkWaitForFences(app->device, 1, &app->fences[slot], VK_TRUE, UINT64_MAX));
	app->stats.wrong += !!memcmp((*images)[0], (*images)[1], ARRAY_SIZE_BYTES((*images)[0]));
}

static void report(struct app *app)
{
	if (!app->stats.frames)
		return;
	println("vk-damage: ", tracker_name(app->options.tracker.kind), ", ", app->count, " images: ", app->stats.frames,
		" frames, ", app->options.verify ? "" : "unverified, ", app->stats.wrong, " wrong, ", app->stats.wholes,
		" redrawn whole, ", fmt_p(100.0 * (double)app->stats.drawn / (double)app->stats.pixels, 2),
		"% of pixels redrawn, ", fmt_p((double)app->stats.rects / (double)app->stats.frames, 2), " rects per frame");
}

static void frame(struct app *app)
{
	size_t slot = app->frame % FRAMES;
	region damage = POOR_DAMAGE_REGION_INIT, repair;
	uint32_t index;
	VkResult result;

	check(vkWaitForFences(app->device, 1, &app->fences[slot], VK_TRUE, UINT64_MAX));
	if (app->options.target == TARGET_OFFSCREEN) {
		index = (uint32_t)engine_acquire(&app->engine);
	} else {
		result = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX, app->acquired[slot], VK_NULL_HANDLE,
					       &index);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			app->stale = true;
			return;
		}
		check(result);
	}
	check(vkResetFences(app->device, 1, &app->fences[slot]));

	if (app->resized)
		poor_damage_region_whole(&damage, app->width, app->height);
	app->resized = false;
	scene_step(&app->scene, &damage);
	if (app->options.flash)
		poor_damage_region_add_region(&damage, &app->flashed);
	poor_damage_region_clip(&damage, app->width, app->height);
	tracker_limit(&app->tracker, &damage);
	tracker_repair(&app->tracker, index, &damage, &repair);

	record(app, app->commands[slot], &app->images[index], &repair);
	submit(app, slot, &app->images[index]);
	tracker_drawn(&app->tracker, index, &damage);
	if (app->options.target == TARGET_OFFSCREEN)
		engine_present(&app->engine, index);
	else
		present(app, index, app->options.flash ? &repair : &damage);
	tracker_presented(&app->tracker, index);
	app->flashed = repair;
	if (app->options.verify)
		verify(app, slot);

	app->frame++;
	app->stats.frames++;
	app->stats.drawn += area(&repair);
	app->stats.pixels += (uint64_t)app->width * (uint64_t)app->height;
	app->stats.rects += repair.count;
	app->stats.wholes += area(&repair) == (uint64_t)app->width * (uint64_t)app->height;
	if (app->options.target == TARGET_WAYLAND && app->stats.frames % 600 == 0)
		report(app);
}

/* With --resize, switch between the requested size and a smaller one every few frames */
static void resize(struct app *app)
{
	bool smaller = app->configured_width == app->options.width;

	if (!app->options.resize || !app->frame || app->frame % app->options.resize || app->frame == app->resized_at)
		return;
	app->resized_at = app->frame;
	app->configured_width = smaller ? app->options.width * 3 / 4 + 1 : app->options.width;
	app->configured_height = smaller ? app->options.height * 2 / 3 + 1 : app->options.height;
}

static void finish(struct app *app)
{
	destroy_targets(app);
	if (app->options.target != TARGET_OFFSCREEN) {
		vkDestroySwapchainKHR(app->device, app->swapchain, nullptr);
		vkDestroySurfaceKHR(app->instance, app->window, nullptr);
	}
	foreach_array_index(app->fences, i) {
		vkDestroyFence(app->device, app->fences[i], nullptr);
		vkDestroySemaphore(app->device, app->acquired[i], nullptr);
	}
	vkDestroyCommandPool(app->device, app->pool, nullptr);
	vkDestroyPipeline(app->device, app->pipeline, nullptr);
	vkDestroyPipelineLayout(app->device, app->layout, nullptr);
	vkDestroyDevice(app->device, nullptr);
	vkDestroyInstance(app->instance, nullptr);
	if (app->options.target == TARGET_WAYLAND) {
		xdg_toplevel_destroy(app->toplevel);
		xdg_surface_destroy(app->xdg_surface);
		wl_surface_destroy(app->surface);
		xdg_wm_base_destroy(app->wm_base);
		wl_compositor_destroy(app->compositor);
		wl_registry_destroy(app->registry);
		wl_display_disconnect(app->display);
	}
}

[[noreturn]] static void usage(const char *name)
{
	printerrln("Usage: ", name, " [options]\n\n"
		   "  --target wayland|headless|offscreen  where to draw (default: wayland)\n"
		   "  --images N        number of swapchain or offscreen images (default: 3 offscreen)\n"
		   "  --order fifo|newest|random  order in which the offscreen target hands out images (default: random)\n"
		   "  --present fifo|mailbox|immediate  present mode (default: fifo)\n"
		   "  --frames N        quit after N frames, 0 to run until the window is closed\n"
		   "  --resize N        switch to a smaller size and back every N frames\n"
		   "  --size WxH        surface size (default: 640x480)\n"
		   "  --seed N          random seed of the scene\n"
		   "  --device N        index of the Vulkan device to use\n"
		   "  --verify          compare every frame with one drawn from scratch\n"
		   "  --flash           tint what was redrawn\n"
		   TRACKER_USAGE);
	exit(EXIT_FAILURE);
}

#define find(names, name) find_impl(ARRAY_SIZE(names), &auto_arr(names), name)

static int find_impl(size_t count, const char *const (*names)[count], const char *name)
{
	foreach_array_index(names, i)
		if (!strcmp((*names)[i], name))
			return (int)i;
	return -1;
}

static void parse(int argc, char *argv[], struct options *options)
{
	static const struct option long_options[] = {
		{ "target", required_argument, nullptr, 't' }, TRACKER_LONG_OPTIONS,
		{ "images", required_argument, nullptr, 'i' }, { "order", required_argument, nullptr, 'o' },
		{ "present", required_argument, nullptr, 'p' }, { "frames", required_argument, nullptr, 'f' },
		{ "size", required_argument, nullptr, 's' }, { "seed", required_argument, nullptr, 'r' },
		{ "resize", required_argument, nullptr, 'z' },
		{ "device", required_argument, nullptr, 'd' }, { "verify", no_argument, nullptr, 'v' },
		{ "flash", no_argument, nullptr, 'l' }, { "help", no_argument, nullptr, 'h' }, {},
	};
	static const char *const targets[] = { "wayland", "headless", "offscreen" };
	static const char *const orders[] = { "fifo", "newest", "random" };
	static const char *const presents[] = { "immediate", "mailbox", "fifo" };
	bool known;
	int option, index;

	*options = (struct options){
		.target = TARGET_WAYLAND, .tracker = TRACKER_OPTIONS_INIT, .order = ENGINE_RANDOM,
		.present = VK_PRESENT_MODE_FIFO_KHR, .images = 0, .frames = 0, .resize = 0, .seed = 1,
		.device = UINT32_MAX,
		.width = 640, .height = 480,
	};
	while ((option = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
		known = true;
		switch (option) {
		case 't':
			known = (index = find(targets, optarg)) >= 0;
			options->target = (enum target)index;
			break;
		case 'o':
			known = (index = find(orders, optarg)) >= 0;
			options->order = (enum engine_order)index;
			break;
		case 'p':
			known = (index = find(presents, optarg)) >= 0;
			options->present = (VkPresentModeKHR)index;
			break;
		case 'i':
			options->images = (uint32_t)atoi(optarg);
			break;
		case 'f':
			options->frames = (uint32_t)atoi(optarg);
			break;
		case 's':
			known = sscanf(optarg, "%dx%d", &options->width, &options->height) == 2 && options->width > 0 &&
				options->height > 0;
			break;
		case 'r':
			options->seed = (uint32_t)atoi(optarg);
			break;
		case 'z':
			options->resize = (uint32_t)atoi(optarg);
			break;
		case 'd':
			options->device = (uint32_t)atoi(optarg);
			break;
		case 'v':
			options->verify = true;
			break;
		case 'l':
			options->flash = true;
			break;
		default:
			known = tracker_option(&options->tracker, option, optarg);
		}
		if (!known)
			usage(argv[0]);
	}
	if (options->target == TARGET_OFFSCREEN)
		options->images = options->images ? options->images : 3;
	if (optind != argc || !options->seed || (options->verify && options->flash) ||
	    (options->target == TARGET_OFFSCREEN && options->images > ENGINE_BUFFERS) ||
	    (options->target != TARGET_WAYLAND && !options->frames))
		usage(argv[0]);
}

int main(int argc, char *argv[])
{
	struct app app;

	parse(argc, argv, &app.options);
	app.configured_width = app.options.width;
	app.configured_height = app.options.height;
	app.swapchain = VK_NULL_HANDLE;
	app.frame = 0;
	app.resized_at = 0;
	app.stats = (typeof(app.stats)){};
	app.closed = false;
	if (app.options.target == TARGET_WAYLAND) {
		app.configured = false;
		app.compositor = nullptr;
		app.wm_base = nullptr;
		init_wayland(&app);
	}
	init_vulkan(&app);
	create_targets(&app);
	while (!app.closed && (!app.options.frames || app.frame < app.options.frames)) {
		if (app.options.target == TARGET_WAYLAND)
			dispatch(&app);
		resize(&app);
		if (app.stale || app.width != app.configured_width || app.height != app.configured_height) {
			destroy_targets(&app);
			create_targets(&app);
		}
		frame(&app);
	}
	report(&app);
	finish(&app);
	return app.stats.wrong ? EXIT_FAILURE : EXIT_SUCCESS;
}
