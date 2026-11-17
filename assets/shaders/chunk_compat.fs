#version 120
// Wildlands chunk shader (fragment) — fallback for pre-330 contexts
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying float fragDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform vec2 fogRange;
uniform float lightScale;
uniform vec2 uvOffset;

void main()
{
    vec4 texel = texture2D(texture0, fragTexCoord + uvOffset);
    if (texel.a < 0.5) discard;

    float light = max(fragColor.r, fragColor.g * lightScale);
    light = clamp(light, 0.045, 1.0);
    vec4 col = texel * colDiffuse;
    col.rgb *= light;

    float fog = clamp((fragDist - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    fog = fog * fog * (3.0 - 2.0 * fog);
    col.rgb = mix(col.rgb, fogColor, fog);

    gl_FragColor = vec4(col.rgb, texel.a * fragColor.a);
}
