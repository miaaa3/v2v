#version 330 core

in  vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4      uTint;   // multiplied with sampled color (use vec4(1) for none)

void main()
{
    vec4 c = texture(uTexture, vUV);
    FragColor = c * uTint;
}
