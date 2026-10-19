$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_sampleRect;
uniform vec4 u_spriteMode; // y: 0 turned to face the camera, 1 lying flat in the model's own XZ plane (a shadow)

void main()
{
	// Plane position to UV
	v_texcoord0.xy = vec2(a_position.x * 0.5f + 0.5f, 0.5f - a_position.y * 0.5f);
	// Zoom on section of sprite to render
	v_texcoord0.xy = v_texcoord0.xy * u_sampleRect.xy + u_sampleRect.zw;

	vec3 translation = mul(u_model[0], vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
	vec4 position = a_position;
	// Apply scaling
	vec3 scaled = mul(u_model[0], vec4(position.xyz, 0.0)).xyz;
	// Undo camera rotation so sprite faces camera
	vec3 facing = mul(u_invView, vec4(scaled, 0.0)).xyz;
	// Or flat: the plane's x and y laid along the model's x and z (its rotation tilts it to the ground under it)
	vec3 lying = mul(u_model[0], vec4(a_position.x, 0.0, a_position.y, 0.0)).xyz;
	position.xyz = mix(facing, lying, u_spriteMode.y);
	// Apply translation
	position.xyz += translation;
	gl_Position = mul(u_viewProj, position);
}
