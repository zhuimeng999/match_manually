#version 460

layout(location = 0) flat in vec4 v_color;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = v_color;
}