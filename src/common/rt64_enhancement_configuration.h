//
// RT64
//

#pragma once

#include <cstdint>

#include "rt64_common.h"

namespace RT64 {
    struct EnhancementConfiguration {
        struct Framebuffer {
            bool reinterpretFixULS;
        };

        struct Presentation {
            enum class Mode {
                Console,
                SkipBuffering,
                PresentEarly
            };

            Mode mode;
            bool removeBlackBorders;
            // Optional native-pixel viewport for callers that compose the VI
            // image into a specific UI rectangle. All-zero dimensions keep
            // the regular VI-derived viewport and scissor.
            int32_t nativeViewportX;
            int32_t nativeViewportY;
            int32_t nativeViewportWidth;
            int32_t nativeViewportHeight;
        };
        
        struct Rect {
            bool fixRectLR;
        };

        struct F3DEX {
            bool forceBranch;
        };

        struct S2DEX {
            bool fixBilerpMismatch;
            bool framebufferFastPath;
        };

        struct TextureLOD {
            bool scale;
        };

        Framebuffer framebuffer;
        Presentation presentation;
        Rect rect;
        F3DEX f3dex;
        S2DEX s2dex;
        TextureLOD textureLOD;

        EnhancementConfiguration();
    };
};
