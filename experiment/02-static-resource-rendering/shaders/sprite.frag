#version 330 core

uniform sampler2D uTexture;
uniform sampler2D uPaletteTexture;
uniform vec3 uRemapColor;
uniform bool uRemapable = true; // 是否替换国家色。
in vec2 textureUV;
out vec4 fragmentColor;

// 截取 RGB 的高 5/6/5 位，再展开为 shader 的 0～1 输出。
vec3 quantizeRgb565(vec3 rgb) {
    // 微小容差抵消 40.0 被浮点运算表示为 39.999996 一类的误差。
    ivec3 rgb8 = ivec3(clamp(rgb, 0.0, 1.0) * 255.0 + 0.0001);
    ivec3 rgb565 = rgb8 >> ivec3(3, 2, 3);
    return vec3(rgb565) / vec3(31.0, 63.0, 31.0);
}

void main() {
    int paletteIndex = int(round(texture(uTexture, textureUV).r * 255.0));
    if (paletteIndex == 0) discard;

    vec4 color = texelFetch(uPaletteTexture, ivec2(paletteIndex, 0), 0);
    // 索引 16～31 替换为国家色渐变。
    if (uRemapable && paletteIndex >= 16 && paletteIndex <= 31) {
        float stage = 1.0 - float(paletteIndex - 16) / 15.0;
        color.rgb = uRemapColor * stage;
    }
    color.rgb = quantizeRgb565(color.rgb);
    fragmentColor = color;
}
