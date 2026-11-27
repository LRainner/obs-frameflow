#include "frameflow-source.hpp"

#include <graphics/graphics.h>
#include <util/bmem.h>

#include <charconv>
#include <mutex>
#include <string_view>
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
	gs_texrender_t *upscaled = nullptr;

	gs_effect_t *scale = nullptr;
	gs_eparam_t *scale_image = nullptr;
	gs_eparam_t *scale_size = nullptr;

	bool show_original = false;

	// Recursive render guard.
	bool rendering = false;

	// Whether the input is registered as an active child.
	bool child_added = false;

	// Guards input/child_added against the active-tree callback.
	std::mutex input_mutex;

	// Diagnostics.
	uint64_t frame_count = 0;
	uint64_t zero_size_count = 0;
	uint64_t begin_fail_count = 0;
	uint64_t upscale_fail_count = 0;
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
	obs_source_t *old = nullptr;
	bool registered = false;

	{
		std::lock_guard<std::mutex> lock(s->input_mutex);
		old = s->input;
		registered = s->child_added;
		s->input = nullptr;
		s->child_added = false;
	}

	if (!old)
		return;

	if (registered)
		obs_source_remove_active_child(s->self, old);

	obs_source_release(old);
}

static void set_input(FrameFlowSource *s, const char *name)
{
	obs_source_t *next = nullptr;

	if (name && *name) {
		next = obs_get_source_by_name(name);
		if (next && (next == s->self || contains_source(next, s->self))) {
			blog(LOG_WARNING,
			     "[frameflow] refusing recursive input source '%s'",
			     name);
			obs_source_release(next);
			next = nullptr;
		}
	}

	bool same;
	{
		std::lock_guard<std::mutex> lock(s->input_mutex);
		same = next == s->input;
	}
	if (same) {
		obs_source_release(next);
		return;
	}

	release_input(s);

	if (!next)
		return;

	// add_active_child walks the candidate's tree to reject cycles; the
	// candidate must not be reachable through us until that check passes.
	if (!obs_source_add_active_child(s->self, next)) {
		blog(LOG_WARNING, "[frameflow] could not activate input");
		obs_source_release(next);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(s->input_mutex);
		s->input = next;
		s->child_added = true;
	}
}

static bool parse_uint(std::string_view text, uint32_t &value)
{
	if (text.empty())
		return false;

	uint32_t parsed = 0;
	const auto result =
		std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (result.ec != std::errc{} ||
	    result.ptr != text.data() + text.size())
		return false;

	value = parsed;
	return true;
}

static void parse_resolution(const char *value, uint32_t *width,
			     uint32_t *height)
{
	std::string_view text(value ? value : "");

	const size_t separator = text.find('x');
	if (separator != std::string_view::npos) {
		uint32_t w = 0;
		uint32_t h = 0;

		if (parse_uint(text.substr(0, separator), w) &&
		    parse_uint(text.substr(separator + 1), h) &&
		    w >= kMinDimension && h >= kMinDimension &&
		    w <= kMaxDimension && h <= kMaxDimension) {
			*width = w;
			*height = h;
			return;
		}
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
	if (s->upscaled)
		gs_texrender_destroy(s->upscaled);
	if (s->scale)
		gs_effect_destroy(s->scale);
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

static gs_texrender_t *resolve_texrender(gs_texrender_t *texrender,
					 gs_color_format format)
{
	if (texrender && gs_texrender_get_format(texrender) != format) {
		gs_texrender_destroy(texrender);
		texrender = nullptr;
	}

	if (!texrender)
		texrender = gs_texrender_create(format, GS_ZS_NONE);

	return texrender;
}

static bool ensure_scale_effect(FrameFlowSource *s)
{
	if (s->scale)
		return true;

	char *path = obs_module_file("effects/frameflow.effect");
	if (!path)
		return false;

	char *error = nullptr;
	s->scale = gs_effect_create_from_file(path, &error);
	bfree(path);

	if (!s->scale) {
		blog(LOG_ERROR, "[frameflow] failed to load scale effect: %s",
		     error ? error : "unknown error");
		bfree(error);
		return false;
	}

	s->scale_image = gs_effect_get_param_by_name(s->scale, "image");
	s->scale_size = gs_effect_get_param_by_name(s->scale, "source_size");
	return s->scale_image && s->scale_size;
}

static bool ensure_resources(FrameFlowSource *s, gs_color_format format)
{
	s->capture = resolve_texrender(s->capture, format);
	if (!s->capture)
		return false;

	// The upscaler is optional: without it the capture texture is drawn
	// directly, which still renders correctly.
	if (ensure_scale_effect(s))
		s->upscaled = resolve_texrender(s->upscaled, format);

	return true;
}

// Clears the render flag on scope exit.
struct RenderGuard {
	bool &flag;
	explicit RenderGuard(bool &value) : flag(value) { flag = true; }
	~RenderGuard() { flag = false; }
};

static enum gs_color_space input_color_space(obs_source_t *input)
{
	static const enum gs_color_space preferred[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
		GS_CS_709_SCRGB,
	};

	return obs_source_get_color_space(input, 4, preferred);
}

// Picks the default-effect technique that converts the input space to the
// current one, mirroring source_render() in libobs.
static const char *draw_technique(gs_effect_t *effect,
				  enum gs_color_space source_space,
				  enum gs_color_space current_space,
				  float *multiplier)
{
	const char *name = "Draw";
	*multiplier = 1.0f;

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
		if (current_space == GS_CS_709_SCRGB) {
			name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		}
		break;
	case GS_CS_709_EXTENDED:
		if (current_space == GS_CS_SRGB ||
		    current_space == GS_CS_SRGB_16F)
			name = "DrawTonemap";
		else if (current_space == GS_CS_709_SCRGB) {
			name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		}
		break;
	case GS_CS_709_SCRGB:
		if (current_space == GS_CS_SRGB ||
		    current_space == GS_CS_SRGB_16F) {
			name = "DrawMultiplyTonemap";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
		} else if (current_space == GS_CS_709_EXTENDED) {
			name = "DrawMultiply";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
		}
		break;
	}

	return gs_effect_get_technique(effect, name) ? name : "Draw";
}

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
	if (!width || !height) {
		++s->zero_size_count;
		if ((s->frame_count % 240) == 0)
			blog(LOG_WARNING,
			     "[frameflow] input '%s' has no size (frames=%llu zero=%llu)",
			     obs_source_get_name(s->input),
			     (unsigned long long)s->frame_count,
			     (unsigned long long)s->zero_size_count);
		return;
	}

	const enum gs_color_space source_space = input_color_space(s->input);
	const enum gs_color_format format = gs_get_format_from_space(source_space);
	if (!ensure_resources(s, format))
		return;

	RenderGuard guard(s->rendering);

	// begin() refuses an already-rendered texrender.
	gs_texrender_reset(s->capture);
	if (!gs_texrender_begin_with_color_space(s->capture, width, height,
						 source_space))
		return;

	// texrender inherits the canvas projection; override it for the input.
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	const struct vec4 clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(s->input);
	gs_blend_state_pop();

	gs_texrender_end(s->capture);

	gs_texture_t *texture = gs_texrender_get_texture(s->capture);
	if (!texture)
		return;

	// Lanczos pass into the output resolution. The final draw below still
	// goes through the color-space conversion table.
	bool used_upscale = false;
	if (!s->show_original && s->scale && s->upscaled) {
		gs_texrender_reset(s->upscaled);
		if (gs_texrender_begin_with_color_space(s->upscaled,
							s->out_width,
							s->out_height,
							source_space)) {
			gs_ortho(0.0f, (float)s->out_width, 0.0f,
				 (float)s->out_height, -100.0f, 100.0f);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

			struct vec2 input_size = {(float)width, (float)height};
			gs_effect_set_texture(s->scale_image, texture);
			gs_effect_set_vec2(s->scale_size, &input_size);

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			while (gs_effect_loop(s->scale, "Draw"))
				gs_draw_sprite(texture, 0, s->out_width,
					       s->out_height);
			gs_blend_state_pop();

			gs_texrender_end(s->upscaled);

			gs_texture_t *scaled =
				gs_texrender_get_texture(s->upscaled);
			if (scaled) {
				texture = scaled;
				used_upscale = true;
			}
		} else {
			++s->upscale_fail_count;
			gs_texrender_reset(s->upscaled);
			if ((s->frame_count % 240) == 0)
				blog(LOG_WARNING,
				     "[frameflow] upscale begin failed (frames=%llu fails=%llu)",
				     (unsigned long long)s->frame_count,
				     (unsigned long long)s->upscale_fail_count);
		}
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *multiplier_param =
		gs_effect_get_param_by_name(effect, "multiplier");

	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier;
	const char *tech = draw_technique(effect, source_space, current_space,
					  &multiplier);

	// 16F textures are linear, so sRGB sampling is a no-op there.
	const bool linear = source_space != GS_CS_SRGB || gs_get_linear_srgb();
	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear);
	if (linear)
		gs_effect_set_texture_srgb(image, texture);
	else
		gs_effect_set_texture(image, texture);
	if (multiplier_param)
		gs_effect_set_float(multiplier_param, multiplier);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(effect, tech))
		gs_draw_sprite(texture, 0, s->out_width, s->out_height);
	gs_blend_state_pop();

	gs_enable_framebuffer_srgb(previous_srgb);

	++s->frame_count;
	if ((s->frame_count % 240) == 0)
		blog(LOG_INFO,
		     "[frameflow] frames=%llu input='%s' %ux%u space=%d format=%d upscale=%d out=%ux%u srgb=%d linear=%d current=%d zero=%llu beginfail=%llu upscalefail=%llu",
		     (unsigned long long)s->frame_count,
		     obs_source_get_name(s->input), width, height,
		     (int)source_space, (int)format, used_upscale ? 1 : 0,
		     s->out_width, s->out_height, previous_srgb ? 1 : 0,
		     linear ? 1 : 0, (int)current_space,
		     (unsigned long long)s->zero_size_count,
		     (unsigned long long)s->begin_fail_count,
		     (unsigned long long)s->upscale_fail_count);
}

enum gs_color_space ff_video_get_color_space(void *data, size_t count,
					     const enum gs_color_space *preferred)
{
	auto *s = static_cast<FrameFlowSource *>(data);
	if (!s)
		return GS_CS_SRGB;

	obs_source_t *input = nullptr;
	{
		std::lock_guard<std::mutex> lock(s->input_mutex);
		input = s->input;
		if (input)
			obs_source_get_ref(input);
	}

	if (!input)
		return GS_CS_SRGB;

	const enum gs_color_space space =
		obs_source_get_color_space(input, count, preferred);
	obs_source_release(input);
	return space;
}

void ff_enum_active_sources(void *data, obs_source_enum_proc_t callback,
			    void *param)
{
	auto *s = static_cast<FrameFlowSource *>(data);
	if (!s)
		return;

	obs_source_t *input = nullptr;
	{
		std::lock_guard<std::mutex> lock(s->input_mutex);
		input = s->input;
		if (input)
			obs_source_get_ref(input);
	}

	if (!input)
		return;

	callback(s->self, input, param);
	obs_source_release(input);
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
	info.video_get_color_space = ff_video_get_color_space;
	info.enum_active_sources = ff_enum_active_sources;

	info.icon_type = OBS_ICON_TYPE_CAMERA;

	return info;
}

} // namespace

struct obs_source_info frameflow_source = build_info();
