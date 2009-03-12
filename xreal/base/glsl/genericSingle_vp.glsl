/*
===========================================================================
Copyright (C) 2006-2008 Robert Beckebans <trebor_7@users.sourceforge.net>

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

attribute vec4		attr_Position;
attribute vec4		attr_TexCoord0;
attribute vec4		attr_Color;
#if defined(r_VertexSkinning)
attribute vec4		attr_BoneIndexes;
attribute vec4		attr_BoneWeights;
uniform int			u_VertexSkinning;
uniform mat4		u_BoneMatrix[MAX_GLSL_BONES];
#endif

uniform mat4		u_ColorTextureMatrix;
uniform int			u_InverseVertexColor;
uniform mat4		u_ModelMatrix;
//uniform mat4		u_ProjectionMatrix;
uniform mat4		u_ModelViewProjectionMatrix;

varying vec3		var_Position;
varying vec2		var_Tex;
varying vec4		var_Color;

void	main()
{
#if defined(r_VertexSkinning)
	if(bool(u_VertexSkinning))
	{
		vec4 vertex = vec4(0.0);
		
		for(int i = 0; i < 4; i++)
		{
			int boneIndex = int(attr_BoneIndexes[i]);
			float boneWeight = attr_BoneWeights[i];
			mat4  boneMatrix = u_BoneMatrix[boneIndex];
			
			vertex += (boneMatrix * attr_Position) * boneWeight;
		}

		// transform vertex position into homogenous clip-space
		gl_Position = u_ModelViewProjectionMatrix * vertex;
		
		// transform position into world space
		var_Position = (u_ModelMatrix * vertex).xyz;
	}
	else
#endif
	{
		// transform vertex position into homogenous clip-space
		gl_Position = u_ModelViewProjectionMatrix * attr_Position;
		//gl_Position = u_ProjectionMatrix * u_ModelViewMatrix * attr_Position;
		//gl_Position = u_ProjectionMatrix * attr_Position;
		
		// transform position into world space
		var_Position = (u_ModelMatrix * attr_Position).xyz;
	}
	
	// transform texcoords
	var_Tex = (u_ColorTextureMatrix * attr_TexCoord0).st;
	
	// assign color
	if(bool(u_InverseVertexColor))
	{
		var_Color.r = 1.0 - attr_Color.r;
		var_Color.g = 1.0 - attr_Color.g;
		var_Color.b = 1.0 - attr_Color.b;
		var_Color.a = 1.0 - attr_Color.a;
	}
	else
	{
		var_Color = attr_Color;
	}
}
