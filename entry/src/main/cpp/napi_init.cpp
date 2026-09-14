#include "napi/native_api.h"
#include "terminal.h"
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/time.h>
#include <unistd.h>
#include <native_window/external_window.h>

#include "hilog/log.h"
#undef LOG_TAG
#define LOG_TAG "testTag"

static EGLDisplay egl_display;
static EGLSurface egl_surface;
static EGLContext egl_context;

// called before drawing to activate egl context
void BeforeDraw() { eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context); }

// called after drawing to swap buffers
void AfterDraw() { eglSwapBuffers(egl_display, egl_surface); }

// called when terminal want to change width
void ResizeWidth(int new_width) {}

// start a terminal
static napi_value Run(napi_env env, napi_callback_info info) {
    std::string err = Start();
    napi_value result = nullptr;
    if (err.empty()) {
        napi_get_undefined(env, &result);
        return result;
    }
    napi_create_string_utf8(env, err.c_str(), err.size(), &result);
    return result;
}

// send data to terminal
static napi_value Send(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    void *data;
    size_t length;
    napi_status ret = napi_get_arraybuffer_info(env, args[0], &data, &length);
    assert(ret == napi_ok);

    SendData((uint8_t *)data, length);
    return nullptr;
}

static napi_value CreateSurface(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int64_t surface_id = 0;
    bool lossless = true;
    napi_status res = napi_get_value_bigint_int64(env, args[0], &surface_id, &lossless);
    assert(res == napi_ok);

    // create windows and display
    OHNativeWindow *native_window;
    OH_NativeWindow_CreateNativeWindowFromSurfaceId(surface_id, &native_window);
    assert(native_window);
    EGLNativeWindowType egl_window = (EGLNativeWindowType)native_window;
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(egl_display != EGL_NO_DISPLAY);

    // initialize egl
    EGLint major_version;
    EGLint minor_version;
    EGLBoolean egl_res = eglInitialize(egl_display, &major_version, &minor_version);
    assert(egl_res == EGL_TRUE);

    const EGLint attrib[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES2_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_DEPTH_SIZE,
                             24,
                             EGL_STENCIL_SIZE,
                             8,
                             EGL_SAMPLE_BUFFERS,
                             1,
                             EGL_SAMPLES,
                             4, // Request 4 samples for multisampling
                             EGL_NONE};

    const EGLint max_config_size = 1;
    EGLint num_configs;
    EGLConfig egl_config;
    egl_res = eglChooseConfig(egl_display, attrib, &egl_config, max_config_size, &num_configs);
    assert(egl_res == EGL_TRUE);

    egl_surface = eglCreateWindowSurface(egl_display, egl_config, egl_window, NULL);

    EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attributes);

    // start render thread
    StartRender();
    return nullptr;
}

static napi_value ResizeSurface(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int width, height;
    napi_get_value_int32(env, args[1], &width);
    napi_get_value_int32(env, args[2], &height);

    Resize(width, height);
    return nullptr;
}

static napi_value Scroll(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    double offset = 0;
    napi_status res = napi_get_value_double(env, args[0], &offset);
    assert(res == napi_ok);

    ScrollBy(offset);
    return nullptr;
}

// TODO
static napi_value DestroySurface(napi_env env, napi_callback_info info) { return nullptr; }

// TODO
static pthread_mutex_t pasteboard_lock = PTHREAD_MUTEX_INITIALIZER;
static std::deque<std::string> copy_queue;
static int paste_requests = 0;
static std::deque<std::string> paste_queue;

static napi_value CheckCopy(napi_env env, napi_callback_info info) {
    napi_value res = nullptr;
    pthread_mutex_lock(&pasteboard_lock);
    if (!copy_queue.empty()) {
        std::string content = copy_queue.front();
        copy_queue.pop_front();
        napi_create_string_utf8(env, content.c_str(), content.size(), &res);
    }
    pthread_mutex_unlock(&pasteboard_lock);

    return res;
}

static napi_value CheckPaste(napi_env env, napi_callback_info info) {
    napi_value res = nullptr;
    pthread_mutex_lock(&pasteboard_lock);
    bool has_paste = false;
    if (paste_requests > 0) {
        has_paste = true;
        paste_requests --;
    }
    pthread_mutex_unlock(&pasteboard_lock);

    napi_get_boolean(env, has_paste, &res);
    return res;
}

static napi_value PushPaste(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    size_t size = 0;
    napi_status res = napi_get_value_string_utf8(env, args[0], NULL, 0, &size);
    assert(res == napi_ok);
    std::vector<char> buffer(size + 1);

    res = napi_get_value_string_utf8(env, args[0], buffer.data(), buffer.size(),
                                        &size);
    assert(res == napi_ok);
    std::string s(buffer.data(), size);

    pthread_mutex_lock(&pasteboard_lock);
    paste_queue.push_back(s);
    pthread_mutex_unlock(&pasteboard_lock);

    return nullptr;
}

void Copy(std::string base64) {
    pthread_mutex_lock(&pasteboard_lock);
    copy_queue.push_back(base64);
    pthread_mutex_unlock(&pasteboard_lock);
}

void RequestPaste() {
    pthread_mutex_lock(&pasteboard_lock);
    paste_requests++;
    pthread_mutex_unlock(&pasteboard_lock);
}

std::string GetPaste() {
    std::string res;
    pthread_mutex_lock(&pasteboard_lock);
    if (!paste_queue.empty()) {
        res = paste_queue.front();
        paste_queue.pop_front();
    }
    pthread_mutex_unlock(&pasteboard_lock);
    return res;
}

napi_value OnForeground(napi_env env, napi_callback_info info) {
    return nullptr;
}

napi_value OnBackground(napi_env env, napi_callback_info info) {
    return nullptr;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"run", nullptr, Run, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"send", nullptr, Send, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"createSurface", nullptr, CreateSurface, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"destroySurface", nullptr, DestroySurface, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"resizeSurface", nullptr, ResizeSurface, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"scroll", nullptr, Scroll, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"checkCopy", nullptr, CheckCopy, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"checkPaste", nullptr, CheckPaste, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"pushPaste", nullptr, PushPaste, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"onForeground", nullptr, OnForeground, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"onBackground", nullptr, OnBackground, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void *)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) { napi_module_register(&demoModule); }
