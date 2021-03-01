#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_inst_id;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(v_uv, uintBitsToFloat(v_inst_id), uintBitsToFloat(0xFFFFFFFFu));
}