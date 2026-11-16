#version 120
// Wildlands chunk shader (vertex) — fallback for pre-330 contexts
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

varying vec2 fragTexCoord;
varying vec4 fragColor;
varying float fragDist;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec4 viewPos = matView * (matModel * vec4(vertexPosition, 1.0));
    fragDist = length(viewPos.xyz);
    gl_Position = matProjection * viewPos;
}
