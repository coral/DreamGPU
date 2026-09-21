#version 120
// SPDX-License-Identifier: GPL-2.0-or-later
// Private fixed-function fragment replacement for CGL's missing image borders.
// The atlas is RGBA16, nearest filtered, with complete mip images in two bands.
// levels[] starts at TEXTURE_BASE_LEVEL. Dimensions exclude supplied borders.
uniform sampler2D image;
uniform vec2 atlasSize;
uniform vec4 levels[12];
uniform int levelCount, is2D, minFilter, magFilter, wrapS, wrapT;
uniform float lodBias, minLOD, maxLOD;
uniform int baseFormat, envMode;
uniform vec4 envColor;
uniform int combineRGB, combineAlpha;
uniform int sourceRGB[3], sourceAlpha[3], operandRGB[3], operandAlpha[3];
uniform float rgbScale, alphaScale;
uniform int colorSumEnabled, fogEnabled, fogMode;
uniform float fogDensity, fogStart, fogEnd;
uniform vec4 fogColor;

// GL_CLAMP clips the coordinate before forming its linear footprint. Its edge
// footprint straddles a real image border; CLAMP_TO_EDGE never samples a border.
float wrapped(float s, int mode) {
    if (mode == 10497) return fract(s); // REPEAT
    if (mode == 33648) s = 1.0 - abs(2.0 * fract(s * 0.5) - 1.0);
    if (mode == 34626 || mode == 34627 || mode == 35090) s = abs(s);
    if (mode == 33069 || mode == 35090) return clamp(s, -1.0, 2.0);
    return clamp(s, 0.0, 1.0);
}
float indexOf(float i, float size, int mode) {
    if (mode == 10497) return mod(i, size) + 1.0;
    bool edge = mode == 33071 || mode == 33648 || mode == 34627;
    return clamp(i, edge ? 0.0 : -1.0, edge ? size - 1.0 : size) + 1.0;
}
vec4 fetch(vec4 level, vec2 p) {
    float x = indexOf(p.x, level.x, wrapS);
    float y = is2D != 0 ? indexOf(p.y, level.y, wrapT) : 0.0;
    return texture2D(image, (vec2(x + level.w, y + level.z) + 0.5) / atlasSize);
}
vec4 sampleLevel(vec2 st, int number, bool linear) {
    vec4 level = levels[number];
    vec2 uv = vec2(wrapped(st.x, wrapS), wrapped(st.y, wrapT)) * level.xy;
    if (!linear) {
        vec2 p = floor(uv);
        // NEAREST at CLAMP's upper endpoint selects the last interior texel.
        if (wrapS != 10497 && wrapS != 33069 && wrapS != 35090)
            p.x = min(p.x, level.x - 1.0);
        if (wrapT != 10497 && wrapT != 33069 && wrapT != 35090)
            p.y = min(p.y, level.y - 1.0);
        return fetch(level, p);
    }
    vec2 p = floor(uv - 0.5);
    vec2 f = fract(uv - 0.5);
    vec4 a = mix(fetch(level, p), fetch(level, p + vec2(1.0, 0.0)), f.x);
    if (is2D == 0) return a;
    vec4 b = mix(fetch(level, p + vec2(0.0, 1.0)),
                 fetch(level, p + vec2(1.0, 1.0)), f.x);
    return mix(a, b, f.y);
}
vec4 sampleImage(vec2 st) {
    // Derivatives of projected, unwrapped coordinates retain minification
    // across repeats and outside a clamped image. Both axes affect 2D LOD.
    vec2 uv = st * levels[0].xy;
    if (is2D == 0) uv.y = 0.0;
    float rho = max(length(dFdx(uv)), length(dFdy(uv)));
    float lambda = clamp(log2(max(rho, 1.0e-20)) + lodBias, minLOD, maxLOD);
    float crossover = magFilter == 9729 &&
        (minFilter == 9984 || minFilter == 9986) ? 0.5 : 0.0;
    if (lambda <= crossover) return sampleLevel(st, 0, magFilter == 9729);
    bool linear = minFilter == 9729 || minFilter == 9985 || minFilter == 9987;
    if (minFilter == 9728 || minFilter == 9729) return sampleLevel(st, 0, linear);
    float lod = clamp(lambda, 0.0, float(levelCount - 1));
    if (minFilter == 9984 || minFilter == 9985) {
        // At exact half-levels the lower image wins (GL1.1 section3.8.1).
        return sampleLevel(st, int(max(ceil(lod - 0.5), 0.0)), linear);
    }
    int lo = int(floor(lod));
    int hi = int(min(float(lo + 1), float(levelCount - 1)));
    return mix(sampleLevel(st, lo, linear), sampleLevel(st, hi, linear), fract(lod));
}
vec4 source(int which, vec4 texel, vec4 primary) {
    if (which == 5890 || which == 33984) return texel; // TEXTURE / TEXTURE0
    if (which == 34166) return envColor; // CONSTANT
    return primary; // PRIMARY_COLOR / PREVIOUS (single public texture unit)
}
vec3 rgbOperand(vec4 c, int operand) {
    if (operand == 769) return 1.0 - c.rgb;
    if (operand == 770) return vec3(c.a);
    if (operand == 771) return vec3(1.0 - c.a);
    return c.rgb;
}
float alphaOperand(vec4 c, int operand) {
    return operand == 771 ? 1.0 - c.a : c.a;
}
vec3 combineColor(vec3 a, vec3 b, vec3 c) {
    if (combineRGB == 7681) return a;
    if (combineRGB == 260) return a + b;
    if (combineRGB == 34164) return a + b - 0.5;
    if (combineRGB == 34165) return a * c + b * (1.0 - c);
    if (combineRGB == 34023) return a - b;
    if (combineRGB == 34478 || combineRGB == 34479)
        return vec3(4.0 * dot(a - 0.5, b - 0.5));
    return a * b;
}
float combineOpacity(float a, float b, float c) {
    if (combineAlpha == 7681) return a;
    if (combineAlpha == 260) return a + b;
    if (combineAlpha == 34164) return a + b - 0.5;
    if (combineAlpha == 34165) return a * c + b * (1.0 - c);
    if (combineAlpha == 34023) return a - b;
    return a * b;
}
vec4 environment(vec4 texel, vec4 primary) {
    // GetTexImage returns zero G/B for luminance and intensity. Reconstruct
    // those sampling swizzles before COMBINE. ALPHA keeps zero RGB, as specified
    // by ARB_texture_env_combine table 3.23 (legacy env modes ignore its RGB).
    bool alpha = baseFormat == 6406;
    bool intensity = baseFormat == 32841;
    bool hasAlpha = alpha || intensity || baseFormat == 6410 || baseFormat == 6408;
    if (intensity) texel = vec4(texel.r);
    else if (baseFormat == 6409 || baseFormat == 6410) texel.rgb = vec3(texel.r);
    else if (alpha) texel.rgb = vec3(0.0);
    if (envMode == 34160) { // COMBINE
        vec3 a = rgbOperand(source(sourceRGB[0], texel, primary), operandRGB[0]);
        vec3 b = rgbOperand(source(sourceRGB[1], texel, primary), operandRGB[1]);
        vec3 c = rgbOperand(source(sourceRGB[2], texel, primary), operandRGB[2]);
        float x = alphaOperand(source(sourceAlpha[0], texel, primary), operandAlpha[0]);
        float y = alphaOperand(source(sourceAlpha[1], texel, primary), operandAlpha[1]);
        float z = alphaOperand(source(sourceAlpha[2], texel, primary), operandAlpha[2]);
        vec3 rgb = combineColor(a, b, c) * rgbScale;
        float opacity = combineOpacity(x, y, z) * alphaScale;
        if (combineRGB == 34479) opacity = rgb.r;
        return clamp(vec4(rgb, opacity), 0.0, 1.0);
    }
    vec4 result = primary;
    if (envMode == 7681) { // REPLACE
        if (!alpha) result.rgb = texel.rgb;
        if (hasAlpha) result.a = texel.a;
    } else if (envMode == 8449) { // DECAL; other base formats are undefined
        if (baseFormat == 6408) result.rgb = mix(primary.rgb, texel.rgb, texel.a);
        else if (baseFormat == 6407) result.rgb = texel.rgb;
    } else if (envMode == 3042) { // BLEND
        if (!alpha) result.rgb = mix(primary.rgb, envColor.rgb, texel.rgb);
        if (intensity) result.a = mix(primary.a, envColor.a, texel.a);
        else if (hasAlpha) result.a *= texel.a;
    } else if (envMode == 260) { // ADD
        if (!alpha) result.rgb += texel.rgb;
        if (intensity) result.a += texel.a;
        else if (hasAlpha) result.a *= texel.a;
    } else { // MODULATE
        if (!alpha) result.rgb *= texel.rgb;
        if (hasAlpha) result.a *= texel.a;
    }
    return clamp(result, 0.0, 1.0);
}
void main() {
    vec2 st = gl_TexCoord[0].xy / gl_TexCoord[0].w;
    vec4 color = environment(sampleImage(st), gl_Color);
    if (colorSumEnabled != 0) color.rgb = clamp(color.rgb + gl_SecondaryColor.rgb, 0.0, 1.0);
    if (fogEnabled != 0) {
        float z = abs(gl_FogFragCoord);
        float f;
        if (fogMode == 9729) f = (fogEnd - z) / (fogEnd - fogStart);
        else if (fogMode == 2049) f = exp(-fogDensity * fogDensity * z * z);
        else f = exp(-fogDensity * z);
        color.rgb = mix(fogColor.rgb, color.rgb, clamp(f, 0.0, 1.0));
    }
    gl_FragColor = color;
}
