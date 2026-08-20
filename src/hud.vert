#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform hud_push {
    vec2 inverse_extent;
} push;

void main()
{
    vec2 clip = in_position * push.inverse_extent * 2.0 - 1.0;
    gl_Position = vec4(clip.x, clip.y, 0.0, 1.0);
    out_color = in_color;
}
