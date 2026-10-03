//
// RT64
//

#include "rt64_enhancement_configuration.h"

namespace RT64 {
    // EnhancementConfiguration
    
    EnhancementConfiguration::EnhancementConfiguration() {
        framebuffer.reinterpretFixULS = true;
        presentation.mode = Presentation::Mode::SkipBuffering;
        presentation.removeBlackBorders = true;
        presentation.nativeViewportX = 0;
        presentation.nativeViewportY = 0;
        presentation.nativeViewportWidth = 0;
        presentation.nativeViewportHeight = 0;
        rect.fixRectLR = true;
        f3dex.forceBranch = false;
        s2dex.fixBilerpMismatch = true;
        s2dex.framebufferFastPath = true;
        textureLOD.scale = false;
    }
};
