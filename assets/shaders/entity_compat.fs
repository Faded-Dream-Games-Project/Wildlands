#version 120
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying float fragDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float entityLight;
uniform float hurtFlash;
uniform vec3 fogColor;
uniform vec2 fogRange;

void main()
{
    vec4 texel = texture2D(texture0, fragTexCoord);
    if (texel.a < 0.5) discard;

    vec4 col = texel * fragColor * colDiffuse;
    col.rgb *= clamp(entityLight, 0.06, 1.0);
    col.rgb = mix(col.rgb, vec3(0.85, 0.12, 0.10), hurtFlash * 0.6);

    float fog = clamp((fragDist - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    fog = fog * fog * (3.0 - 2.0 * fog);
    col.rgb = mix(col.rgb, fogColor, fog);

    gl_FragColor = col;
}
