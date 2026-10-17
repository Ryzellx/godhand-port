#version 450
// slice shader vertex — posisi piksel GS (y-down) -> NDC Vulkan (y-up)
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(push_constant) uniform PC { vec2 screen; } pc;
layout(location = 0) out vec4 vColor;
void main() {
    vec2 ndc = vec2(inPos.x / pc.screen.x * 2.0 - 1.0,
                    1.0 - inPos.y / pc.screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = inColor;
}
