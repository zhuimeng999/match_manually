#version 460

layout(location = 0) in vec2 position;
layout(location = 1) in mat4 model;
layout(location = 5) in vec2 imageSize;
layout(location = 6) in float depth;

//in int gl_InstanceIndex;

out gl_PerVertex {
    vec4 gl_Position;
//    float gl_PointSize;
//    float gl_ClipDistance[];
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_inst_id;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec2 windowSize;
};

//const vec4 quadVert[] = { // Y up, front = CW
//{-1, -1, 0.5, 1.},
//{-1, 1, 0.5, 1.},
//{1, -1, 0.5, 1.},
//{1, 1, 0.5, 1.}
//};

void main()
{
    vec4 out_pos = mvp*model*vec4((position - 0.5)*imageSize, depth, 1.);
    out_pos.xy = out_pos.xy/windowSize;
    gl_Position = out_pos;
    v_uv = position;
    v_inst_id = gl_InstanceIndex;
}