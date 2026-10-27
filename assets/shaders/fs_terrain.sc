$input v_texcoord0, v_texcoord1, v_weight, v_materialID0, v_materialID1, v_materialBlend, v_lightLevel, v_waterAlpha, v_distToCamera

#include <bgfx_shader.sh>

#define M_PI 3.1415926535897932384626433832795

SAMPLER2DARRAY(s0_materials, 0);
SAMPLER2D(s1_bump, 1);
SAMPLER2D(s2_smallBump, 2);
SAMPLER2D(s3_footprints, 3);

uniform vec4 u_skyAndBump;

// Light colour for the time of day, on the sky's scale: 0 night, 1 dusk, 2 day. The warm dusk and cool
// night colours are approximations matched by eye to the original game, not values from its data.
vec3 timeOfDayTint(float skyType)
{
	vec3 night = vec3(0.55f, 0.6f, 0.85f);
	// Dusk measured on the opening's sand (the original's 150, 82, 51 against ours 128, 68, 33 with 1, 0.66, 0.44).
	vec3 dusk = vec3(1.17f, 0.79f, 0.68f);
	// Day measured on the opening's path (the original's sand 139, 120, 68 against ours 151, 128, 77 at 1, 1, 1).
	vec3 day = vec3(0.9f, 0.93f, 0.92f);
	return skyType < 1.0f ? mix(night, dusk, skyType) : mix(dusk, day, skyType - 1.0f);
}

void main()
{
	// unpack uniforms
	float skyType = u_skyAndBump.x;
	// From dawn to day the light changes slowly at first, then quickly (eased, matched to the original's opening).
	if (skyType > 1.0f)
	{
		skyType = 1.0f + (skyType - 1.0f) * (skyType - 1.0f);
	}
	float bumpMapStrength = u_skyAndBump.y;
	float smallBumpMapStrength = u_skyAndBump.z;

	// do each vert with both materials
	vec4 colOne = mix(
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID0.r)),
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID1.r)),
		v_materialBlend.r
	) * v_weight.r;
	vec4 colTwo = mix(
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID0.g)),
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID1.g)),
		v_materialBlend.g
	) * v_weight.g;
	vec4 colThree = mix(
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID0.b)),
		texture2DArray(s0_materials, vec3(v_texcoord0.xy, v_materialID1.b)),
		v_materialBlend.b
	) * v_weight.b;

	// add the 3 blended textures together
	vec4 col = colOne + colTwo + colThree;

	// apply bump map (2x because it's half bright?) - not at all with the camera high over the land (Renderer.cpp)
	if (bumpMapStrength > 0.0f) {
		float bump = mix(1.0f, texture2D(s1_bump, v_texcoord0.xy).r * 2.0f, bumpMapStrength);
		col = col * bump;
	}

	// don't apply smallbump unless we're close
	if (v_distToCamera < 200.0f) {
		float smallStrength = (1.0f - (v_distToCamera / 200.0f)) * smallBumpMapStrength;

		float smallbump = 1.0f - mix(0.0f, texture2D(s2_smallBump, v_texcoord0.xy * 10.0f).r, smallStrength);
		col = col * smallbump;
	}

	vec4 footprints = texture2D(s3_footprints, v_texcoord1.xy);
	col.rgb = mix(col.rgb, footprints.rgb, footprints.a);

	// apply light map
	float skyBightness = skyType / 2.0f;
	col = col * mix(0.25f, clamp(v_lightLevel * 2.0f, 0.5f, 1.0f), skyBightness);

	col.rgb = col.rgb * timeOfDayTint(skyType);

	gl_FragColor = vec4(col.rgb, v_waterAlpha);

	//gl_FragColor.r = v_distToCamera / 200.0f;

	if (v_waterAlpha == 0.0f) {
		discard;
	}
}
