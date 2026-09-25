$input v_position, v_texcoord0, v_normal

#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
uniform vec4 u_skyAlphaThreshold;

// Light colour for the time of day, on the sky's scale: 0 night, 1 dusk, 2 day. The warm dusk and cool
// night colours are approximations matched by eye to the original game, not values from its data.
vec3 timeOfDayTint(float skyType)
{
	vec3 night = vec3(0.55f, 0.6f, 0.85f);
	vec3 dusk = vec3(1.0f, 0.66f, 0.44f);
	return skyType < 1.0f ? mix(night, dusk, skyType) : mix(dusk, vec3(1.0f, 1.0f, 1.0f), skyType - 1.0f);
}

void main()
{
	// constants
	const vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);
	const vec4 lightPos = vec4(-4000.0f, 1300.0f, -1435.0f, 1.0f);
	const float ambientStrength = 0.25f;

	// unpack uniforms
	float skyType = u_skyAlphaThreshold.x;
	float alphaThreshold = u_skyAlphaThreshold.y;
	// A building under construction: only built up to this world height yet (z is 1 when clipping).
	if (u_skyAlphaThreshold.z > 0.5f && v_position.y > u_skyAlphaThreshold.w)
	{
		discard;
	}

	float skyBightness = skyType / 2.0f;

	// ambient
	vec3 ambient = (skyBightness * 0.25f + ambientStrength) * lightColor;

	// diffuse
	vec3 norm = normalize(v_normal);
	vec4 lightDir = normalize(lightPos - v_position);
	float diff = max(dot(v_normal, lightDir.xyz), 0.0);
	vec3 diffuse = skyBightness * diff * lightColor * ( 1.0f - ambientStrength);

	vec4 diffuseTex = texture2D(s_diffuse, v_texcoord0.xy);
	diffuseTex.rgb = diffuseTex.rgb * (ambient + diffuse) * timeOfDayTint(skyType);
	if (diffuseTex.a <= alphaThreshold)
	{
		discard;
	}
	gl_FragColor = diffuseTex;
}
