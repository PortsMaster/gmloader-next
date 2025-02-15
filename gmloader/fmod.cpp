#ifdef USE_FMOD

#include "fmod.hpp"
#include "fmod_studio.hpp"
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"
#include "configuration.h"

namespace fs = std::filesystem;

static FMOD::System* fmod_system = nullptr;
static FMOD::Studio::System* fmod_studio_system = nullptr;
static std::vector<FMOD::Studio::EventInstance *> event_instances = {};
static std::vector<FMOD::Studio::EventInstance *> event_instances_oneshot = {};
static std::unordered_map<std::string, FMOD::Studio::EventDescription *> event_desc_list = {};

static const char *AccessYYString(const RValue *ret, int index)
{
    if (ret[index].kind == VALUE_STRING)
        return (const char *)ret[index].rvalue.str->m_thing;
    
    fatal_error("Index %d is not an integer!", index);
    return "";
}

extern "C" void FMOD_SDL_Register(FMOD_SYSTEM *system);
ABI_ATTR void fmod_init(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    int maxchannels = YYGetInt32(args, 0);

    if (fmod_system && fmod_studio_system)
    {
        ret->rvalue.val = 1.0;
        return;
    }
    
    FMOD_RESULT res;
    res = FMOD::Studio::System::create(&fmod_studio_system);
    if (res != FMOD_OK) {
        fatal_error("[FMOD]: Could not create Studio System (%d).\n", res);
        return;
    }

    res = fmod_studio_system->getCoreSystem(&fmod_system);
    if (res != FMOD_OK) {
        fatal_error("[FMOD]: Could not get Core System (%d).\n", res);
        return;
    }

    FMOD_SDL_Register((FMOD_SYSTEM *)fmod_system);
    res = fmod_system->setSoftwareFormat(0, FMOD_SPEAKERMODE_STEREO, 0);
    if (res != FMOD_OK) {
        fatal_error("[FMOD]: Could not set Software Format (%d).\n", res);
        return;
    }

    res = fmod_studio_system->initialize(maxchannels, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, 0);
    if (res != FMOD_OK) {
        fatal_error("[FMOD]: Could not init Studio System (%d).\n", res);
        return;
    }

    // pre-reserve ample space...
    event_instances.reserve(2048);
    event_instances_oneshot.reserve(2048);
    event_desc_list.reserve(2048);

    ret->rvalue.val = 1.0;
}
ABI_ATTR void fmod_destroy(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;

    if (fmod_system)
    {
        fmod_studio_system->release();
        fmod_studio_system = nullptr;
    }
    if (fmod_system)
    {
        fmod_system->release();
        fmod_system = nullptr;
    }

    event_instances.clear();
    event_instances_oneshot.clear();
    event_desc_list.clear();
}

ABI_ATTR void fmod_bank_load(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;

    const char *bank_name = AccessYYString(args, 0);
    double isBlocking = YYGetReal(args, 1);

    fs::path bank_path = bank_name;
    FMOD::Studio::Bank* bank;
    FMOD_RESULT result = fmod_studio_system->loadBankFile(bank_path.c_str(), (isBlocking != 0.0), &bank);
    if (result != FMOD_OK)
    {
        fatal_error("[FMOD]: Failed to load bank '%s'!", bank_name);
        ret->rvalue.val = 0.0;
        return;
    }

    ret->rvalue.val = 1.0;
}

ABI_ATTR void fmod_update(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    FMOD_RESULT result;
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    if (fmod_studio_system)
    {
        result = fmod_studio_system->update();
        if (result != FMOD_OK)
            return;

        int i = 0;
        while (i < event_instances_oneshot.size())
        {
            auto oneshot = event_instances_oneshot.begin() + i;
            FMOD_STUDIO_PLAYBACK_STATE state;

            result = (*oneshot)->getPlaybackState(&state);
            if (result == FMOD_OK)
            {
                if (state & FMOD_STUDIO_PLAYBACK_STOPPED) {
                    (*oneshot)->release();
                    event_instances_oneshot.erase(oneshot);
                    continue;
                }
            }

            i++;
        }

        ret->rvalue.val = 1.0;
    }
}
ABI_ATTR void fmod_event_create_instance(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    const char *event_path = AccessYYString(args, 0);
    FMOD::Studio::EventDescription* event_description = nullptr;
    FMOD_RESULT result = fmod_studio_system->getEvent(event_path, &event_description);
    if (result != FMOD_OK || event_description == nullptr)
    {
        ret->rvalue.v64 = (uintptr_t)nullptr;
        return;
    }

    FMOD::Studio::EventInstance *instance = nullptr;
    result = event_description->createInstance(&instance);
    if (result != FMOD_OK || instance == nullptr)
    {
        ret->rvalue.v64 = (uintptr_t)nullptr;
        return;
    }

    event_instances.push_back(instance);
    ret->rvalue.v64 = (uintptr_t)instance;
}
ABI_ATTR void fmod_event_instance_play(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        instance->start();
        ret->rvalue.val = 1.0;
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_event_instance_stop(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    float isImmediate = YYGetReal(args, 1);

    if (instance)
    {
        instance->stop((isImmediate != 0.0) ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT);
        ret->rvalue.val = 1.0;
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_event_instance_release(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;

    auto it = std::find(event_instances.begin(), event_instances.end(), instance);
    if (it != event_instances.end())
        event_instances.erase(it);

    it = std::find(event_instances_oneshot.begin(), event_instances_oneshot.end(), instance);
    if (it != event_instances_oneshot.end())
        event_instances_oneshot.erase(it);

    if (instance)
    {
        instance->release();
        ret->rvalue.val = 1.0;
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_event_instance_set_3d_attributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;

    if (instance)
    {
        float posX = YYGetReal(args, 1);
        float posY = YYGetReal(args, 2);
        float velX = 0.0f;
        float velY = 0.0f;
        FMOD_3D_ATTRIBUTES attributes = { { posX, posY, 0.0f }, { velX, velY, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } };
        instance->set3DAttributes(&attributes);
        ret->rvalue.val = 1.0;
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_set_listener_attributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    int listener_index = YYGetInt32(args, 0);
    float posX = YYGetReal(args, 1);
    float posY = YYGetReal(args, 2);
    float velX = 0.0f;
    float velY = 0.0f;

    FMOD_3D_ATTRIBUTES attributes = { { posX, posY, 0.0f }, { velX, velY, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } };
    fmod_studio_system->setListenerAttributes(listener_index, &attributes);
    ret->rvalue.val = 1.0;
}
ABI_ATTR void fmod_set_num_listeners(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    int num_listeners = YYGetInt32(args, 0);
    FMOD_RESULT result = fmod_studio_system->setNumListeners(num_listeners);
    if (result == FMOD_OK)
    {
        ret->rvalue.val = 1.0;
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}   
ABI_ATTR void fmod_event_instance_set_parameter(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        const char *parameter_name = AccessYYString(args, 1);
        float value = YYGetReal(args, 2);
        FMOD_RESULT result = instance->setParameterByName(parameter_name, value);
        if (result == FMOD_OK)
        {
            ret->rvalue.val = 1.0;
        }
        else
        {
            ret->rvalue.val = 0.0;
        }
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_event_instance_get_parameter(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        const char *parameter_name = AccessYYString(args, 1);
        float value;
        FMOD_RESULT result = instance->getParameterByName(parameter_name, &value);
        if (result == FMOD_OK)
        {
            ret->rvalue.val = value;
        }
        else
        {
            ret->rvalue.val = 0.0;
        }
    }
    else
    {
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_set_parameter(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    const char *parameter_name = AccessYYString(args, 0);
    float value = YYGetReal(args, 1);
    float ignoreseekspeed = YYGetInt32(args, 2);

    FMOD_RESULT result = fmod_studio_system->setParameterByName(parameter_name, value, ignoreseekspeed);
    ret->rvalue.val = (result == FMOD_OK) ? 1.0 : 0.0;
}
ABI_ATTR void fmod_get_parameter(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    const char *parameter_name = AccessYYString(args, 0);
    float value;
    FMOD_RESULT result = fmod_studio_system->getParameterByName(parameter_name, &value);
    if (result == FMOD_OK)
    {
        ret->rvalue.val = value;
    }
    else
    {
        warning("[FMOD]: Parameter '%s' is unknown!\n", parameter_name);
        ret->rvalue.val = 0.0;
    }
}
ABI_ATTR void fmod_event_instance_set_paused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        double flag = YYGetReal(args, 1);
        FMOD_RESULT result = instance->setPaused(flag != 0.0);
        ret->rvalue.val = (result == FMOD_OK) ? 1.0 : 0.0;
    }
}
ABI_ATTR void fmod_event_instance_get_paused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        bool flag = 0;
        FMOD_RESULT result = instance->getPaused(&flag);
        ret->rvalue.val = flag ? 1.0 : 0.0;
    }
}
ABI_ATTR void fmod_event_instance_set_paused_all(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;

    double flag = YYGetReal(args, 0);
    for (auto& instance : event_instances)
    {
        FMOD_RESULT res = instance->setPaused(flag != 0.0);
        if (res != FMOD_OK)
            ret->rvalue.val = 0.0;
    }

    for (auto& instance : event_instances_oneshot)
    {
        FMOD_RESULT res = instance->setPaused(flag != 0.0);
        if (res != FMOD_OK)
            ret->rvalue.val = 0.0;
    }
}

ABI_ATTR void fmod_event_one_shot(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD_RESULT result;
    const char *event_path = AccessYYString(args, 0);
    FMOD::Studio::EventDescription* event_description = nullptr;
    FMOD::Studio::EventInstance* one_shot_instance = nullptr;

    /* Cache getEvent calls */
    const auto it = std::find_if(event_desc_list.begin(), event_desc_list.end(), 
        [&](const auto &x) { return x.first == std::string_view(event_path); });

    if (it == event_desc_list.end())
    {
        result = fmod_studio_system->getEvent(event_path, &event_description);
        if (result != FMOD_OK || event_description == nullptr)
        {
            ret->rvalue.val = 0.0;
            return;
        }

        event_desc_list[std::string(event_path)] = event_description;
    }
    else
    {
        event_description = it->second;
    }

    result = event_description->createInstance(&one_shot_instance);
    if (result != FMOD_OK || one_shot_instance == nullptr)
    {
        ret->rvalue.val = 0.0;
        return;
    }

    result = one_shot_instance->start();
    event_instances_oneshot.push_back(one_shot_instance);
    ret->rvalue.val = 1.0;
}

ABI_ATTR void fmod_event_one_shot_3d(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    FMOD_RESULT result;
    const char *event_path = AccessYYString(args, 0);
    float posX = YYGetReal(args, 1);
    float posY = YYGetReal(args, 2);
    float posZ = 0.0;
    if (argc >= 4)
        posZ = YYGetReal(args, 3);

    FMOD_3D_ATTRIBUTES attributes = { { posX, posY, posZ }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } };
    FMOD::Studio::EventDescription* event_description = nullptr;
    FMOD::Studio::EventInstance* one_shot_instance = nullptr;

    /* Cache getEvent calls */
    const auto it = std::find_if(event_desc_list.begin(), event_desc_list.end(), 
        [&](auto &x) { return x.first == std::string_view(event_path); });

    if (it == event_desc_list.end())
    {
        result = fmod_studio_system->getEvent(event_path, &event_description);
        if (result != FMOD_OK || event_description == nullptr)
        {
            ret->rvalue.val = 0.0;
            return;
        }

        event_desc_list[std::string(event_path)] = event_description;
    }
    else
    {
        event_description = it->second;
    }
    
    result = event_description->createInstance(&one_shot_instance);
    if (result != FMOD_OK || one_shot_instance == nullptr)
    {
        ret->rvalue.val = 0.0;
        return;
    }

    one_shot_instance->set3DAttributes(&attributes);
    one_shot_instance->start();
    event_instances_oneshot.push_back(one_shot_instance);
    ret->rvalue.val = 1.0;
}

ABI_ATTR void fmod_event_instance_is_playing(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        FMOD_STUDIO_PLAYBACK_STATE state;
        FMOD_RESULT result = instance->getPlaybackState(&state);
        if (result == FMOD_OK && state == FMOD_STUDIO_PLAYBACK_PLAYING)
            ret->rvalue.val = 1.0;
    }
}

ABI_ATTR void fmod_event_instance_get_timeline_pos(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        int timeline_position = 0;
        FMOD_RESULT result = instance->getTimelinePosition(&timeline_position);
        if (result == FMOD_OK)
            ret->rvalue.val = static_cast<double>(timeline_position);
    }
}

ABI_ATTR void fmod_event_instance_set_timeline_pos(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    FMOD::Studio::EventInstance *instance = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (instance)
    {
        int timeline_position = static_cast<int>(YYGetInt32(args, 1));
        FMOD_RESULT result = instance->setTimelinePosition(timeline_position);
        if (result == FMOD_OK)
            ret->rvalue.val = 1.0;
    }
}

ABI_ATTR void fmod_bank_load_sample_data(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;

    const char *bank_name = AccessYYString(args, 0);
    fs::path bank_path = bank_name;
    FMOD::Studio::Bank* bank;
    FMOD_RESULT result = fmod_studio_system->loadBankFile(bank_path.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
    if (result != FMOD_OK)
        return;

    result = bank->loadSampleData();
    if (result != FMOD_OK)
        return;

    ret->rvalue.val = 1.0;
}

ABI_ATTR void fmod_event_get_length(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    const char* event_path = AccessYYString(args, 0);
    FMOD::Studio::EventDescription* event_description = nullptr;
    FMOD_RESULT result = fmod_studio_system->getEvent(event_path, &event_description);
    if (result != FMOD_OK || event_description == nullptr)
    {
        ret->rvalue.val = 0.0;
        return;
    }

    int length = 0;
    result = event_description->getLength(&length);
    if (result != FMOD_OK)
    {
        ret->rvalue.val = 0.0;
        return;
    }

    ret->rvalue.val = static_cast<double>(length);
}

void patch_fmod(struct so_module *mod)
{
    Function_Add("fmod_init", fmod_init, 1, 0);
    Function_Add("fmod_destroy", fmod_destroy, 0, 0);
    Function_Add("fmod_bank_load", fmod_bank_load, 1, 0);
    Function_Add("fmod_update", fmod_update, 0, 0);
    Function_Add("fmod_event_create_instance", fmod_event_create_instance, 1, 0);
    Function_Add("fmod_event_instance_play", fmod_event_instance_play, 0, 0);
    Function_Add("fmod_event_instance_stop", fmod_event_instance_stop, 0, 0);
    Function_Add("fmod_event_instance_release", fmod_event_instance_release, 0, 0);
    Function_Add("fmod_event_instance_set_3d_attributes", fmod_event_instance_set_3d_attributes, 3, 0);
    Function_Add("fmod_set_listener_attributes", fmod_set_listener_attributes, 3, 0);
    Function_Add("fmod_set_num_listeners", fmod_set_num_listeners, 1, 0);
    Function_Add("fmod_event_instance_set_parameter", fmod_event_instance_set_parameter, 3, 0);
    Function_Add("fmod_event_instance_get_parameter", fmod_event_instance_get_parameter, 1, 0);
    Function_Add("fmod_set_parameter", fmod_set_parameter, 2, 0);
    Function_Add("fmod_get_parameter", fmod_get_parameter, 1, 0);
    Function_Add("fmod_event_instance_set_paused", fmod_event_instance_set_paused, 2, 0);
    Function_Add("fmod_event_instance_get_paused", fmod_event_instance_get_paused, 0, 0);
    Function_Add("fmod_event_instance_set_paused_all", fmod_event_instance_set_paused_all, 1, 0);
    Function_Add("fmod_event_one_shot", fmod_event_one_shot, 1, 0);
    Function_Add("fmod_event_one_shot_3d", fmod_event_one_shot_3d, 0, 0);
    Function_Add("fmod_event_instance_is_playing", fmod_event_instance_is_playing, 0, 0);
    Function_Add("fmod_event_instance_get_timeline_pos", fmod_event_instance_get_timeline_pos, 0, 0);
    Function_Add("fmod_event_instance_set_timeline_pos", fmod_event_instance_set_timeline_pos, 2, 0);
    Function_Add("fmod_bank_load_sample_data", fmod_bank_load_sample_data, 1, 0);
    Function_Add("fmod_event_get_length", fmod_event_get_length, 1, 0);
}

#else
struct so_module;
void patch_fmod(struct so_module *mod)
{
    /* FMOD not used, do nothing */
}

#endif