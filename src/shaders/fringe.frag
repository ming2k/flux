#version 450

layout(location = 0) in float in_coverage;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform flux_fringe_pc {
    vec2 surface_size;
    vec2 pad;
    vec4 color;
} pc;

void main(void)
{
    out_color = pc.color * in_coverage;
}
