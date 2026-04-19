#version 320 es
precision highp float;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;

layout(location = 0) uniform vec4 uStretch;

out vec2 vUv;
out float vHorizWarp;

/**
 * vStretch: x=u_scale_center, y=u_blend_edges, z=source_aspect (w/h), w=viewport_aspect (w/h)
 */
void main() {
    vec2 pos = inPos;
    vec2 uv = inUv;

    float srcAspect = max(uStretch.z, 1e-4);
    float dstAspect = max(uStretch.w, 1e-4);

    float pillar = dstAspect / srcAspect;
    float pillarClamped = clamp(pillar, 0.5, 3.0);

    float nx = uv.x * 2.0 - 1.0;
    float guard = uStretch.x;
    float edgeBlend = uStretch.y;

    float band = smoothstep(guard - edgeBlend, guard + edgeBlend, abs(nx));
    float stretchFactor = mix(1.0, pillarClamped, band);

    float ax = nx / stretchFactor;
    float warpedUvX = clamp(ax * 0.5 + 0.5, 0.0, 1.0);

    vec2 warpedUv = vec2(warpedUvX, uv.y);

    float horizWarp = band;
    vUv = warpedUv;
    vHorizWarp = horizWarp;
    gl_Position = vec4(pos, 0.0, 1.0);
}
