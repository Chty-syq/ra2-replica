#version 330 core

uniform vec2 uWindowSize;
uniform vec2 uDrawableSize;
uniform vec2 uTileSize;
out vec4 fragmentColor;

void main() {
  vec2 screen = gl_FragCoord.xy * uWindowSize / uDrawableSize;
  screen.y = uWindowSize.y - screen.y;
  vec2 origin = uWindowSize * 0.5;
  vec2 projected = (screen - origin) / (uTileSize * 0.5);
  vec2 logical = vec2(projected.y + projected.x, projected.y - projected.x) * 0.5;
  vec2 cell = floor(logical + 0.5);

  float alternate = mod(cell.x + cell.y, 2.0);
  vec3 ground = mix(vec3(0.41, 0.42, 0.45), vec3(0.46, 0.47, 0.50), alternate);

  // 格子边界位于半整数处。fwidth 把逻辑距离换算为像素宽度，平滑斜线边缘。
  vec2 distanceToEdge = abs(fract(logical) - 0.5);
  vec2 pixelsToEdge = distanceToEdge / max(fwidth(logical), vec2(0.00001));
  float line = 1.0 - smoothstep(0.0, 1.0, min(pixelsToEdge.x, pixelsToEdge.y));
  fragmentColor = vec4(mix(ground, vec3(0.78, 0.80, 0.84), line * 0.55), 1.0);
}
