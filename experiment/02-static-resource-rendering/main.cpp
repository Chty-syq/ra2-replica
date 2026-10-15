#define SDL_MAIN_HANDLED

#include "shader.hpp"
#include "shp.hpp"
#include "texture.hpp"
#include "palette.hpp"
#include <glm/vec2.hpp>

constexpr int xWindowWidth = 1280;
constexpr int xWindowHeight = 720;
constexpr auto uTileSize = glm::vec2(60, 30);
const std::string xWindowTitle = "RA2 | 02 - Static Resource Rendering";


class VertexArray {
    GLuint vao_ = 0;

public:
    VertexArray() {
        glGenVertexArrays(1, &vao_);
    }

    ~VertexArray() {
        glDeleteVertexArrays(1, &vao_);
    }

    void draw() const {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

inline std::vector<Texture> loadShpTextures(const ShapeFile &shp) {
    std::vector<Texture> textures;
    for (int i = 0; i < shp.frameCnt; ++i) {
        Bytes indices;
        for (const auto &row: shp.pixels[i])
            indices.insert(indices.end(), row.begin(), row.end());
        textures.emplace_back(shp.width, shp.height, GL_RED, indices);
    }
    return textures;
}

void drawSprite(const Texture &image, const Shader &shader, const VertexArray &vao, const glm::vec2 center) {
    const auto pos = center - glm::vec2(image.width / 2, image.height / 2);
    shader.setVec2("uPosition", pos);
    shader.setVec2("uSize", glm::vec2(image.width, image.height));
    image.bind();
    vao.draw();
}


int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    auto window = SDL_CreateWindow(xWindowTitle.c_str(),SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, xWindowWidth,
                                   xWindowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    auto context = SDL_GL_CreateContext(window);

    if (!window || !context) throw std::runtime_error(SDL_GetError());
    if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) throw std::runtime_error("OpenGL函数加载失败");

    Shader gridShader("shaders/grid.vert", "shaders/grid.frag");
    Shader spriteShader("shaders/sprite.vert", "shaders/sprite.frag");
    ShapeFile ggPower("assets/ggpowr.shp");
    ShapeFile ggPowerA = ShapeFile("assets/ggpowr_a.shp");
    Palette palette("assets/unittem.pal");

    std::vector<Texture> ggPowerTextures = loadShpTextures(ggPower);
    std::vector<Texture> ggPowerATextures = loadShpTextures(ggPowerA);
    Texture paletteTexture(palette.size, 1, GL_RGBA, palette.rgba);
    VertexArray vao;

    spriteShader.use();
    spriteShader.setInt("uTexture", 0);
    spriteShader.setInt("uPaletteTexture", 1);
    spriteShader.setVec3("uRemapColor", 32.0f / 255, 105.0f / 255, 213.0f / 255);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) goto Quit;
        }

        int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GL_GetDrawableSize(window, &pixelWidth, &pixelHeight);
        glViewport(0, 0, pixelWidth, pixelHeight);

        gridShader.use();
        gridShader.setVec2("uTileSize", uTileSize.x, uTileSize.y);
        gridShader.setVec2("uWindowSize", width, height);
        gridShader.setVec2("uDrawableSize", pixelWidth, pixelHeight);
        vao.draw();

        spriteShader.use();
        spriteShader.setVec2("uWindowSize", width, height);
        paletteTexture.bind(1);
        const auto center = glm::vec2(width / 2.0f, height / 2.0f - uTileSize.y / 2.0f);
        drawSprite(ggPowerTextures[0], spriteShader, vao, center);

        SDL_GL_SwapWindow(window);
    }

Quit:
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
