#version 320 es
precision highp float;

in vec2 vUv;
in float vHorizWarp;

layout(location = 1) uniform sampler2D uFramebuffer;

layout(location = 0) out vec4 outColor;

/**
 * Intelligent 4:3 gameplay region stays near-uniform scale; outer horizontal bands absorb
 * additional stretch so circular UI elements remain round in the central band.
 */
void main() {
    vec3 fb = texture(uFramebuffer, vUv).rgb;

    vec3 vignette = vec3(1.0 - 0.035 * vHorizWarp);

    float edgeBleed = smoothstep(0.35, 0.95, abs(vUv.x * 2.0 - 1.0));
    vec3 tuned = fb * vignette * (1.0 - 0.02 * edgeBleed);

    outColor = vec4(tuned, 1.0);
}
