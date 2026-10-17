#version 450
// slice shader fragment — warna vertex polos (MVP tanpa tekstur/alpha blend)
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = vColor; }
