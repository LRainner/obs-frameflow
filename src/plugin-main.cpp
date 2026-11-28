#include "frameflow-source.hpp"
#include "maxine-backend.hpp"
#include "frameflow-version.hpp"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("frameflow", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "FrameFlow";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Real-time upscaling, denoise and frame generation for any OBS source.";
}

void obs_module_unload(void)
{
	obs_enter_graphics();
	maxine::shutdown();
	obs_leave_graphics();
}

bool obs_module_load(void)
{
	obs_register_source(&frameflow_source);
	blog(LOG_INFO, "[frameflow] loaded version %s", FRAMEFLOW_VERSION);
	return true;
}
