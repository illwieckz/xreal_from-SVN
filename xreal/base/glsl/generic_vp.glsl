/*
===========================================================================
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/* generic_vp.glsl */

attribute vec4		attr_Position;
attribute vec4		attr_TexCoord0;
attribute vec3		attr_Normal;
attribute vec4		attr_Color;

attribute vec4		attr_Position2;
attribute vec3		attr_Normal2;

//#if defined(r_VertexSkinning)
attribute vec4		attr_BoneIndexes;
attribute vec4		attr_BoneWeights;
uniform int			u_VertexSkinning;
uniform mat4		u_BoneMatrix[MAX_GLSL_BONES];
//#endif

uniform float		u_VertexInterpolation;

uniform mat4		u_ColorTextureMatrix;
uniform vec3		u_ViewOrigin;
uniform int			u_TCGen_Environment;
uniform int			u_DeformGen;
uniform vec4		u_DeformWave;	// [base amplitude phase freq]
uniform vec3		u_DeformBulge;	// [width height speed]
uniform float		u_DeformSpread;
uniform float		u_Time;
uniform vec4		u_ColorModulate;
uniform vec4		u_Color;
uniform mat4		u_ModelMatrix;
uniform mat4		u_ModelViewProjectionMatrix;

varying vec3		var_Position;
varying vec2		var_Tex;
varying vec4		var_Color;





void	main()
{
	vec4 position = vec4(0.0);
	vec3 normal = vec3(0.0);

#if defined(USE_VERTEX_SKINNING)
	{
		for(int i = 0; i < 4; i++)
		{
			int boneIndex = int(attr_BoneIndexes[i]);
			float boneWeight = attr_BoneWeights[i];
			mat4  boneMatrix = u_BoneMatrix[boneIndex];
			
			position += (boneMatrix * attr_Position) * boneWeight;
			normal += (boneMatrix * vec4(attr_Normal, 0.0)).xyz * boneWeight;
		}
	}
#elif defined(USE_VERTEX_ANIMATION)
	{
		if(u_VertexInterpolation > 0.0)
		{
			VertexAnimation_P_N(attr_Position, attr_Position2,
								attr_Normal, attr_Normal2,
								u_VertexInterpolation,
								position, normal);
		}
		else
		{
			position = attr_Position;
			normal = attr_Normal;
		}
	}
#else
	{
		position = attr_Position;
		normal = attr_Normal;
	}
#endif
	
#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(	u_DeformGen,
								u_DeformWave,	// [base amplitude phase freq]
								u_DeformBulge,	// [width height speed]
								u_DeformSpread,
								u_Time,
								position,
								normal,
								attr_TexCoord0.st);
#endif

	// transform vertex position into homogenous clip-space
	gl_Position = u_ModelViewProjectionMatrix * position;
	
	// transform position into world space
	var_Position = (u_ModelMatrix * position).xyz;
	
	// transform texcoords
	vec4 texCoord;
#if defined(USE_TCGEN_ENVIRONMENT)
	{
		vec3 viewer = normalize(u_ViewOrigin - position.xyz);

		float d = dot(attr_Normal, viewer);

		vec3 reflected = attr_Normal * 2.0 * d - viewer;
		
		texCoord.s = 0.5 + reflected.y * 0.5;
		texCoord.t = 0.5 - reflected.z * 0.5;
		texCoord.q = 0;
		texCoord.w = 1;
	}
#else
	texCoord = attr_TexCoord0;
#endif
	
	var_Tex = (u_ColorTextureMatrix * texCoord).st;
	
	var_Color = attr_Color * u_ColorModulate + u_Color;
}
