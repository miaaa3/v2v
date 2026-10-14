#version 330 core

// Per-vertex (static): unit triangle in local space [-0.5 .. +0.5].
// Local +X is the vehicle's forward direction (the arrow tip).
layout(location = 0) in vec2 aLocal;

// Per-instance.
layout(location = 1) in vec2  aInstancePos;
layout(location = 2) in float aInstanceHeading;
layout(location = 3) in vec4  aInstanceColor;
layout(location = 4) in vec2  aInstanceSize;   // (length, width) in meters
layout(location = 5) in vec4  aInstanceFx;     // x=brake y=emergency z=v2vGlow w=-

uniform mat4  uViewProjection;
uniform float uSizeScale;    // global zoom-visibility multiplier

out vec2 vLocal;             // local coord, forwarded for fragment-side effects
out vec4 vColor;
out vec4 vFx;

void main()
{
    vec2  scaled = aLocal * aInstanceSize * uSizeScale;

    float cs = cos(aInstanceHeading);
    float sn = sin(aInstanceHeading);
    vec2  rotated = vec2(scaled.x * cs - scaled.y * sn,
                         scaled.x * sn + scaled.y * cs);

    vLocal = aLocal;
    vColor = aInstanceColor;
    vFx    = aInstanceFx;
    gl_Position = uViewProjection * vec4(aInstancePos + rotated, 0.0, 1.0);
}
