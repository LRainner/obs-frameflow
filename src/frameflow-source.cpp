#include "frameflow-source.hpp"

#include <graphics/graphics.h>
#include <util/bmem.h>

#include <cstdio>
#include <unordered_set>

namespace {

constexpr uint32_t kMinDimension = 16;
constexpr uint32_t kMaxDimension = 7680;
constexpr int kMaxSceneDepth = 32;
constexpr uint32_t kDefaultWidth = 2560;
constexpr uint32_t kDefaultHeight = 1440;

struct ResolutionPreset {
	const char *label;
	uint32_t width;
	uint32_t height;
};

const ResolutionPreset kResolutions[] = {
	{"1920x1080", 1920, 1080},
	{"2560x1440", 2560, 1440},
	{"3440x1440", 3440, 1440},
	{"3840x2160", 3840, 2160},
};

struct FrameFlowSource {
	obs_source_t *self = nullptr;
	obs_source_t *input = nullptr;

	uint32_t out_width = kDefaultWidth;
	uint32_t out_height = kDefaultHeight;

	gs_texrender_t *capture = nullptr;
	uint32_t capture_width = 0;
	uint32_t capture_height = 0;

	gs_effect_t *draw = nullptr;
	gs_eparam_t *image_param = nullptr;

	bool show_original = false;

	// Recursive render guard.
	bool rendering = false;
};

struct ContainsContext {
	obs_source_t *needle;
	int depth;
	bool found;
	std::unordered_set<obs_source_t *> *visited;
};

static bool contains_source_impl(obs_source_t *haystack, obs_source_t *needle,
				 int depth,
				 std::unordered_set<obs_source_t *> &visited)
{
	if (!haystack || !needle || depth > kMaxSceneDepth)
		return false;
	if (haystack == needle)
		return true;

	// Visit each scene once.
	if (!visited.insert(haystack).second)
		return false;

	obs_scene_t *scene = obs_scene_from_source(haystack);
	if (!scene)
		return false;

	ContainsContext ctx{needle, depth, false, &visited};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<ContainsContext *>(param);
			obs_source_t *child = obs_sceneitem_get_source(item);
			if (child == c->needle ||
			    contains_source_impl(child, c->needle, c->depth + 1,
						 *c->visited)) {
				c->found = true;
				return false;
			}
			return true;
		},
		&ctx);

	return ctx.found;
}

static bool contains_source(obs_source_t *haystack, obs_source_t *needle)
{
	std::unordered_set<obs_source_t *> visited;
	return contains_source_impl(haystack, needle, 0, visited);
}

static void release_input(FrameFlowSource *s)
{
	if (s->input) {
		obs_source_release(s->input);
		s->input = nullptr;
	}
}

static void set_input(FrameFlowSource *s, const char *name)
{
	release_input(s);

	if (!name || !*name)
		return;

	obs_source_t *src = obs_get_source_by_name(name);
	if (!src)
		return;

	if (src == s->self || contains_source(src, s->self)) {
		blog(LOG_WARNING,
		     "[frameflow] refusing recursive input source '%s'", name);
		obs_source_release(src);
		return;
	}

	s->input = src;
}

static void parse_resolution(const char *value, uint32_t *width,
			     uint32_t *height)
{
	unsigned int w = 0;
	unsigned int h = 0;

	if (value && std::sscanf(value, "%ux%u", &w, &h) == 2 &&
	    w >= kMinDimension && h >= kMinDimension && w <= kMaxDimension &&
	    h <= kMaxDimension) {
		*width = w;
		*height = h;
		return;
	}

	*width = kDefaultWidth;
	*height = kDefaultHeight;
}

static void apply_settings(FrameFlowSource *s, obs_data_t *settings)
{
	set_input(s, obs_data_get_string(settings, "input"));
	parse_resolution(obs_data_get_string(settings, "resolution"),
			 &s->out_width, &s->out_height);
	s->show_original = obs_data_get_bool(settings, "show_original");
}

const char *ff_get_name(void *)
{
	return obs_module_text("FrameFlow");
}

void *ff_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new FrameFlowSource();
	s->self = source;
	apply_settings(s, settings);
	return s;
}

void ff_destroy(void *data)
{
	auto *s = static_cast<FrameFlowSource *>(data);
	if (!s)
		return;

	release_input(s);

	obs_enter_graphics();
	if (s->capture)
		gs_texrender_destroy(s->capture);
	if (s->draw)
		gs_effect_destroy(s->draw);
	obs_leave_graphics();

	delete s;
}

void ff_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "input", "");
	obs_data_set_default_string(settings, "resolution", "2560x1440");
	obs_data_set_default_bool(settings, "show_original", false);
}

void ff_update(void *data, obs_data_t *settings)
{
	apply_settings(static_cast<FrameFlowSource *>(data), settings);
}

uint32_t ff_get_width(void *data)
{
	return static_cast<FrameFlowSource *>(data)->out_width;
}

uint32_t ff_get_height(void *data)
{
	return static_cast<FrameFlowSource *>(data)->out_height;
}

struct SourceListContext {
	obs_property_t *list;
	FrameFlowSource *self;
};

bool enum_source(void *param, obs_source_t *source)
{
	auto *ctx = static_cast<SourceListContext *>(param);

	if (source == ctx->self->self)
		return true;
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
		return true;
	if (contains_source(source, ctx->self->self))
		return true;

	const char *name = obs_source_get_name(source);
	if (name && *name)
		obs_property_list_add_string(ctx->list, name, name);

	return true;
}

obs_properties_t *ff_get_properties(void *data)
{
	auto *s = static_cast<FrameFlowSource *>(data);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *input = obs_properties_add_list(
		props, "input", obs_module_text("InputSource"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(input, obs_module_text("None"), "");
	if (s) {
		SourceListContext ctx{input, s};
		obs_enum_sources(enum_source, &ctx);
	}
	obs_property_set_long_description(
		input, obs_module_text("InputSource.Description"));

	obs_property_t *resolution = obs_properties_add_list(
		props, "resolution", obs_module_text("OutputResolution"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &preset : kResolutions)
		obs_property_list_add_string(resolution, preset.label,
					     preset.label);
	obs_property_set_long_description(
		resolution, obs_module_text("OutputResolution.Description"));

	obs_property_t *original = obs_properties_add_bool(
		props, "show_original", obs_module_text("ShowOriginal"));
	obs_property_set_long_description(
		original, obs_module_text("ShowOriginal.Description"));

	return props;
}

static bool ensure_resources(FrameFlowSource *s, uint32_t width,
			     uint32_t height)
{
	if (!s->draw) {
		char *path = obs_module_file("effects/frameflow.effect");
		if (!path)
			return false;

		char *error = nullptr;
		s->draw = gs_effect_create_from_file(path, &error);
		bfree(path);

		if (!s->draw) {
			blog(LOG_ERROR, "[frameflow] failed to load effect: %s",
			     error ? error : "unknown error");
			bfree(error);
			return false;
		}

		s->image_param = gs_effect_get_param_by_name(s->draw, "image");
		if (!s->image_param) {
			gs_effect_destroy(s->draw);
			s->draw = nullptr;
			return false;
		}
	}

	if (!s->capture || s->capture_width != width ||
	    s->capture_height != height) {
		if (s->capture) {
			gs_texrender_destroy(s->capture);
			s->capture = nullptr;
		}

		s->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		if (!s->capture)
			return false;

		s->capture_width = width;
		s->capture_height = height;
	}

	return true;
}

// Clears the render flag on scope exit.
struct RenderGuard {
	bool &flag;
	explicit RenderGuard(bool &value) : flag(value) { flag = true; }
	~RenderGuard() { flag = false; }
};

void ff_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<FrameFlowSource *>(data);
	if (!s || !s->input || s->rendering)
		return;

	if (obs_source_removed(s->input)) {
		release_input(s);
		return;
	}

	const uint32_t width = obs_source_get_width(s->input);
	const uint32_t height = obs_source_get_height(s->input);
	if (!width || !height)
		return;

	if (!ensure_resources(s, width, height))
		return;

	RenderGuard guard(s->rendering);

	// begin() refuses an already-rendered texrender.
	gs_texrender_reset(s->capture);
	if (!gs_texrender_begin(s->capture, width, height))
		return;

	const struct vec4 clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

	obs_source_video_render(s->input);
	gs_texrender_end(s->capture);

	gs_texture_t *texture = gs_texrender_get_texture(s->capture);
	if (!texture)
		return;

	// TODO(P2): upscale before drawing.
	gs_effect_set_texture(s->image_param, texture);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(s->draw, "Draw"))
		gs_draw_sprite(texture, 0, s->out_width, s->out_height);
	gs_blend_state_pop();
}

obs_source_info build_info()
{
	obs_source_info info = {};

	info.id = "frameflow";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
			    OBS_SOURCE_SRGB;

	info.get_name = ff_get_name;
	info.create = ff_create;
	info.destroy = ff_destroy;
	info.get_defaults = ff_get_defaults;
	info.get_properties = ff_get_properties;
	info.update = ff_update;
	info.get_width = ff_get_width;
	info.get_height = ff_get_height;
	info.video_render = ff_render;

	info.icon_type = OBS_ICON_TYPE_CAMERA;

	return info;
}

} // namespace

struct obs_source_info frameflow_source = build_info();
