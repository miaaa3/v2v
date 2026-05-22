#version 330 core

// Per-vertex (static): unit triangle [-0.5..+0.5].
layout(location = 0) in vec2 aLocal;

// Per-instance.
layout(location = 1) in vec2  aInstancePos;
layout(location = 2) in float aInstanceHeading;
layout(location = 3) in vec4  aInstanceColor;
layout(location = 4) in vec2  aInstanceSize;   // (length, width) in meters

uniform mat4  uViewProjection;
uniform float uSizeScale;   // global multiplier (camera-zoom visibility clamp)

out vec4 vColor;

void main()
{
    vec2  scaled  = aLocal * aInstanceSize * uSizeScale;
    float cs      = cos(aInstanceHeading);
    float sn      = sin(aInstanceHeading);
    vec2  rotated = vec2(scaled.x * cs - scaled.y * sn,
                         scaled.x * sn + scaled.y * cs);

    vColor = aInstanceColor;
    gl_Position = uViewProjection * vec4(aInstancePos + rotated, 0.0, 1.0);
}
