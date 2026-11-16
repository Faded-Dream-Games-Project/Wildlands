#version 330
// Wildlands chunk shader (fragment) — GLSL 330
// Baked vertex shading (face/AO/skylight) * atlas texel, fog, day/night.
in vec2 fragTexCoord;
in vec4 fragColor;
in float fragDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;     // matches the sky horizon
uniform vec2 fogRange;     // x = start, y = end distance
uniform float lightScale;  // day/night brightness multiplier
uniform vec2 uvOffset;     // texture scroll (clouds); 0 for chunks

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord + uvOffset);
    if (texel.a < 0.5) discard;   // cutout: leaves, plants, glass holes

    vec4 col = texel * fragColor * colDiffuse;
    col.rgb *= lightScale;

    float fog = clamp((fragDist - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    fog = fog * fog * (3.0 - 2.0 * fog);
    col.rgb = mix(col.rgb, fogColor, fog);

    finalColor = col;
}
