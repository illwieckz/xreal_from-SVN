/*
===========================================================================
Copyright (C) 2008 Robert Beckebans <trebor_7@users.sourceforge.net>

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

uniform sampler2D	u_DiffuseMap;
uniform sampler2D	u_NormalMap;
uniform sampler2D	u_SpecularMap;
uniform vec3		u_ViewOrigin;
uniform float		u_SpecularExponent;

varying vec3		var_Vertex;
varying vec4		var_TexDiffuseNormal;
varying vec2		var_TexSpecular;
varying vec3		var_LightDir;
varying vec4		var_Color;
varying mat3		var_OS2TSMatrix;

void	main()
{
	// compute view direction in tangent space
	vec3 V = normalize(var_OS2TSMatrix * (u_ViewOrigin - var_Vertex));

	// compute light direction in tangent space
	vec3 L = normalize(var_OS2TSMatrix * var_LightDir);
	
	// compute half angle in tangent space
	vec3 H = normalize(L + V);
	
	// compute normal in tangent space from normalmap
	vec3 N = 2.0 * (texture2D(u_NormalMap, var_TexDiffuseNormal.pq).xyz - 0.5);
	#if defined(r_NormalScale)
	N.z *= r_NormalScale;
	normalize(N);
	#endif
	
	// compute the diffuse term
	vec4 diffuse = texture2D(u_DiffuseMap, var_TexDiffuseNormal.st);
	diffuse.rgb *= var_Color.rgb * clamp(dot(N, L), 0.0, 1.0);
	
	// compute the specular term
	vec3 specular = texture2D(u_SpecularMap, var_TexSpecular).rgb * var_Color.rgb * pow(clamp(dot(N, H), 0.0, 1.0), r_SpecularExponent) * r_SpecularScale;
	
	// compute final color
	vec4 color = diffuse;
	color.rgb += specular;
	
	gl_FragColor = color;
}
