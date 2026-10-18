#version 450

layout(push_constant) uniform Draw {
	ivec4 rect;
	ivec2 size;
	uint colors[2];
	uint mode;
} draw;

void main()
{
	vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
	vec2 position = vec2(draw.rect.xy) + corner * vec2(draw.rect.zw);

	gl_Position = vec4(position / vec2(draw.size) * 2.0 - 1.0, 0.0, 1.0);
}
