#version 450

layout(push_constant) uniform Draw {
	ivec4 rect;
	ivec2 size;
	uint colors[2];
	uint mode;
} draw;

layout(location = 0) out vec4 color;

void main()
{
	uvec2 cell = uvec2(gl_FragCoord.xy) >> 4;

	color = unpackUnorm4x8(draw.colors[draw.mode & ((cell.x ^ cell.y) & 1)]).zyxw;
}
