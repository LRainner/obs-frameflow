#include "maxine-backend.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <windows.h>
#include <d3d11.h>

#include <cstring>
#include <cwchar>

namespace {

// NvCVImage is 64 bytes in the SDK; we never touch its fields, so the
// runtime API works against this opaque buffer.
struct alignas(8) NvImage {
	unsigned char bytes[64];
};

using Status = int;

struct Api {
	HMODULE vfx = nullptr;
	HMODULE img = nullptr;
	HMODULE ngx = nullptr;

	Status(__cdecl *create_effect)(const char *, void **) = nullptr;
	Status(__cdecl *set_image)(void *, const char *, void *) = nullptr;
	Status(__cdecl *set_u32)(void *, const char *, unsigned) = nullptr;
	Status(__cdecl *set_stream)(void *, const char *, void *) = nullptr;
	Status(__cdecl *load)(void *) = nullptr;
	Status(__cdecl *run)(void *, int) = nullptr;
	Status(__cdecl *destroy_effect)(void *) = nullptr;
	Status(__cdecl *stream_create)(void **) = nullptr;
	Status(__cdecl *stream_sync)(void *) = nullptr;
	Status(__cdecl *stream_destroy)(void *) = nullptr;

	Status(__cdecl *img_create)(unsigned, unsigned, int, int, unsigned,
				    unsigned, unsigned, NvImage **) = nullptr;
	Status(__cdecl *img_transfer)(const NvImage *, NvImage *, float,
				      void *, NvImage *) = nullptr;
	Status(__cdecl *img_from_d3d11)(NvImage *, ID3D11Texture2D *) = nullptr;
	Status(__cdecl *img_map)(NvImage *, void *) = nullptr;
	Status(__cdecl *img_unmap)(NvImage *, void *) = nullptr;
	void(__cdecl *img_dealloc)(NvImage *) = nullptr;
	void(__cdecl *img_destroy)(NvImage *) = nullptr;
};

enum Entries {
	NVCV_RGBA = 6,
	NVCV_U8 = 1,
	NVCV_CHUNKY = 0,
	NVCV_GPU = 1,
};

struct State {
	bool loaded = false;
	bool failed = false;
	Api api;

	void *effect = nullptr;
	void *stream = nullptr;
	NvImage *gpu_in = nullptr;
	NvImage *gpu_out = nullptr;

	struct gs_texture *out_texture = nullptr;
	ID3D11Texture2D *in_d3d = nullptr;
	ID3D11Texture2D *out_d3d = nullptr;

	NvImage wrap_in = {};
	NvImage wrap_out = {};
	bool wrap_in_valid = false;
	bool wrap_out_valid = false;

	uint32_t in_width = 0;
	uint32_t in_height = 0;
	uint32_t out_width = 0;
	uint32_t out_height = 0;
};

State g_state;

wchar_t *utf8_to_wide(const char *text)
{
	size_t len = strlen(text) + 1;
	auto *wide = static_cast<wchar_t *>(bmalloc(sizeof(wchar_t) * len));
	if (!wide)
		return nullptr;
	MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, (int)len);
	return wide;
}

HMODULE load_module(const wchar_t *path, const char *label)
{
	HMODULE module =
		LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module)
		blog(LOG_WARNING, "[frameflow] load %s failed (%lu): %ls", label,
		     GetLastError(), path);
	return module;
}

bool load_api()
{
	if (g_state.loaded)
		return true;
	if (g_state.failed)
		return false;

	char *hint = obs_module_file("maxine/bin/NVVideoEffects.dll");
	if (!hint) {
		blog(LOG_INFO,
		     "[frameflow] maxine runtime not found, using Lanczos");
		g_state.failed = true;
		return false;
	}

	blog(LOG_INFO, "[frameflow] maxine path: %s", hint);
	wchar_t *vfx_path = utf8_to_wide(hint);
	bfree(hint);
	if (!vfx_path) {
		g_state.failed = true;
		return false;
	}

	// obs_module_file() uses forward slashes on Windows.
	for (wchar_t *c = vfx_path; *c; ++c) {
		if (*c == L'/')
			*c = L'\\';
	}

	wchar_t img_path[MAX_PATH];
	wcscpy_s(img_path, vfx_path);
	wchar_t *slash = wcsrchr(img_path, L'\\');
	if (slash)
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - img_path),
			 L"NVCVImage.dll");

	wchar_t ngx_path[MAX_PATH];
	wcscpy_s(ngx_path, vfx_path);
	slash = wcsrchr(ngx_path, L'\\');
	if (slash)
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - ngx_path),
			 L"nvngxruntime.dll");

	Api &api = g_state.api;
	api.ngx = load_module(ngx_path, "nvngxruntime");
	api.img = load_module(img_path, "NVCVImage");
	api.vfx = load_module(vfx_path, "NVVideoEffects");
	bfree(vfx_path);

	auto resolve = [](HMODULE mod, const char *name) -> FARPROC {
		return mod ? GetProcAddress(mod, name) : nullptr;
	};

	if (!api.vfx || !api.img) {
		blog(LOG_WARNING,
		     "[frameflow] maxine runtime could not be loaded (%lu)",
		     GetLastError());
		g_state.failed = true;
		return false;
	}

#define RESOLVE(field, module, symbol)                                         \
	api.field = reinterpret_cast<decltype(api.field)>(                      \
		resolve(module, symbol));                                       \
	if (!api.field) {                                                      \
		blog(LOG_WARNING, "[frameflow] missing %s", symbol);           \
		g_state.failed = true;                                         \
		return false;                                                  \
	}

	RESOLVE(create_effect, api.vfx, "NvVFX_CreateEffect");
	RESOLVE(set_image, api.vfx, "NvVFX_SetImage");
	RESOLVE(set_u32, api.vfx, "NvVFX_SetU32");
	RESOLVE(set_stream, api.vfx, "NvVFX_SetCudaStream");
	RESOLVE(load, api.vfx, "NvVFX_Load");
	RESOLVE(run, api.vfx, "NvVFX_Run");
	RESOLVE(destroy_effect, api.vfx, "NvVFX_DestroyEffect");
	RESOLVE(stream_create, api.vfx, "NvVFX_CudaStreamCreate");
	RESOLVE(stream_sync, api.vfx, "NvVFX_CudaStreamSynchronize");
	RESOLVE(stream_destroy, api.vfx, "NvVFX_CudaStreamDestroy");
	RESOLVE(img_create, api.img, "NvCVImage_Create");
	RESOLVE(img_transfer, api.img, "NvCVImage_Transfer");
	RESOLVE(img_from_d3d11, api.img, "NvCVImage_InitFromD3D11Texture");
	RESOLVE(img_map, api.img, "NvCVImage_MapResource");
	RESOLVE(img_unmap, api.img, "NvCVImage_UnmapResource");
	RESOLVE(img_dealloc, api.img, "NvCVImage_Dealloc");
	RESOLVE(img_destroy, api.img, "NvCVImage_Destroy");
#undef RESOLVE

	g_state.loaded = true;
	blog(LOG_INFO, "[frameflow] maxine runtime loaded");
	return true;
}

void release_texture_wraps()
{
	Api &api = g_state.api;
	if (g_state.wrap_in_valid) {
		api.img_dealloc(&g_state.wrap_in);
		g_state.wrap_in_valid = false;
	}
	if (g_state.wrap_out_valid) {
		api.img_dealloc(&g_state.wrap_out);
		g_state.wrap_out_valid = false;
	}
}

void release_pipeline()
{
	Api &api = g_state.api;
	release_texture_wraps();
	if (g_state.effect) {
		api.destroy_effect(g_state.effect);
		g_state.effect = nullptr;
	}
	if (g_state.stream && api.stream_destroy) {
		api.stream_destroy(g_state.stream);
	}
	g_state.stream = nullptr;
	if (g_state.gpu_in) {
		api.img_destroy(g_state.gpu_in);
		g_state.gpu_in = nullptr;
	}
	if (g_state.gpu_out) {
		api.img_destroy(g_state.gpu_out);
		g_state.gpu_out = nullptr;
	}
	if (g_state.out_texture) {
		gs_texture_destroy(g_state.out_texture);
		g_state.out_texture = nullptr;
	}
	g_state.in_d3d = nullptr;
	g_state.out_d3d = nullptr;
	g_state.in_width = 0;
	g_state.in_height = 0;
	g_state.out_width = 0;
	g_state.out_height = 0;
}

bool ensure_pipeline(struct gs_texture *src, uint32_t in_width,
		     uint32_t in_height, uint32_t out_width,
		     uint32_t out_height)
{
	Api &api = g_state.api;

	ID3D11Texture2D *src_d3d =
		static_cast<ID3D11Texture2D *>(gs_texture_get_obj(src));
	if (!src_d3d)
		return false;

	if (g_state.effect && g_state.in_d3d == src_d3d &&
	    g_state.in_width == in_width && g_state.in_height == in_height &&
	    g_state.out_width == out_width && g_state.out_height == out_height)
		return true;

	release_pipeline();

	g_state.out_texture = gs_texture_create(out_width, out_height, GS_RGBA,
						1, nullptr, GS_RENDER_TARGET);
	if (!g_state.out_texture)
		return false;

	ID3D11Texture2D *out_d3d = static_cast<ID3D11Texture2D *>(
		gs_texture_get_obj(g_state.out_texture));
	if (!out_d3d)
		return false;

	if (api.img_create(in_width, in_height, NVCV_RGBA, NVCV_U8,
			   NVCV_CHUNKY, NVCV_GPU, 1, &g_state.gpu_in) != 0)
		return false;
	if (api.img_create(out_width, out_height, NVCV_RGBA, NVCV_U8,
			   NVCV_CHUNKY, NVCV_GPU, 1, &g_state.gpu_out) != 0)
		return false;

	if (api.img_from_d3d11(&g_state.wrap_in, src_d3d) != 0)
		return false;
	g_state.wrap_in_valid = true;
	if (api.img_from_d3d11(&g_state.wrap_out, out_d3d) != 0)
		return false;
	g_state.wrap_out_valid = true;

	if (api.create_effect("VideoSuperRes", &g_state.effect) != 0)
		return false;
	if (api.stream_create(&g_state.stream) != 0)
		return false;
	if (api.set_stream(g_state.effect, "CudaStream", g_state.stream) != 0)
		return false;
	if (api.set_u32(g_state.effect, "QualityLevel", 1) != 0)
		return false;
	if (api.set_image(g_state.effect, "SrcImage0", g_state.gpu_in) != 0)
		return false;
	if (api.set_image(g_state.effect, "DstImage0", g_state.gpu_out) != 0)
		return false;
	if (api.load(g_state.effect) != 0)
		return false;

	g_state.in_d3d = src_d3d;
	g_state.out_d3d = out_d3d;
	g_state.in_width = in_width;
	g_state.in_height = in_height;
	g_state.out_width = out_width;
	g_state.out_height = out_height;
	return true;
}

} // namespace

namespace maxine {

bool available()
{
	return load_api();
}

struct gs_texture *upscale(struct gs_texture *src, uint32_t in_width,
			   uint32_t in_height, uint32_t out_width,
			   uint32_t out_height)
{
	if (!load_api())
		return nullptr;

	Api &api = g_state.api;
	if (!ensure_pipeline(src, in_width, in_height, out_width, out_height)) {
		blog(LOG_WARNING, "[frameflow] maxine pipeline init failed");
		release_pipeline();
		return nullptr;
	}

	if (api.img_map(&g_state.wrap_in, g_state.stream) != 0)
		return nullptr;

	Status st = api.img_transfer(&g_state.wrap_in, g_state.gpu_in, 1.f,
				     g_state.stream, nullptr);

	api.img_unmap(&g_state.wrap_in, g_state.stream);
	if (st != 0) {
		blog(LOG_WARNING, "[frameflow] maxine input transfer failed: %d",
		     st);
		return nullptr;
	}

	st = api.run(g_state.effect, 0);
	if (st != 0) {
		blog(LOG_WARNING, "[frameflow] maxine run failed: %d", st);
		return nullptr;
	}

	if (api.img_map(&g_state.wrap_out, g_state.stream) != 0)
		return nullptr;

	st = api.img_transfer(g_state.gpu_out, &g_state.wrap_out, 1.f,
			      g_state.stream, nullptr);

	api.stream_sync(g_state.stream);
	api.img_unmap(&g_state.wrap_out, g_state.stream);
	if (st != 0) {
		blog(LOG_WARNING, "[frameflow] maxine output transfer failed: %d",
		     st);
		return nullptr;
	}

	return g_state.out_texture;
}

void shutdown()
{
	if (!g_state.loaded)
		return;
	release_pipeline();
	if (g_state.api.vfx)
		FreeLibrary(g_state.api.vfx);
	if (g_state.api.img)
		FreeLibrary(g_state.api.img);
	if (g_state.api.ngx)
		FreeLibrary(g_state.api.ngx);
	g_state = State{};
}

} // namespace maxine
