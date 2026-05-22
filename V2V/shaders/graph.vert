#version 330 core

layout(location = 0) in vec2 aPos;     // world meters (EPSG:3857)
layout(location = 1) in vec4 aColor;   // per-vertex RGBA

uniform mat4 uViewProjection;

out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
}
