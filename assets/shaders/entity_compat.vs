#version 120
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying float fragDist;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec4 viewPos = matView * (matModel * vec4(vertexPosition, 1.0));
    fragDist = length(viewPos.xyz);
    gl_Position = matProjection * viewPos;
}
