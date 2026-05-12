#include "gl_loader.h"

#include <SDL.h>

#include <stdexcept>

void ensureOpenGlLoaded() {
  static bool loaded = false;
  if (loaded) {
    return;
  }

  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
    throw std::runtime_error("Failed to initialize GLAD with SDL_GL_GetProcAddress");
  }

  loaded = true;
}
