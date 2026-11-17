#version 330
// Wildlands entity shader (fragment) — GLSL 330
// Mobs, dropped items, held items: per-draw uniform light (sampled from
// the voxel light at the entity position) + hurt flash + fog.
in vec2 fragTexCoord;
in vec4 fragColor;
in float fragDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float entityLight;  // combined 0..1 light at the entity position
uniform float hurtFlash;    // 0..1 red mix while being hurt
uniform vec3 fogColor;
uniform vec2 fogRange;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    if (texel.a < 0.5) discard;

    vec4 col = texel * fragColor * colDiffuse;
    col.rgb *= clamp(entityLight, 0.06, 1.0);
    col.rgb = mix(col.rgb, vec3(0.85, 0.12, 0.10), hurtFlash * 0.6);

    float fog = clamp((fragDist - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    fog = fog * fog * (3.0 - 2.0 * fog);
    col.rgb = mix(col.rgb, fogColor, fog);

    finalColor = col;
}
