#version 330
// Wildlands chunk shader (vertex) — GLSL 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

out vec2 fragTexCoord;
out vec4 fragColor;
out float fragDist;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec4 viewPos = matView * (matModel * vec4(vertexPosition, 1.0));
    fragDist = length(viewPos.xyz);
    gl_Position = matProjection * viewPos;
}
