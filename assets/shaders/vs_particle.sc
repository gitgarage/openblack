$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// The particle systems' atoms (src/PSys): quads already turned to the camera, in the world's coordinates.
void main()
{
	v_texcoord0 = vec4(a_texcoord0, 0.0, 1.0);
	v_color0 = a_color0;
	gl_Position = mul(u_viewProj, vec4(a_position.xyz, 1.0));
}
