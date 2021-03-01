#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_inst_id;

layout(set = 0, binding = 0) uniform sampler2D tex[256];

layout(location = 0) out vec4 fragColor;

void main()
{
//        fragColor = vec4(v_uv, 0, 1.0);
    fragColor = texture(tex[v_inst_id], v_uv);
}