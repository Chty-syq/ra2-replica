#version 330 core

// 三个顶点覆盖整个窗口，顶点位置由 gl_VertexID 索引，无需顶点缓冲。
const vec2 positions[6] = vec2[6](
  vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0)
);

void main() {
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
