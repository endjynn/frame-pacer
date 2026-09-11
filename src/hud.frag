#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(location = 0) out vec4 out_color;

void main()
{
    float coverage = in_uv.x < 0.0 ? 1.0 : texture(atlas, in_uv).r;
    out_color = vec4(in_color.rgb, in_color.a * coverage);
}
