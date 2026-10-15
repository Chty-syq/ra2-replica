
#define SDL_MAIN_HANDLED
#include <glad/glad.h>
#include <SDL.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

const int xWindowWidth = 1280;
const int xWindowHeight = 720;
const std::string xWindowTitle = "RA2 | 01 - Setup";


class Shader {
    GLuint vertex_ = 0, fragment_ = 0, program_ = 0;

    void compile(GLuint& shader, GLenum kind, std::string path) {
        std::size_t size = 0;
        const std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(path.c_str(), &size), SDL_free);
        const std::string source = std::string(static_cast<const char*>(data.get()), size);
        const char* code = source.c_str();
        shader = glCreateShader(kind);
        glShaderSource(shader, 1, &code, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            throw std::runtime_error("Shader编译失败: " + std::string(infoLog));
        }
    }

public:
    Shader(const std::string &vPath, const std::string &fPath) {
        compile(vertex_, GL_VERTEX_SHADER, vPath);
        compile(fragment_, GL_FRAGMENT_SHADER, fPath);
        program_ = glCreateProgram();
        glAttachShader(program_, vertex_);
        glAttachShader(program_, fragment_);
        glLinkProgram(program_);

        GLint ok = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char infoLog[512];
            glGetProgramInfoLog(program_, 512, nullptr, infoLog);
            throw std::runtime_error("Shader链接失败: " + std::string(infoLog));
        }

    }

    ~Shader() {
        glDeleteProgram(program_);
        glDeleteShader(fragment_);
        glDeleteShader(vertex_);
    }

    void use() const { glUseProgram(program_); }
    void setVec2(const char* name, float x, float y) const { glUniform2f(glGetUniformLocation(program_, name), x, y); }
};

class IsometricGrid {
    GLuint vao = 0;

public:
    IsometricGrid() { glGenVertexArrays(1, &vao); }
    ~IsometricGrid() { glDeleteVertexArrays(1, &vao); }
    void draw() const {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    auto window = SDL_CreateWindow(xWindowTitle.c_str(),SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, xWindowWidth, xWindowHeight,SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    auto context = SDL_GL_CreateContext(window);

    if (!window || !context) throw std::runtime_error(SDL_GetError());
    if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) throw std::runtime_error("OpenGL函数加载失败");

    Shader shader("shaders/grid.vert", "shaders/grid.frag");
    IsometricGrid grid;

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))  goto Quit;
        }

        int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GL_GetDrawableSize(window, &pixelWidth, &pixelHeight);
        glViewport(0, 0, pixelWidth, pixelHeight);

        shader.use();
        shader.setVec2("uTileSize", 60, 30);
        shader.setVec2("uWindowSize", static_cast<float>(width), static_cast<float>(height));
        shader.setVec2("uDrawableSize", static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));
        grid.draw();
        SDL_GL_SwapWindow(window);
    }

Quit:
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}