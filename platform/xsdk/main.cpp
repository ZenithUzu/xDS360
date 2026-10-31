// SPDX-License-Identifier: GPL-3.0-or-later
// xDS360 first native xSDK frontend. No XDK libraries or std replacements.
#include "core.h"
#include "settings.h"
#include "xenonds/controller_mapper.hpp"
#include "xds360-xsdk_shaders.h"
#include <xsdk/gpu.h>
#include <xsdk/log.h>
#include <xsdk/present.h>
#include <xsdk/shader.h>
#include <xsdk/text.h>
#include <xsdk/thread.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>

namespace {
constexpr unsigned pixels = 256 * 384;
uint32_t frame[pixels];
FILE *report = nullptr;
using Clock = std::chrono::steady_clock;

void log(const char *message) {
    xsdk_log_write("xDS360: %s", message);
    if (report) { std::fprintf(report, "%s\n", message); std::fflush(report); }
}

struct Video {
    XsdkShader vertex{}, pixel{};
    XsdkTexture textures[2]{};
    XsdkSamplerState sampler{};
    volatile uint32_t *vertices = nullptr;
    uint32_t fences[2]{};
    uint32_t width = 0, height = 0;
    unsigned slot = 0;
    bool gpu = false, present = false, text = false, primed = false;

    bool open() {
        if (!NT_SUCCESS(xsdk_gpu_open())) return false;
        gpu = true;
        if (!NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, 2, 1, &width, &height))) return false;
        present = true;
        if (!NT_SUCCESS(xsdk_shader_load(&vertex, screen_vert.container, screen_vert.container_bytes)) ||
            !NT_SUCCESS(xsdk_shader_load(&pixel, screen_frag.container, screen_frag.container_bytes))) return false;
        XsdkVertexElement layout[2]{};
        layout[0].usage = XSDK_USAGE_POSITION; layout[0].format = XSDK_VERTEX_FLOAT2;
        layout[1].usage = XSDK_USAGE_TEXCOORD; layout[1].format = XSDK_VERTEX_FLOAT2;
        layout[1].offset_bytes = 8;
        const uint32_t stride = 16;
        if (!NT_SUCCESS(xsdk_shader_set_vertex_layout(&vertex, layout, 2, &stride))) return false;
        vertices = xsdk_gpu_alloc(12 * stride);
        if (!vertices) return false;
        float quads[12][4];
        for (unsigned screen = 0; screen < 2; ++screen) {
            const float left = (112.f + screen * 544.f - 0.5f) * 2.f / width - 1.f;
            const float right = left + 512.f * 2.f / width;
            const float top = 1.f - (168.f - 0.5f) * 2.f / height;
            const float bottom = top - 384.f * 2.f / height;
            const float v0 = screen * 0.5f, v1 = v0 + 0.5f;
            const float quad[6][4] = {{left,top,0,v0},{right,top,1,v0},{left,bottom,0,v1},
                {left,bottom,0,v1},{right,top,1,v0},{right,bottom,1,v1}};
            std::memcpy(quads + screen * 6, quad, sizeof quad);
        }
        uint32_t words[48]; std::memcpy(words, quads, sizeof words);
        for (unsigned i = 0; i < 48; ++i) vertices[i] = words[i];
        xsdk_gpu_invalidate(xsdk_gpu_address(vertices), sizeof words);
        for (auto &texture : textures) {
            if (!NT_SUCCESS(xsdk_texture_create(&texture, XSDK_FORMAT_A8R8G8B8, 256, 384, 1, false))) return false;
        }
        XsdkSampler state{};
        state.mag_filter = state.min_filter = state.mip_filter = XSDK_FILTER_POINT;
        state.address_u = state.address_v = XSDK_ADDRESS_CLAMP;
        xsdk_texture_sampler_state(&sampler, &state);
        text = NT_SUCCESS(xsdk_text_open());
        return text;
    }

    bool draw(const char *hud) {
        const unsigned next = slot++ & 1u;
        // The GPU may still be reading this texture from two frames ago.
        if (fences[next] && !NT_SUCCESS(xsdk_gpu_wait(fences[next], 5000))) return false;
        xsdk_texture_upload(&textures[next], 0, frame, 256 * sizeof(uint32_t));
        xsdk_gpu_invalidate(textures[next].address, textures[next].size_bytes);
        uint32_t address;
        if (!NT_SUCCESS(xsdk_present_begin(&address, 5000))) return false;
        xsdk_gpu_set_target(width, height);
        if (!primed) { xsdk_gpu_resolve(address, width, height, 0xFF101010u); primed = true; }
        xsdk_gpu_set_target(width, height);
        xsdk_gpu_set_shaders(&vertex, &pixel);
        xsdk_built_shader_set_literals(&screen_vert);
        xsdk_built_shader_set_literals(&screen_frag);
        xsdk_gpu_set_texture(0, &textures[next], &sampler);
        xsdk_gpu_set_vertices(0, xsdk_gpu_address(vertices), 12 * 16);
        xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, 12);
        xsdk_text_draw(16, 16, 1, 0xFFFFFFFFu, hud);
        xsdk_gpu_resolve(address, width, height, 0xFF101010u);
        fences[next] = xsdk_gpu_fence();
        xsdk_present_end();
        return true;
    }

    ~Video() {
        if (gpu) (void)xsdk_gpu_wait(xsdk_gpu_fence(), 5000);
        if (text) xsdk_text_close();
        if (present) xsdk_present_close();
        for (auto &texture : textures) if (texture.texels) xsdk_texture_free(&texture);
        if (vertices) xsdk_gpu_free(vertices);
        if (pixel.microcode) xsdk_shader_free(&pixel);
        if (vertex.microcode) xsdk_shader_free(&vertex);
        if (gpu) (void)xsdk_gpu_close();
    }
};

xenonds::ControllerSnapshot snapshot(const INPUT_GAMEPAD &pad) {
    xenonds::ControllerSnapshot s;
    const unsigned b = pad.buttons;
    s.south=b&INPUT_GAMEPAD_A; s.east=b&INPUT_GAMEPAD_B;
    s.west=b&INPUT_GAMEPAD_X; s.north=b&INPUT_GAMEPAD_Y;
    s.dpad_up=b&INPUT_GAMEPAD_DPAD_UP; s.dpad_down=b&INPUT_GAMEPAD_DPAD_DOWN;
    s.dpad_left=b&INPUT_GAMEPAD_DPAD_LEFT; s.dpad_right=b&INPUT_GAMEPAD_DPAD_RIGHT;
    s.left_shoulder=b&INPUT_GAMEPAD_LEFT_SHOULDER; s.right_shoulder=b&INPUT_GAMEPAD_RIGHT_SHOULDER;
    s.start=b&INPUT_GAMEPAD_START; s.back=b&INPUT_GAMEPAD_BACK;
    s.right_x=pad.thumb_rx; s.right_y=pad.thumb_ry; s.right_trigger=pad.right_trigger;
    return s;
}

void apply(Core &core, const xenonds::InputState &input) {
    for (int key = 0; key < 12; ++key) {
        if (input.buttons & (1u << key)) core.input.pressKey(key);
        else core.input.releaseKey(key);
    }
    if (input.touch.pressed) {
        core.spi.setTouch(input.touch.x, input.touch.y); core.input.pressScreen();
    } else { core.spi.clearTouch(); core.input.releaseScreen(); }
}

void configure() {
    Settings::basePath = "game:";
    Settings::directBoot = Settings::romInRam = 1;
    Settings::fpsLimiter = Settings::frameskip = Settings::threaded2D = 0;
    Settings::threaded3D = 2;
    Settings::highRes3D = Settings::screenGhost = Settings::screenFilter = 0;
    Settings::emulateAudio = 0; // separate audio milestone, not a dummy sink
    Settings::savesFolder = Settings::statesFolder = Settings::cheatsFolder = 0;
    Settings::dsiMode = Settings::arm7Hle = 0;
    Settings::ndsBios9Path = "game:/bios9.bin";
    Settings::ndsBios7Path = "game:/bios7.bin";
    Settings::ndsFirmPath = "game:/firmware.bin";
}

int run(Video &video) {
    configure();
    std::unique_ptr<Core> core;
    char message[256] = "Sin ROM: copia game.nds junto al XEX. BACK: salir";
    FILE *rom = std::fopen("game:/game.nds", "rb");
    if (rom) {
        std::fclose(rom);
        try { core.reset(new Core("game:/game.nds")); }
        catch (int error) { std::snprintf(message,sizeof message,"Error NooDS: %d. BACK: salir",error); }
        catch (const std::exception &error) { std::snprintf(message,sizeof message,"Error: %.160s",error.what()); }
        log(core ? "NooDS iniciado; game.nds cargado; sin audio" : message);
    } else log(message);
    for (unsigned y=0; y<384; ++y) for (unsigned x=0; x<256; ++x)
        frame[y*256+x] = y<192 ? 0xFF0000FFu : 0xFFFF0000u;
    xenonds::ControllerMapper mapper;
    unsigned emulated = 0, shown = 0;
    auto window = Clock::now(), lastSave = window;
    char hud[512]; std::snprintf(hud,sizeof hud,"xDS360 xSDK 0.1 | %s",core ? "Cargando NSMB/DS..." : message);
    const unsigned exitChord = INPUT_GAMEPAD_BACK|INPUT_GAMEPAD_START|INPUT_GAMEPAD_LEFT_SHOULDER|INPUT_GAMEPAD_RIGHT_SHOULDER;
    bool stopped = false;
    while (true) {
        INPUT_STATE state{};
        if (XamInputGetState(0,0,&state)!=ERROR_SUCCESS) state = INPUT_STATE{};
        if ((state.gamepad.buttons & exitChord)==exitChord || ((!core || stopped) && (state.gamepad.buttons&INPUT_GAMEPAD_BACK))) break;
        const auto input = mapper.map(snapshot(state.gamepad));
        if (core && !stopped) {
            apply(*core,input);
            const uint32_t before = core->completedFrames;
            try {
                unsigned slices = 0;
                do { core->runCore(); } while (core->completedFrames==before && ++slices<4096);
                if (core->completedFrames==before) throw std::runtime_error("Sin progreso de cuadros");
                emulated += core->completedFrames-before;
                (void)core->gpu.getFrame(frame,false);
            } catch (int error) {
                std::snprintf(hud,sizeof hud,"Error del nucleo: %d. BACK: salir",error); log(hud); stopped=true;
            } catch (const std::exception &error) {
                std::snprintf(hud,sizeof hud,"Error: %.180s. BACK: salir",error.what()); log(hud); stopped=true;
            }
            for (int d=-3; d<=3; ++d) {
                const int x=input.touch.x, y=input.touch.y;
                if (x+d>=0 && x+d<256) frame[(y+192)*256+x+d]=0xFF00FFFFu;
                if (y+d>=0 && y+d<192) frame[(y+d+192)*256+x]=0xFF00FFFFu;
            }
        }
        if (!video.draw(hud)) { log("Fallo de presentacion/espera GPU"); return 1; }
        ++shown;
        const auto now=Clock::now();
        const double seconds=std::chrono::duration<double>(now-window).count();
        if (seconds>=1.0 && !stopped) {
            if (core) std::snprintf(hud,sizeof hud,
                "xDS360 xSDK 0.1 | EMU %.1f FPS | VID %.1f | SIN AUDIO\nSalir: LB+RB+START+BACK | Tactil: stick derecho + RT | Reporte: %s",
                emulated/seconds,shown/seconds,report?"OK":"NO DISPONIBLE");
            else std::snprintf(hud,sizeof hud,"xDS360 xSDK 0.1 | %s\nReporte: %s",message,report?"OK":"NO DISPONIBLE");
            log(hud); emulated=shown=0; window=now;
        }
        if (core && now-lastSave>=std::chrono::seconds(5)) { core->cartridgeNds.writeSave(); lastSave=now; }
    }
    if (core) core->cartridgeNds.writeSave();
    return 0;
}
} // namespace

int main() {
    report=std::fopen("game:/xds360-report.txt","w");
    log("xDS360 xSDK 0.1 | NooDS | requested CPU 0, render workers 2/4; audio disabled");
    (void)xsdk_thread_move(0);
    int result=1;
    {
        Video video;
        if (video.open()) result=run(video);
        else log("No se pudo inicializar video/shaders/texto");
    }
    if (report) { std::fclose(report); report=nullptr; }
    return result;
}
