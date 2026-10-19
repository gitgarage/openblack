$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
uniform vec4 u_tint;
uniform vec4 u_spriteMode; // x: 0 a one-channel texture (red is colour and alpha alike), 1 a full RGBA one

void main()
{
	vec4 texel = texture2D(s_diffuse, v_texcoord0.xy);
	gl_FragColor = mix(texel.rrrr, texel, u_spriteMode.x) * u_tint;
}
