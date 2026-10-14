#version 330 core

// Local triangle coord: x = +0.5 front tip, -0.5 rear edge; y = side.
in vec2 vLocal;
in vec4 vColor;
in vec4 vFx;        // x = brake [0..1], y = emergency (0/1), z = v2v glow [0..1]

out vec4 FragColor;

uniform float uTime;         // seconds — animates flashers / pulses
uniform float uNight;        // 0 = day .. 1 = night — drives headlights

// Soft radial spot: 1 at centre `c`, fading to 0 at radius `r`.
float spot(vec2 p, vec2 c, float r)
{
    return 1.0 - smoothstep(0.0, r, length(p - c));
}

void main()
{
    vec2  p   = vLocal;                 // [-0.5 .. 0.5]
    vec3  col = vColor.rgb;
    float a   = vColor.a;

    // Gentle top-down shading — the front reads a touch brighter.
    col *= 0.82 + 0.30 * smoothstep(-0.5, 0.5, p.x);

    // ---- Headlights: warm glow over the front, mostly on at night ------
    float head = smoothstep(0.06, 0.5, p.x);
    head *= head;
    col += vec3(1.0, 0.93, 0.70) * head * (0.12 + 0.85 * uNight);

    // ---- Tail / brake lights: two red spots on the rear corners --------
    float brake = clamp(vFx.x, 0.0, 1.0);
    float tail  = spot(p, vec2(-0.40,  0.30), 0.17)
                + spot(p, vec2(-0.40, -0.30), 0.17);
    col += vec3(1.0, 0.06, 0.04) * tail * (0.28 + 0.72 * brake);

    // ---- Emergency beacon: alternating red / blue flasher --------------
    if (vFx.y > 0.5) {
        float ph    = fract(uTime * 2.2);
        float isRed = step(ph, 0.5);
        vec3  beac  = mix(vec3(0.15, 0.40, 1.0), vec3(1.0, 0.10, 0.10), isRed);
        col += beac * spot(p, vec2(0.0, 0.0), 0.34) * 1.5;
    }

    // ---- V2V broadcast glow: cyan pulse over the whole car -------------
    float glow = clamp(vFx.z, 0.0, 1.0);
    if (glow > 0.001) {
        float pulse = 0.55 + 0.45 * sin(uTime * 7.0);
        col += vec3(0.25, 0.78, 1.0) * glow * pulse;
        a    = max(a, 0.5 + 0.5 * glow);
    }

    FragColor = vec4(clamp(col, 0.0, 1.0), a);
}
