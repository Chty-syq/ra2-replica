#version 330 core

uniform vec2 uWindowSize;
uniform vec2 uPosition;  // 图片完整画布的左上角，单位为窗口单位。
uniform vec2 uSize;
out vec2 textureUV;

const vec2 corners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0)
);

void main() {
    textureUV = corners[gl_VertexID];
    vec2 screen = uPosition + textureUV * uSize;
    vec2 ndc = screen / uWindowSize * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
