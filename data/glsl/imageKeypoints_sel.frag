#version 460

layout(location = 0) flat in uint v_vert_id;
layout(location = 1) flat in uint v_inst_id;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(-1., -1., uintBitsToFloat(v_inst_id), uintBitsToFloat(v_vert_id));
}