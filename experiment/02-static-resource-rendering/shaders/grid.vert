#version 330 core

// 六个顶点组成两个三角形，覆盖整个窗口。
const vec2 positions[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
    vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
