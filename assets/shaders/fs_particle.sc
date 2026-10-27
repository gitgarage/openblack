$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);

void main()
{
	gl_FragColor = texture2D(s_diffuse, v_texcoord0.xy) * v_color0;
}
