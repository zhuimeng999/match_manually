#version 460

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 2) in mat4 model;
layout(location = 6) in vec2 imageSize;
layout(location = 7) in float depth;

//in int gl_InstanceIndex;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
//    float gl_ClipDistance[];
};

layout(push_constant) uniform PC {
    mat4 mvp;
    vec2 windowSize;
    float pointSize;
};

layout(location = 0) flat out vec4 v_color;

void main()
{
    vec4 out_pos = mvp*model*vec4((position - 0.5)*imageSize, depth - 1e-7, 1.);
    out_pos.xy = out_pos.xy/windowSize;
    gl_Position = out_pos;
    gl_PointSize = pointSize;
    v_color = color;
}