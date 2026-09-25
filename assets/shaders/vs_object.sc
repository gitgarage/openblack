#ifdef USE_INSTANCING
$input a_position, a_texcoord0, a_normal, a_indices, i_data0, i_data1, i_data2, i_data3, i_data4
#else
$input a_position, a_texcoord0, a_normal, a_indices
#endif // USE_INSTANCING
$output v_position, v_texcoord0, v_normal

#if BGFX_SHADER_LANGUAGE_HLSL == 3
#define BGFX_CONFIG_MAX_BONES 48
#else
#define BGFX_CONFIG_MAX_BONES 128
#endif

#include <bgfx_shader.sh>

#ifdef USE_HEIGHT_MAP
SAMPLER2D(s_heightmap, 1);
uniform vec4 u_islandExtent;
#endif // USE_HEIGHT_MAP

#ifdef USE_INSTANCING
// Bone matrices for instanced draws come from a float texture rather than u_model: one row per pose, four
// texels (matrix columns) per bone. The Switch's GLES driver kept only the first matrix of u_model.
// The row is carried in the instance transform's first column's w, which is otherwise always 0.
SAMPLER2D(s_bones, 2);
uniform vec4 u_bonesSize; // texture width, height in texels
#endif // USE_INSTANCING

void main()
{
	// Unpack
#ifdef USE_HEIGHT_MAP
	vec2 extentMin = u_islandExtent.xy;
	vec2 extentMax = u_islandExtent.zw;
#endif // USE_HEIGHT_MAP

	// The bone index is a float in the vertex data on every backend (see L3DSubMesh's EnhancedL3DVertex).
	int modelIndex = int(max(0.0f, a_indices.x));

#ifdef USE_INSTANCING
	float boneV = (i_data0.w + 0.5f) / u_bonesSize.y;
	float boneU = (float(modelIndex) * 4.0f + 0.5f) / u_bonesSize.x;
	float texelU = 1.0f / u_bonesSize.x;
	mat4 bone = mtxFromCols(
	    texture2DLod(s_bones, vec2(boneU, boneV), 0.0f),
	    texture2DLod(s_bones, vec2(boneU + texelU, boneV), 0.0f),
	    texture2DLod(s_bones, vec2(boneU + 2.0f * texelU, boneV), 0.0f),
	    texture2DLod(s_bones, vec2(boneU + 3.0f * texelU, boneV), 0.0f));
	v_position = mul(bone, vec4(a_position.xyz, 1.0f));

	mat4 model;
	model[0] = vec4(i_data0.xyz, 0.0f);
	model[1] = i_data1;
	model[2] = i_data2;
	model[3] = i_data3;

	v_position = instMul(model, v_position);
#else
	v_position = mul(u_model[modelIndex], vec4(a_position.xyz, 1.0f));
#endif // USE_INSTANCING

#ifdef USE_HEIGHT_MAP
#ifdef USE_INSTANCING
	float original_height = i_data3.y;
#else
    float original_height = u_model[modelIndex][3].y;
#endif // USE_INSTANCING
	vec2 blockUv = (v_position.xz - extentMin) / (extentMax - extentMin);
	float terrain_height = texture2DLod(s_heightmap, blockUv, 0.0f).r * 170.85f;
	v_position.y += terrain_height - original_height;
#endif // USE_HEIGHT_MAP

	v_texcoord0 = vec4(a_texcoord0, 0.0f, 0.0f);
	v_normal = a_normal;
	gl_Position = mul(u_viewProj, v_position);
}
