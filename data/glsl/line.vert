#version 460

layout(location = 0) in mat4 model;
layout(location = 4) in vec3 color;
layout(location = 5) in float depth;
layout(location = 6) in vec2 position;
layout(location = 7) in vec2 imageSize;

//in int gl_InstanceIndex;

out gl_PerVertex {
    vec4 gl_Position;
//    float gl_PointSize;
//    float gl_ClipDistance[];
};

layout(location = 0) out vec3 v_color;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec2 windowSize;
    float pointSize;
};

void main()
{
    vec4 out_pos = mvp*model*vec4((position - 0.5)*imageSize, depth - 1e-7, 1.);
    out_pos.xy = out_pos.xy/windowSize;
    gl_Position = out_pos;
    v_color = color;
}