/*
===========================================================================
Copyright (C) 2008-2009 Robert Beckebans <trebor_7@users.sourceforge.net>

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

uniform sampler2D	u_CurrentMap;
uniform float		u_HDRExposure;
uniform float		u_HDRMaxBrightness;

void	main()
{
	// calculate the screen texcoord in the 0.0 to 1.0 range
	vec2 st = gl_FragCoord.st * r_FBufScale;
	
#if defined(ATI_flippedImageFix)
	// BUGFIX: the ATI driver flips the image
	st.t = 1.0 - st.t;
#endif
	
	// scale by the screen non-power-of-two-adjust
	st *= r_NPOTScale;
	
	// autofocus
	//float vignette = texture2D(u_CurrentMap, vec2(0.5, 0.5) * r_NPOTScale).r;
	
	vec4 color = texture2D(u_CurrentMap, st);
	
	// perform tone-mapping
	float Y = dot(vec4(0.30, 0.59, 0.11, 0.0), color);
	float YD = u_HDRExposure * (u_HDRExposure / u_HDRMaxBrightness + 1.0) / (u_HDRExposure + 1.0);
	color *= YD;
	
	gl_FragColor = color;
}
