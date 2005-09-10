/// ============================================================================
/*
Copyright (C) 2004 Robert Beckebans <trebor_7@users.sourceforge.net>
Please see the file "AUTHORS" for a list of contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
/// ============================================================================

uniform sampler2D	u_DiffuseMap;
uniform sampler2D	u_LightMap;
//uniform sampler2D	u_DeluxeMap;

varying vec3		var_Normal;
varying vec2		var_TexDiffuse;
varying vec2		var_TexLight;

void	main()
{
	// compute normal
	vec3 N = normalize(var_Normal);
	
	// compute light direction from object space deluxe map
//	vec3 L = normalize(2.0 * (texture2D(u_DeluxeMap, var_tex_deluxe).xyz - 0.5));
	
	// compute light color from object space lightmap
	vec3 C = texture2D(u_LightMap, var_TexLight).rgb;
	
	// compute the diffuse term
	vec4 diffuse = texture2D(u_DiffuseMap, var_TexDiffuse);
	diffuse.rgb *= C;// * clamp(dot(N, L), 0.0, 1.0);
	
	// compute final color
	gl_FragColor = diffuse;
}
