/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// tr_shade.c

#include "tr_local.h"

/*
  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

static char    *GLSL_PrintInfoLog(GLhandleARB object)
{
	static char     msg[4096];
	int             maxLength = 0;

	qglGetObjectParameterivARB(object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength);

	if(maxLength >= (int)sizeof(msg))
	{
		ri.Error(ERR_DROP, "RB_PrintInfoLog: max length >= sizeof(msg)");
		return NULL;
	}

	qglGetInfoLogARB(object, maxLength, &maxLength, msg);

	return msg;
}

char           *GLSL_PrintShaderSource(GLhandleARB object)
{
	static char     msg[4096];
	int             maxLength = 0;

	qglGetObjectParameterivARB(object, GL_OBJECT_SHADER_SOURCE_LENGTH_ARB, &maxLength);

	if(maxLength >= (int)sizeof(msg))
	{
		ri.Error(ERR_DROP, "RB_PrintShaderSource: max length >= sizeof(msg)");
		return NULL;
	}

	qglGetShaderSourceARB(object, maxLength, &maxLength, msg);

	return msg;
}

static void GLSL_LoadGPUShader(GLhandleARB program, const char *name, GLenum shaderType)
{

	char            filename[MAX_QPATH];
	GLcharARB      *buffer = NULL;
	int             size;
	GLint           compiled;
	GLhandleARB     shader;

	if(shaderType == GL_VERTEX_SHADER_ARB)
	{
		Com_sprintf(filename, sizeof(filename), "glsl/%s_vp.glsl", name);
	}
	else
	{
		Com_sprintf(filename, sizeof(filename), "glsl/%s_fp.glsl", name);
	}

	ri.Printf(PRINT_ALL, "...loading '%s'\n", filename);
	size = ri.FS_ReadFile(filename, (void **)&buffer);
	if(!buffer)
	{
		ri.Error(ERR_DROP, "Couldn't load %s", filename);
	}

	shader = qglCreateShaderObjectARB(shaderType);

	{
		static char     bufferExtra[32000];
		static char     line[MAX_STRING_CHARS];
		int             sizeExtra;

		char           *bufferFinal = NULL;
		int             sizeFinal;

		Com_Memset(bufferExtra, 0, sizeof(bufferExtra));

		// HACK: add some macros to avoid extra uniforms
		Q_strcat(bufferExtra, sizeof(bufferExtra),
				 va("#ifndef r_SpecularScale\n#define r_SpecularScale %f\n#endif\n", r_specularScale->value));
		Q_strcat(bufferExtra, sizeof(bufferExtra),
				 va("#ifndef r_NormalScale\n#define r_NormalScale %f\n#endif\n", r_normalScale->value));

		// HACK: add ATI's GLSL quirks      
		if(glConfig.hardwareType == GLHW_ATI)
		{
			Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef ATI\n#define ATI\n#endif\n");
		}

		if(r_shadows->integer >= 4 && glConfig.textureFloatAvailable && glConfig.framebufferObjectAvailable)
		{
			Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef VSM\n#define VSM 1\n#endif\n");

			if(glConfig.hardwareType == GLHW_ATI)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef VSM_CLAMP\n#define VSM_CLAMP 1\n#endif\n");
			}

			if(glConfig.hardwareType == GLHW_G80 && r_shadows->integer == 5)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef VSM_EPSILON\n#define VSM_EPSILON 0.000001\n#endif\n");
			}
			else
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef VSM_EPSILON\n#define VSM_EPSILON 0.0001\n#endif\n");
			}

			if(r_debugShadowMaps->integer)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra),
						 va("#ifndef DEBUG_VSM\n#define DEBUG_VSM %i\n#endif\n", r_debugShadowMaps->integer));
			}

			if(r_softShadows->integer == 1)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCF_2X2\n#define PCF_2X2 1\n#endif\n");
			}
			else if(r_softShadows->integer == 2)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCF_3X3\n#define PCF_3X3 1\n#endif\n");
			}
			else if(r_softShadows->integer == 3)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCF_4X4\n#define PCF_4X4 1\n#endif\n");
			}
			else if(r_softShadows->integer == 4)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCF_5X5\n#define PCF_5X5 1\n#endif\n");
			}
			else if(r_softShadows->integer == 5)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCF_6X6\n#define PCF_6X6 1\n#endif\n");
			}
			else if(r_softShadows->integer == 6)
			{
				Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PCSS\n#define PCSS 1\n#endif\n");
			}
		}

		if(glConfig.drawBuffersAvailable && glConfig.maxDrawBuffers >= 4 && glConfig.hardwareType != GLHW_ATI)
		{
			//Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef GL_ARB_draw_buffers\n#define GL_ARB_draw_buffers 1\n#endif\n");
			Q_strcat(bufferExtra, sizeof(bufferExtra), "#extension GL_ARB_draw_buffers : enable\n");
		}

		if( /* TODO: check for shader model 3 hardware  && */ r_parallaxMapping->integer)
		{
			Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef PARALLAX\n#define PARALLAX 1\n#endif\n");
		}

		/*
		   if(glConfig.textureFloatAvailable)
		   {
		   Q_strcat(bufferExtra, sizeof(bufferExtra), "#ifndef GL_ARB_texture_float\n#define GL_ARB_texture_float 1\n#endif\n");
		   }
		 */

		sizeExtra = strlen(bufferExtra);
		sizeFinal = sizeExtra + size;

		//ri.Printf(PRINT_ALL, "GLSL extra: %s\n", bufferExtra);

		bufferFinal = ri.Hunk_AllocateTempMemory(size + sizeExtra);

		strcpy(bufferFinal, bufferExtra);
		Q_strcat(bufferFinal, sizeFinal, buffer);

		qglShaderSourceARB(shader, 1, (const GLcharARB **)&bufferFinal, &sizeFinal);

		ri.Hunk_FreeTempMemory(bufferFinal);
	}

	// compile shader
	qglCompileShaderARB(shader);

	// check if shader compiled
	qglGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &compiled);
	if(!compiled)
	{
		ri.Error(ERR_DROP, "Couldn't compile %s", GLSL_PrintInfoLog(shader));
		ri.FS_FreeFile(buffer);
		return;
	}
#ifdef _DEBUG
	ri.Printf(PRINT_ALL, "GLSL compile log:\n%s\n", GLSL_PrintInfoLog(shader));
#endif
//  ri.Printf(PRINT_ALL, "%s\n", GLSL_PrintShaderSource(shader));

	// attach shader to program
	qglAttachObjectARB(program, shader);

	// delete shader, no longer needed
	qglDeleteObjectARB(shader);

	ri.FS_FreeFile(buffer);
}

static void GLSL_LinkProgram(GLhandleARB program)
{
	GLint           linked;

	qglLinkProgramARB(program);

	qglGetObjectParameterivARB(program, GL_OBJECT_LINK_STATUS_ARB, &linked);
	if(!linked)
	{
		ri.Error(ERR_DROP, "%s\nshaders failed to link", GLSL_PrintInfoLog(program));
	}
}

static void GLSL_ValidateProgram(GLhandleARB program)
{
	GLint           validated;

	qglValidateProgramARB(program);

	qglGetObjectParameterivARB(program, GL_OBJECT_VALIDATE_STATUS_ARB, &validated);
	if(!validated)
	{
		ri.Error(ERR_DROP, "%s\nshaders failed to validate", GLSL_PrintInfoLog(program));
	}
}

static void GLSL_ShowProgramUniforms(GLhandleARB program)
{
	int             i, count, size, type;
	char            uniformName[1000];

	// install the executables in the program object as part of current state.
	qglUseProgramObjectARB(program);

	// check for GL Errors

	// query the number of active uniforms
	qglGetObjectParameterivARB(program, GL_OBJECT_ACTIVE_UNIFORMS_ARB, &count);

	// Loop over each of the active uniforms, and set their value
	for(i = 0; i < count; i++)
	{
		qglGetActiveUniformARB(program, i, sizeof(uniformName), NULL, &size, &type, uniformName);

		ri.Printf(PRINT_DEVELOPER, "active uniform: '%s'\n", uniformName);
	}

	qglUseProgramObjectARB(0);
}

static void GLSL_InitGPUShader(shaderProgram_t * program, const char *name, int attribs, qboolean fragmentShader)
{

	ri.Printf(PRINT_DEVELOPER, "------- GPU shader -------\n");

	program->program = qglCreateProgramObjectARB();
	program->attribs = attribs;

	GLSL_LoadGPUShader(program->program, name, GL_VERTEX_SHADER_ARB);

	if(fragmentShader)
		GLSL_LoadGPUShader(program->program, name, GL_FRAGMENT_SHADER_ARB);

//  if( attribs & ATTRVERTEX )
//      qglBindAttribLocationARB( program->program, ATTR_INDEX_VERTEX, "attr_Vertex");

	if(attribs & GLCS_TEXCOORD0)
		qglBindAttribLocationARB(program->program, ATTR_INDEX_TEXCOORD0, "attr_TexCoord0");

	if(attribs & GLCS_TEXCOORD1)
		qglBindAttribLocationARB(program->program, ATTR_INDEX_TEXCOORD1, "attr_TexCoord1");

	if(attribs & GLCS_TEXCOORD2)
		qglBindAttribLocationARB(program->program, ATTR_INDEX_TEXCOORD2, "attr_TexCoord2");

//  if(attribs & GLCS_TEXCOORD3)
//      qglBindAttribLocationARB(program->program, ATTR_INDEX_TEXCOORD3, "attr_TexCoord3");

	if(attribs & GLCS_TANGENT)
		qglBindAttribLocationARB(program->program, ATTR_INDEX_TANGENT, "attr_Tangent");

	if(attribs & GLCS_BINORMAL)
		qglBindAttribLocationARB(program->program, ATTR_INDEX_BINORMAL, "attr_Binormal");

//  if( attribs & GLCS_NORMAL )
//      qglBindAttribLocationARB( program->program, ATTR_INDEX_NORMAL, "attr_Normal");

//  if( attribs & GLCS_COLOR )
//      qglBindAttribLocationARB( program->program, ATTR_INDEX_COLOR, "attr_Color");

	GLSL_LinkProgram(program->program);
}

void GLSL_InitGPUShaders(void)
{
	int             startTime, endTime;

	ri.Printf(PRINT_ALL, "------- GLSL_InitGPUShaders -------\n");

	startTime = ri.Milliseconds();

	// single texture rendering
	GLSL_InitGPUShader(&tr.genericSingleShader, "genericSingle", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	tr.genericSingleShader.u_ColorMap = qglGetUniformLocationARB(tr.genericSingleShader.program, "u_ColorMap");
	tr.genericSingleShader.u_InverseVertexColor =
		qglGetUniformLocationARB(tr.genericSingleShader.program, "u_InverseVertexColor");

	qglUseProgramObjectARB(tr.genericSingleShader.program);
	qglUniform1iARB(tr.genericSingleShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.genericSingleShader.program);
	GLSL_ShowProgramUniforms(tr.genericSingleShader.program);
	GL_CheckErrors();

	// geometric-buffer fill rendering with diffuse + bump + specular
	if(r_deferredShading->integer && glConfig.maxColorAttachments >= 4 && glConfig.textureFloatAvailable &&
	   glConfig.drawBuffersAvailable && glConfig.maxDrawBuffers >= 4 && glConfig.hardwareType != GLHW_ATI)
	{
		GLSL_InitGPUShader(&tr.geometricFillShader_DBS, "geometricFill_DBS",
						   GLCS_VERTEX | GLCS_TEXCOORD0 | GLCS_TEXCOORD1 | GLCS_TEXCOORD2 | GLCS_TANGENT | GLCS_BINORMAL |
						   GLCS_NORMAL, qtrue);

		tr.geometricFillShader_DBS.u_DiffuseMap = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_DiffuseMap");
		tr.geometricFillShader_DBS.u_NormalMap = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_NormalMap");
		tr.geometricFillShader_DBS.u_SpecularMap = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_SpecularMap");
		tr.geometricFillShader_DBS.u_ViewOrigin = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_ViewOrigin");
		tr.geometricFillShader_DBS.u_SpecularExponent =
			qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_SpecularExponent");
		tr.geometricFillShader_DBS.u_DepthScale = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_DepthScale");
		tr.geometricFillShader_DBS.u_ModelMatrix = qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_ModelMatrix");
		tr.geometricFillShader_DBS.u_ModelViewMatrix =
			qglGetUniformLocationARB(tr.geometricFillShader_DBS.program, "u_ModelViewMatrix");

		qglUseProgramObjectARB(tr.geometricFillShader_DBS.program);
		qglUniform1iARB(tr.geometricFillShader_DBS.u_DiffuseMap, 0);
		qglUniform1iARB(tr.geometricFillShader_DBS.u_NormalMap, 1);
		qglUniform1iARB(tr.geometricFillShader_DBS.u_SpecularMap, 2);
		qglUseProgramObjectARB(0);

		GLSL_ValidateProgram(tr.geometricFillShader_DBS.program);
		GLSL_ShowProgramUniforms(tr.geometricFillShader_DBS.program);
		GL_CheckErrors();

		// deferred omni-directional lighting post process effect
		GLSL_InitGPUShader(&tr.deferredLightingShader_DBS_omni, "deferredLighting_DBS_omni", GLCS_VERTEX, qtrue);

		tr.deferredLightingShader_DBS_omni.u_CurrentMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_CurrentMap");
		tr.deferredLightingShader_DBS_omni.u_NormalMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_NormalMap");
		tr.deferredLightingShader_DBS_omni.u_SpecularMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_SpecularMap");
		tr.deferredLightingShader_DBS_omni.u_PositionMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_PositionMap");
		tr.deferredLightingShader_DBS_omni.u_AttenuationMapXY =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_AttenuationMapXY");
		tr.deferredLightingShader_DBS_omni.u_AttenuationMapZ =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_AttenuationMapZ");
		tr.deferredLightingShader_DBS_omni.u_ShadowMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_ShadowMap");
		tr.deferredLightingShader_DBS_omni.u_ViewOrigin =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_ViewOrigin");
		tr.deferredLightingShader_DBS_omni.u_LightOrigin =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_LightOrigin");
		tr.deferredLightingShader_DBS_omni.u_LightColor =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_LightColor");
		tr.deferredLightingShader_DBS_omni.u_LightRadius =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_LightRadius");
		tr.deferredLightingShader_DBS_omni.u_LightScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_LightScale");
		tr.deferredLightingShader_DBS_omni.u_LightAttenuationMatrix =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_LightAttenuationMatrix");
		tr.deferredLightingShader_DBS_omni.u_ShadowCompare =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_ShadowCompare");
		tr.deferredLightingShader_DBS_omni.u_FBufScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_FBufScale");
		tr.deferredLightingShader_DBS_omni.u_NPOTScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_omni.program, "u_NPOTScale");

		qglUseProgramObjectARB(tr.deferredLightingShader_DBS_omni.program);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_DiffuseMap, 0);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_NormalMap, 1);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_SpecularMap, 2);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_PositionMap, 3);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_AttenuationMapXY, 4);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_AttenuationMapZ, 5);
		qglUniform1iARB(tr.deferredLightingShader_DBS_omni.u_ShadowMap, 6);
		qglUseProgramObjectARB(0);

		GLSL_ValidateProgram(tr.deferredLightingShader_DBS_omni.program);
		GLSL_ShowProgramUniforms(tr.deferredLightingShader_DBS_omni.program);
		GL_CheckErrors();

		// deferred projective lighting post process effect
		GLSL_InitGPUShader(&tr.deferredLightingShader_DBS_proj, "deferredLighting_DBS_proj", GLCS_VERTEX, qtrue);

		tr.deferredLightingShader_DBS_proj.u_CurrentMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_CurrentMap");
		tr.deferredLightingShader_DBS_proj.u_NormalMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_NormalMap");
		tr.deferredLightingShader_DBS_proj.u_SpecularMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_SpecularMap");
		tr.deferredLightingShader_DBS_proj.u_PositionMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_PositionMap");
		tr.deferredLightingShader_DBS_proj.u_AttenuationMapXY =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_AttenuationMapXY");
		tr.deferredLightingShader_DBS_proj.u_AttenuationMapZ =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_AttenuationMapZ");
		tr.deferredLightingShader_DBS_proj.u_ShadowMap =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_ShadowMap");
		tr.deferredLightingShader_DBS_proj.u_ViewOrigin =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_ViewOrigin");
		tr.deferredLightingShader_DBS_proj.u_LightOrigin =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_LightOrigin");
		tr.deferredLightingShader_DBS_proj.u_LightColor =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_LightColor");
		tr.deferredLightingShader_DBS_proj.u_LightRadius =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_LightRadius");
		tr.deferredLightingShader_DBS_proj.u_LightScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_LightScale");
		tr.deferredLightingShader_DBS_proj.u_LightAttenuationMatrix =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_LightAttenuationMatrix");
		tr.deferredLightingShader_DBS_proj.u_ShadowMatrix =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_ShadowMatrix");
		tr.deferredLightingShader_DBS_proj.u_ShadowCompare =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_ShadowCompare");
		tr.deferredLightingShader_DBS_proj.u_FBufScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_FBufScale");
		tr.deferredLightingShader_DBS_proj.u_NPOTScale =
			qglGetUniformLocationARB(tr.deferredLightingShader_DBS_proj.program, "u_NPOTScale");

		qglUseProgramObjectARB(tr.deferredLightingShader_DBS_proj.program);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_DiffuseMap, 0);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_NormalMap, 1);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_SpecularMap, 2);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_PositionMap, 3);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_AttenuationMapXY, 4);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_AttenuationMapZ, 5);
		qglUniform1iARB(tr.deferredLightingShader_DBS_proj.u_ShadowMap, 6);
		qglUseProgramObjectARB(0);

		GLSL_ValidateProgram(tr.deferredLightingShader_DBS_proj.program);
		GLSL_ShowProgramUniforms(tr.deferredLightingShader_DBS_proj.program);
		GL_CheckErrors();
	}

	// black depth fill rendering with textures
	GLSL_InitGPUShader(&tr.depthFillShader, "depthFill", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	tr.depthFillShader.u_ColorMap = qglGetUniformLocationARB(tr.depthFillShader.program, "u_ColorMap");
	tr.depthFillShader.u_AlphaTest = qglGetUniformLocationARB(tr.depthFillShader.program, "u_AlphaTest");
	tr.depthFillShader.u_AmbientColor = qglGetUniformLocationARB(tr.depthFillShader.program, "u_AmbientColor");

	qglUseProgramObjectARB(tr.depthFillShader.program);
	qglUniform1iARB(tr.depthFillShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.depthFillShader.program);
	GLSL_ShowProgramUniforms(tr.depthFillShader.program);
	GL_CheckErrors();

	// colored depth test rendering with textures into gl_FragData[1]
	GLSL_InitGPUShader(&tr.depthTestShader, "depthTest", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	tr.depthTestShader.u_ColorMap = qglGetUniformLocationARB(tr.depthTestShader.program, "u_ColorMap");
	tr.depthTestShader.u_CurrentMap = qglGetUniformLocationARB(tr.depthTestShader.program, "u_CurrentMap");
	tr.depthTestShader.u_FBufScale = qglGetUniformLocationARB(tr.depthTestShader.program, "u_FBufScale");
	tr.depthTestShader.u_NPOTScale = qglGetUniformLocationARB(tr.depthTestShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.depthTestShader.program);
	qglUniform1iARB(tr.depthTestShader.u_ColorMap, 0);
	qglUniform1iARB(tr.depthTestShader.u_CurrentMap, 1);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.depthTestShader.program);
	GLSL_ShowProgramUniforms(tr.depthTestShader.program);
	GL_CheckErrors();

	// shadow volume extrusion
	GLSL_InitGPUShader(&tr.shadowExtrudeShader, "shadowExtrude", GLCS_VERTEX, qtrue);

	tr.shadowExtrudeShader.u_LightOrigin = qglGetUniformLocationARB(tr.shadowExtrudeShader.program, "u_LightOrigin");

	GLSL_ValidateProgram(tr.shadowExtrudeShader.program);
	GLSL_ShowProgramUniforms(tr.shadowExtrudeShader.program);
	GL_CheckErrors();

	// shadowmap distance compression
	GLSL_InitGPUShader(&tr.shadowFillShader, "shadowFill", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	tr.shadowFillShader.u_ColorMap = qglGetUniformLocationARB(tr.shadowFillShader.program, "u_ColorMap");
	tr.shadowFillShader.u_AlphaTest = qglGetUniformLocationARB(tr.shadowFillShader.program, "u_AlphaTest");
	tr.shadowFillShader.u_LightOrigin = qglGetUniformLocationARB(tr.shadowFillShader.program, "u_LightOrigin");
	tr.shadowFillShader.u_LightRadius = qglGetUniformLocationARB(tr.shadowFillShader.program, "u_LightRadius");
	tr.shadowFillShader.u_ModelMatrix = qglGetUniformLocationARB(tr.shadowFillShader.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.shadowFillShader.program);
	qglUniform1iARB(tr.shadowFillShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.shadowFillShader.program);
	GLSL_ShowProgramUniforms(tr.shadowFillShader.program);
	GL_CheckErrors();

	// omni-directional specular bump mapping ( Doom3 style )
	GLSL_InitGPUShader(&tr.forwardLightingShader_DBS_omni,
					   "forwardLighting_DBS_omni",
					   GLCS_VERTEX | GLCS_TEXCOORD0 | GLCS_TEXCOORD1 | GLCS_TEXCOORD2 | GLCS_TANGENT | GLCS_BINORMAL |
					   GLCS_NORMAL, qtrue);

	tr.forwardLightingShader_DBS_omni.u_DiffuseMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_DiffuseMap");
	tr.forwardLightingShader_DBS_omni.u_NormalMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_NormalMap");
	tr.forwardLightingShader_DBS_omni.u_SpecularMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_SpecularMap");
	tr.forwardLightingShader_DBS_omni.u_AttenuationMapXY =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_AttenuationMapXY");
	tr.forwardLightingShader_DBS_omni.u_AttenuationMapZ =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_AttenuationMapZ");
	if(r_shadows->integer >= 4)
	{
		tr.forwardLightingShader_DBS_omni.u_ShadowMap =
			qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ShadowMap");
	}
	tr.forwardLightingShader_DBS_omni.u_ViewOrigin =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ViewOrigin");
	tr.forwardLightingShader_DBS_omni.u_InverseVertexColor =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_InverseVertexColor");
	tr.forwardLightingShader_DBS_omni.u_LightOrigin =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_LightOrigin");
	tr.forwardLightingShader_DBS_omni.u_LightColor =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_LightColor");
	tr.forwardLightingShader_DBS_omni.u_LightRadius =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_LightRadius");
	tr.forwardLightingShader_DBS_omni.u_LightScale =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_LightScale");
	tr.forwardLightingShader_DBS_omni.u_ShadowCompare =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ShadowCompare");
	tr.forwardLightingShader_DBS_omni.u_ShadowTexelSize =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ShadowTexelSize");
	tr.forwardLightingShader_DBS_omni.u_ShadowBlur =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ShadowBlur");
	tr.forwardLightingShader_DBS_omni.u_SpecularExponent =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_SpecularExponent");
	tr.forwardLightingShader_DBS_omni.u_ModelMatrix =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_omni.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.forwardLightingShader_DBS_omni.program);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_DiffuseMap, 0);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_NormalMap, 1);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_SpecularMap, 2);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_AttenuationMapXY, 3);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_AttenuationMapZ, 4);
	if(r_shadows->integer >= 4)
	{
		qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_ShadowMap, 5);
	}
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.forwardLightingShader_DBS_omni.program);
	GLSL_ShowProgramUniforms(tr.forwardLightingShader_DBS_omni.program);
	GL_CheckErrors();

	// projective lighting ( Doom3 style )
	GLSL_InitGPUShader(&tr.forwardLightingShader_DBS_proj, "forwardLighting_DBS_proj",
					   GLCS_VERTEX | GLCS_TEXCOORD0 | GLCS_TANGENT | GLCS_BINORMAL | GLCS_NORMAL, qtrue);

	tr.forwardLightingShader_DBS_proj.u_DiffuseMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_DiffuseMap");
	tr.forwardLightingShader_DBS_proj.u_NormalMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_NormalMap");
	tr.forwardLightingShader_DBS_proj.u_SpecularMap =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_SpecularMap");
	tr.forwardLightingShader_DBS_proj.u_AttenuationMapXY =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_AttenuationMapXY");
	tr.forwardLightingShader_DBS_proj.u_AttenuationMapZ =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_AttenuationMapZ");
	if(r_shadows->integer >= 4)
	{
		tr.forwardLightingShader_DBS_proj.u_ShadowMap =
			qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ShadowMap");
	}
	tr.forwardLightingShader_DBS_proj.u_ViewOrigin =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ViewOrigin");
	tr.forwardLightingShader_DBS_proj.u_InverseVertexColor =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_InverseVertexColor");
	tr.forwardLightingShader_DBS_proj.u_LightOrigin =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_LightOrigin");
	tr.forwardLightingShader_DBS_proj.u_LightColor =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_LightColor");
	tr.forwardLightingShader_DBS_proj.u_LightRadius =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_LightRadius");
	tr.forwardLightingShader_DBS_proj.u_LightScale =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_LightScale");
	tr.forwardLightingShader_DBS_proj.u_ShadowCompare =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ShadowCompare");
	tr.forwardLightingShader_DBS_proj.u_ShadowTexelSize =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ShadowTexelSize");
	tr.forwardLightingShader_DBS_proj.u_ShadowBlur =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ShadowBlur");
	tr.forwardLightingShader_DBS_proj.u_SpecularExponent =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_SpecularExponent");
	tr.forwardLightingShader_DBS_proj.u_ModelMatrix =
		qglGetUniformLocationARB(tr.forwardLightingShader_DBS_proj.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.forwardLightingShader_DBS_proj.program);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_DiffuseMap, 0);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_NormalMap, 1);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_SpecularMap, 2);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_AttenuationMapXY, 3);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_AttenuationMapZ, 4);
	if(r_shadows->integer >= 4)
	{
		qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_ShadowMap, 5);
	}
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.forwardLightingShader_DBS_proj.program);
	GLSL_ShowProgramUniforms(tr.forwardLightingShader_DBS_proj.program);
	GL_CheckErrors();

#ifdef VOLUMETRIC_LIGHTING
	// volumetric lighting
	GLSL_InitGPUShader(&tr.lightVolumeShader_omni, "lightVolume_omni", GLCS_VERTEX, qtrue);

	tr.lightVolumeShader_omni.u_AttenuationMapXY =
		qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_AttenuationMapXY");
	tr.lightVolumeShader_omni.u_AttenuationMapZ =
		qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_AttenuationMapZ");
	tr.lightVolumeShader_omni.u_ShadowMap = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_ShadowMap");
	tr.lightVolumeShader_omni.u_ViewOrigin = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_ViewOrigin");
	tr.lightVolumeShader_omni.u_LightOrigin = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_LightOrigin");
	tr.lightVolumeShader_omni.u_LightColor = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_LightColor");
	tr.lightVolumeShader_omni.u_LightRadius = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_LightRadius");
	tr.lightVolumeShader_omni.u_LightScale = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_LightScale");
	tr.lightVolumeShader_omni.u_LightAttenuationMatrix =
		qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_LightAttenuationMatrix");
	tr.lightVolumeShader_omni.u_ShadowCompare = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_ShadowCompare");
	tr.lightVolumeShader_omni.u_ModelMatrix = qglGetUniformLocationARB(tr.lightVolumeShader_omni.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.lightVolumeShader_omni.program);
	qglUniform1iARB(tr.lightVolumeShader_omni.u_AttenuationMapXY, 0);
	qglUniform1iARB(tr.lightVolumeShader_omni.u_AttenuationMapZ, 1);
	qglUniform1iARB(tr.lightVolumeShader_omni.u_ShadowMap, 2);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.lightVolumeShader_omni.program);
	GLSL_ShowProgramUniforms(tr.lightVolumeShader_omni.program);
	GL_CheckErrors();
#endif

	// cubemap reflection for abitrary polygons
	GLSL_InitGPUShader(&tr.reflectionShader_C, "reflection_C", GLCS_VERTEX | GLCS_NORMAL, qtrue);

	tr.reflectionShader_C.u_ColorMap = qglGetUniformLocationARB(tr.reflectionShader_C.program, "u_ColorMap");
	tr.reflectionShader_C.u_ViewOrigin = qglGetUniformLocationARB(tr.reflectionShader_C.program, "u_ViewOrigin");
	tr.reflectionShader_C.u_ModelMatrix = qglGetUniformLocationARB(tr.reflectionShader_C.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.reflectionShader_C.program);
	qglUniform1iARB(tr.reflectionShader_C.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.reflectionShader_C.program);
	GLSL_ShowProgramUniforms(tr.reflectionShader_C.program);
	GL_CheckErrors();

	// bumped cubemap reflection for abitrary polygons ( EMBM )
	GLSL_InitGPUShader(&tr.reflectionShader_CB,
					   "reflection_CB", GLCS_VERTEX | GLCS_TEXCOORD1 | GLCS_TANGENT | GLCS_BINORMAL | GLCS_NORMAL, qtrue);

	tr.reflectionShader_CB.u_ColorMap = qglGetUniformLocationARB(tr.reflectionShader_CB.program, "u_ColorMap");
	tr.reflectionShader_CB.u_NormalMap = qglGetUniformLocationARB(tr.reflectionShader_CB.program, "u_NormalMap");
	tr.reflectionShader_CB.u_ViewOrigin = qglGetUniformLocationARB(tr.reflectionShader_CB.program, "u_ViewOrigin");
	tr.reflectionShader_CB.u_ModelMatrix = qglGetUniformLocationARB(tr.reflectionShader_CB.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.reflectionShader_CB.program);
	qglUniform1iARB(tr.reflectionShader_CB.u_ColorMap, 0);
	qglUniform1iARB(tr.reflectionShader_CB.u_NormalMap, 1);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.reflectionShader_CB.program);
	GLSL_ShowProgramUniforms(tr.reflectionShader_CB.program);
	GL_CheckErrors();

	// cubemap refraction for abitrary polygons
	GLSL_InitGPUShader(&tr.refractionShader_C, "refraction_C", GLCS_VERTEX | GLCS_NORMAL, qtrue);

	tr.refractionShader_C.u_ColorMap = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_ColorMap");
	tr.refractionShader_C.u_ViewOrigin = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_ViewOrigin");
	tr.refractionShader_C.u_RefractionIndex = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_RefractionIndex");
	tr.refractionShader_C.u_FresnelPower = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_FresnelPower");
	tr.refractionShader_C.u_FresnelScale = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_FresnelScale");
	tr.refractionShader_C.u_FresnelBias = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_FresnelBias");
	tr.refractionShader_C.u_ModelMatrix = qglGetUniformLocationARB(tr.refractionShader_C.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.refractionShader_C.program);
	qglUniform1iARB(tr.refractionShader_C.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.refractionShader_C.program);
	GLSL_ShowProgramUniforms(tr.refractionShader_C.program);
	GL_CheckErrors();

	// cubemap dispersion for abitrary polygons
	GLSL_InitGPUShader(&tr.dispersionShader_C, "dispersion_C", GLCS_VERTEX | GLCS_NORMAL, qtrue);

	tr.dispersionShader_C.u_ColorMap = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_ColorMap");
	tr.dispersionShader_C.u_ViewOrigin = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_ViewOrigin");
	tr.dispersionShader_C.u_EtaRatio = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_EtaRatio");
	tr.dispersionShader_C.u_FresnelPower = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_FresnelPower");
	tr.dispersionShader_C.u_FresnelScale = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_FresnelScale");
	tr.dispersionShader_C.u_FresnelBias = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_FresnelBias");
	tr.dispersionShader_C.u_ModelMatrix = qglGetUniformLocationARB(tr.dispersionShader_C.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.dispersionShader_C.program);
	qglUniform1iARB(tr.dispersionShader_C.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.dispersionShader_C.program);
	GLSL_ShowProgramUniforms(tr.dispersionShader_C.program);
	GL_CheckErrors();

	// skybox drawing for abitrary polygons
	GLSL_InitGPUShader(&tr.skyBoxShader, "skybox", GLCS_VERTEX | GLCS_NORMAL, qtrue);

	tr.skyBoxShader.u_ColorMap = qglGetUniformLocationARB(tr.skyBoxShader.program, "u_ColorMap");
	tr.skyBoxShader.u_ViewOrigin = qglGetUniformLocationARB(tr.skyBoxShader.program, "u_ViewOrigin");
	tr.skyBoxShader.u_ModelMatrix = qglGetUniformLocationARB(tr.skyBoxShader.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.skyBoxShader.program);
	qglUniform1iARB(tr.skyBoxShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.skyBoxShader.program);
	GLSL_ShowProgramUniforms(tr.skyBoxShader.program);
	GL_CheckErrors();

	// heatHaze post process effect
	GLSL_InitGPUShader(&tr.heatHazeShader, "heatHaze", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	if(glConfig.hardwareType == GLHW_ATI)
	{
		tr.heatHazeShader.u_ProjectionMatrixTranspose =
			qglGetUniformLocationARB(tr.heatHazeShader.program, "u_ProjectionMatrixTranspose");
	}
	tr.heatHazeShader.u_DeformMagnitude = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_DeformMagnitude");
	tr.heatHazeShader.u_NormalMap = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_NormalMap");
	tr.heatHazeShader.u_CurrentMap = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_CurrentMap");
	tr.heatHazeShader.u_ContrastMap = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_ContrastMap");
	tr.heatHazeShader.u_AlphaTest = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_AlphaTest");
	tr.heatHazeShader.u_FBufScale = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_FBufScale");
	tr.heatHazeShader.u_NPOTScale = qglGetUniformLocationARB(tr.heatHazeShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.heatHazeShader.program);
	qglUniform1iARB(tr.heatHazeShader.u_NormalMap, 0);
	qglUniform1iARB(tr.heatHazeShader.u_CurrentMap, 1);
	qglUniform1iARB(tr.heatHazeShader.u_ContrastMap, 2);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.heatHazeShader.program);
	GLSL_ShowProgramUniforms(tr.heatHazeShader.program);
	GL_CheckErrors();

	// bloom post process effect
	GLSL_InitGPUShader(&tr.bloomShader, "bloom", GLCS_VERTEX, qtrue);

	tr.bloomShader.u_ColorMap = qglGetUniformLocationARB(tr.bloomShader.program, "u_ColorMap");
	tr.bloomShader.u_ContrastMap = qglGetUniformLocationARB(tr.bloomShader.program, "u_ContrastMap");
	tr.bloomShader.u_FBufScale = qglGetUniformLocationARB(tr.bloomShader.program, "u_FBufScale");
	tr.bloomShader.u_NPOTScale = qglGetUniformLocationARB(tr.bloomShader.program, "u_NPOTScale");
	tr.bloomShader.u_BlurMagnitude = qglGetUniformLocationARB(tr.bloomShader.program, "u_BlurMagnitude");

	qglUseProgramObjectARB(tr.bloomShader.program);
	qglUniform1iARB(tr.bloomShader.u_ColorMap, 0);
	qglUniform1iARB(tr.bloomShader.u_ContrastMap, 1);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.bloomShader.program);
	GLSL_ShowProgramUniforms(tr.bloomShader.program);
	GL_CheckErrors();

	// contrast post process effect
	GLSL_InitGPUShader(&tr.contrastShader, "contrast", GLCS_VERTEX, qtrue);

	tr.contrastShader.u_ColorMap = qglGetUniformLocationARB(tr.contrastShader.program, "u_ColorMap");
	tr.contrastShader.u_FBufScale = qglGetUniformLocationARB(tr.contrastShader.program, "u_FBufScale");
	tr.contrastShader.u_NPOTScale = qglGetUniformLocationARB(tr.contrastShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.contrastShader.program);
	qglUniform1iARB(tr.contrastShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.contrastShader.program);
	GLSL_ShowProgramUniforms(tr.contrastShader.program);
	GL_CheckErrors();

	// blurX post process effect
	GLSL_InitGPUShader(&tr.blurXShader, "blurX", GLCS_VERTEX, qtrue);

	tr.blurXShader.u_ColorMap = qglGetUniformLocationARB(tr.blurXShader.program, "u_ColorMap");
	tr.blurXShader.u_FBufScale = qglGetUniformLocationARB(tr.blurXShader.program, "u_FBufScale");
	tr.blurXShader.u_NPOTScale = qglGetUniformLocationARB(tr.blurXShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.blurXShader.program);
	qglUniform1iARB(tr.blurXShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.blurXShader.program);
	GLSL_ShowProgramUniforms(tr.blurXShader.program);
	GL_CheckErrors();

	// blurY post process effect
	GLSL_InitGPUShader(&tr.blurYShader, "blurY", GLCS_VERTEX, qtrue);

	tr.blurYShader.u_ColorMap = qglGetUniformLocationARB(tr.blurYShader.program, "u_ColorMap");
	tr.blurYShader.u_FBufScale = qglGetUniformLocationARB(tr.blurYShader.program, "u_FBufScale");
	tr.blurYShader.u_NPOTScale = qglGetUniformLocationARB(tr.blurYShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.blurYShader.program);
	qglUniform1iARB(tr.blurYShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.blurYShader.program);
	GLSL_ShowProgramUniforms(tr.blurYShader.program);
	GL_CheckErrors();

	// rotoscope post process effect
	GLSL_InitGPUShader(&tr.rotoscopeShader, "rotoscope", GLCS_VERTEX | GLCS_TEXCOORD0, qtrue);

	tr.rotoscopeShader.u_ColorMap = qglGetUniformLocationARB(tr.rotoscopeShader.program, "u_ColorMap");
	tr.rotoscopeShader.u_FBufScale = qglGetUniformLocationARB(tr.rotoscopeShader.program, "u_FBufScale");
	tr.rotoscopeShader.u_NPOTScale = qglGetUniformLocationARB(tr.rotoscopeShader.program, "u_NPOTScale");
	tr.rotoscopeShader.u_BlurMagnitude = qglGetUniformLocationARB(tr.rotoscopeShader.program, "u_BlurMagnitude");

	qglUseProgramObjectARB(tr.rotoscopeShader.program);
	qglUniform1iARB(tr.rotoscopeShader.u_ColorMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.rotoscopeShader.program);
	GLSL_ShowProgramUniforms(tr.rotoscopeShader.program);
	GL_CheckErrors();

	// screen post process effect
	GLSL_InitGPUShader(&tr.screenShader, "screen", GLCS_VERTEX | GLCS_COLOR, qtrue);

	tr.screenShader.u_CurrentMap = qglGetUniformLocationARB(tr.screenShader.program, "u_CurrentMap");
	tr.screenShader.u_FBufScale = qglGetUniformLocationARB(tr.screenShader.program, "u_FBufScale");
	tr.screenShader.u_NPOTScale = qglGetUniformLocationARB(tr.screenShader.program, "u_NPOTScale");

	qglUseProgramObjectARB(tr.screenShader.program);
	qglUniform1iARB(tr.screenShader.u_CurrentMap, 0);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.screenShader.program);
	GLSL_ShowProgramUniforms(tr.screenShader.program);
	GL_CheckErrors();

	// liquid post process effect
	GLSL_InitGPUShader(&tr.liquidShader, "liquid",
					   GLCS_VERTEX | GLCS_TEXCOORD0 | GLCS_TANGENT | GLCS_BINORMAL | GLCS_NORMAL, qtrue);

	tr.liquidShader.u_CurrentMap = qglGetUniformLocationARB(tr.liquidShader.program, "u_CurrentMap");
	tr.liquidShader.u_PortalMap = qglGetUniformLocationARB(tr.liquidShader.program, "u_PortalMap");
	tr.liquidShader.u_ViewOrigin = qglGetUniformLocationARB(tr.liquidShader.program, "u_ViewOrigin");
	tr.liquidShader.u_RefractionIndex = qglGetUniformLocationARB(tr.liquidShader.program, "u_RefractionIndex");
	tr.liquidShader.u_FresnelPower = qglGetUniformLocationARB(tr.liquidShader.program, "u_FresnelPower");
	tr.liquidShader.u_FresnelScale = qglGetUniformLocationARB(tr.liquidShader.program, "u_FresnelScale");
	tr.liquidShader.u_FresnelBias = qglGetUniformLocationARB(tr.liquidShader.program, "u_FresnelBias");
	tr.liquidShader.u_FBufScale = qglGetUniformLocationARB(tr.liquidShader.program, "u_FBufScale");
	tr.liquidShader.u_NPOTScale = qglGetUniformLocationARB(tr.liquidShader.program, "u_NPOTScale");
	tr.liquidShader.u_ModelMatrix = qglGetUniformLocationARB(tr.liquidShader.program, "u_ModelMatrix");

	qglUseProgramObjectARB(tr.liquidShader.program);
	qglUniform1iARB(tr.liquidShader.u_CurrentMap, 0);
	qglUniform1iARB(tr.liquidShader.u_PortalMap, 1);
	qglUseProgramObjectARB(0);

	GLSL_ValidateProgram(tr.liquidShader.program);
	GLSL_ShowProgramUniforms(tr.liquidShader.program);
	GL_CheckErrors();

	endTime = ri.Milliseconds();

	ri.Printf(PRINT_ALL, "GLSL shaders load time = %5.2f seconds\n", (endTime - startTime) / 1000.0);
}

void GLSL_ShutdownGPUShaders(void)
{
	ri.Printf(PRINT_ALL, "------- GLSL_ShutdownGPUShaders -------\n");

	if(tr.genericSingleShader.program)
	{
		qglDeleteObjectARB(tr.genericSingleShader.program);
		tr.genericSingleShader.program = 0;
	}

	if(tr.geometricFillShader_DBS.program)
	{
		qglDeleteObjectARB(tr.geometricFillShader_DBS.program);
		tr.geometricFillShader_DBS.program = 0;
	}

	if(tr.deferredLightingShader_DBS_omni.program)
	{
		qglDeleteObjectARB(tr.deferredLightingShader_DBS_omni.program);
		tr.deferredLightingShader_DBS_omni.program = 0;
	}

	if(tr.deferredLightingShader_DBS_proj.program)
	{
		qglDeleteObjectARB(tr.deferredLightingShader_DBS_proj.program);
		tr.deferredLightingShader_DBS_proj.program = 0;
	}

	if(tr.depthFillShader.program)
	{
		qglDeleteObjectARB(tr.depthFillShader.program);
		tr.depthFillShader.program = 0;
	}

	if(tr.depthTestShader.program)
	{
		qglDeleteObjectARB(tr.depthTestShader.program);
		tr.depthTestShader.program = 0;
	}

	if(tr.shadowExtrudeShader.program)
	{
		qglDeleteObjectARB(tr.shadowExtrudeShader.program);
		tr.shadowExtrudeShader.program = 0;
	}

	if(tr.shadowFillShader.program)
	{
		qglDeleteObjectARB(tr.shadowFillShader.program);
		tr.shadowFillShader.program = 0;
	}

	if(tr.forwardLightingShader_DBS_omni.program)
	{
		qglDeleteObjectARB(tr.forwardLightingShader_DBS_omni.program);
		tr.forwardLightingShader_DBS_omni.program = 0;
	}

	if(tr.forwardLightingShader_DBS_proj.program)
	{
		qglDeleteObjectARB(tr.forwardLightingShader_DBS_proj.program);
		tr.forwardLightingShader_DBS_proj.program = 0;
	}

#ifdef VOLUMETRIC_LIGHTING
	if(tr.lightVolumeShader_omni.program)
	{
		qglDeleteObjectARB(tr.lightVolumeShader_omni.program);
		tr.lightVolumeShader_omni.program = 0;
	}
#endif

	if(tr.reflectionShader_C.program)
	{
		qglDeleteObjectARB(tr.reflectionShader_C.program);
		tr.reflectionShader_C.program = 0;
	}

	if(tr.reflectionShader_CB.program)
	{
		qglDeleteObjectARB(tr.reflectionShader_CB.program);
		tr.reflectionShader_CB.program = 0;
	}

	if(tr.refractionShader_C.program)
	{
		qglDeleteObjectARB(tr.refractionShader_C.program);
		tr.refractionShader_C.program = 0;
	}

	if(tr.dispersionShader_C.program)
	{
		qglDeleteObjectARB(tr.dispersionShader_C.program);
		tr.dispersionShader_C.program = 0;
	}

	if(tr.skyBoxShader.program)
	{
		qglDeleteObjectARB(tr.skyBoxShader.program);
		tr.skyBoxShader.program = 0;
	}

	if(tr.heatHazeShader.program)
	{
		qglDeleteObjectARB(tr.heatHazeShader.program);
		tr.heatHazeShader.program = 0;
	}

	if(tr.bloomShader.program)
	{
		qglDeleteObjectARB(tr.bloomShader.program);
		tr.bloomShader.program = 0;
	}

	if(tr.contrastShader.program)
	{
		qglDeleteObjectARB(tr.contrastShader.program);
		tr.contrastShader.program = 0;
	}

	if(tr.blurXShader.program)
	{
		qglDeleteObjectARB(tr.blurXShader.program);
		tr.blurXShader.program = 0;
	}

	if(tr.blurYShader.program)
	{
		qglDeleteObjectARB(tr.blurYShader.program);
		tr.blurYShader.program = 0;
	}

	if(tr.rotoscopeShader.program)
	{
		qglDeleteObjectARB(tr.rotoscopeShader.program);
		tr.rotoscopeShader.program = 0;
	}

	if(tr.screenShader.program)
	{
		qglDeleteObjectARB(tr.screenShader.program);
		tr.screenShader.program = 0;
	}

	if(tr.liquidShader.program)
	{
		qglDeleteObjectARB(tr.liquidShader.program);
		tr.liquidShader.program = 0;
	}

	glState.currentProgram = 0;
	qglUseProgramObjectARB(0);
}

/*
==================
DrawElements
==================
*/
static void DrawElements()
{
	// move tess data through the GPU, finally
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		//qglDrawRangeElementsEXT(GL_TRIANGLES, 0, tessmesh->vertexes.size(), mesh->indexes.size(), GL_UNSIGNED_INT, VBO_BUFFER_OFFSET(mesh->vbo_indexes_ofs));

		qglDrawElements(GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, BUFFER_OFFSET(glState.currentVBO->ofsIndexes));
		backEnd.pc.c_indexes += tess.numIndexes;
	}
	else
	{
		qglDrawElements(GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes);
		backEnd.pc.c_indexes += tess.numIndexes;
	}

	// update performance counters
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_vertexes += tess.numVertexes;

	if(glState.currentVBO)
	{
		backEnd.pc.c_vboVertexes += tess.numVertexes;
		backEnd.pc.c_vboIndexes += tess.numIndexes;
	}
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t tess;


/*
=================
BindAnimatedImage
=================
*/
void BindAnimatedImage(textureBundle_t * bundle)
{
	if(bundle->isVideoMap)
	{
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	GL_Bind(bundle->image[0]);
}



/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris()
{
	GLimp_LogComment("--- DrawTris ---\n");

	if(r_showBatches->integer || r_showLightBatches->integer)
	{
		qglColor4fv(g_color_table[backEnd.pc.c_batches % 8]);
	}
	else if(glState.currentVBO)
	{
		qglColor3f(0, 0, 1);
	}
	else
	{
		qglColor3f(1, 1, 1);
	}

	GL_Program(0);
	GL_SelectTexture(0);
	GL_Bind(tr.whiteImage);

	GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE);
	GL_ClientState(GLCS_VERTEX);
	qglDepthRange(0, 0);

	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	DrawElements();

	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}
	qglDepthRange(0, 1);
}


/*
================
DrawTangentSpaces

Draws vertex tangent spaces for debugging
================
*/
static void DrawTangentSpaces()
{
	int             i;
	vec3_t          temp;

	GLimp_LogComment("--- DrawTangentSpaces ---\n");

	GL_Program(0);
	GL_SelectTexture(0);
	GL_Bind(tr.whiteImage);
	qglDepthRange(0, 0);		// never occluded
	GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE);

	qglBegin(GL_LINES);
	for(i = 0; i < tess.numVertexes; i++)
	{
		qglColor3f(1, 0, 0);
		qglVertex3fv(tess.xyz[i]);
		VectorMA(tess.xyz[i], 2, tess.tangents[i], temp);
		qglVertex3fv(temp);

		qglColor3f(0, 1, 0);
		qglVertex3fv(tess.xyz[i]);
		VectorMA(tess.xyz[i], 2, tess.binormals[i], temp);
		qglVertex3fv(temp);

		qglColor3f(0, 0, 1);
		qglVertex3fv(tess.xyz[i]);
		VectorMA(tess.xyz[i], 2, tess.normals[i], temp);
		qglVertex3fv(temp);
	}
	qglEnd();

	qglDepthRange(0, 1);
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals()
{
	int             i;
	vec3_t          temp;

	GLimp_LogComment("--- DrawNormals ---\n");

	GL_Program(0);
	GL_SelectTexture(0);
	GL_Bind(tr.whiteImage);
	qglColor3f(1, 1, 1);
	qglDepthRange(0, 0);		// never occluded
	GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE);

	qglBegin(GL_LINES);
	for(i = 0; i < tess.numVertexes; i++)
	{
		qglVertex3fv(tess.xyz[i]);
		VectorMA(tess.xyz[i], 2, tess.normals[i], temp);
		qglVertex3fv(temp);
	}
	qglEnd();

	qglDepthRange(0, 1);
}

/*
==============
Tess_Begin

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a Tess_End due
to overflow.
==============
*/
// *INDENT-OFF*
void Tess_Begin(	 void (*stageIteratorFunc)(),
					 shader_t * surfaceShader, shader_t * lightShader,
					 qboolean skipTangentSpaces,
					 qboolean shadowVolume)
{
	shader_t       *state = (surfaceShader->remappedShader) ? surfaceShader->remappedShader : surfaceShader;
		
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.surfaceShader = state;
	
	tess.surfaceStages = state->stages;
	tess.numSurfaceStages = state->numStages;
	
	tess.lightShader = lightShader;
	
	tess.stageIteratorFunc = stageIteratorFunc;
	tess.stageIteratorFunc2 = NULL;
	if(!tess.stageIteratorFunc)
	{
		tess.stageIteratorFunc = Tess_StageIteratorGeneric;
	}
	if(tess.stageIteratorFunc == Tess_StageIteratorGeneric)
	{
		if(state->isSky)
		{
			tess.stageIteratorFunc = Tess_StageIteratorSky;
			tess.stageIteratorFunc2 = Tess_StageIteratorGeneric;
		}
	}
	if(tess.stageIteratorFunc == Tess_StageIteratorGBuffer)
	{
		if(state->isSky)
		{
			tess.stageIteratorFunc = Tess_StageIteratorSky;
			tess.stageIteratorFunc2 = Tess_StageIteratorGBuffer;
		}
	}

	
	tess.skipTangentSpaces = skipTangentSpaces;
	tess.shadowVolume = shadowVolume;
	
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va("--- Tess_Begin( %s, %s, %i, %i ) ---\n", tess.surfaceShader->name, tess.lightShader ? tess.lightShader->name : NULL, tess.skipTangentSpaces, tess.shadowVolume));
	}
}
// *INDENT-ON*

static void Render_genericSingle(int stage)
{
	shaderStage_t  *pStage;

	GLimp_LogComment("--- Render_genericSingle ---\n");

	pStage = tess.surfaceStages[stage];

	GL_State(pStage->stateBits);
	GL_Program(tr.genericSingleShader.program);

	if(pStage->vertexColor || pStage->inverseVertexColor)
	{
		GL_ClientState(tr.genericSingleShader.attribs | GLCS_COLOR);
	}
	else
	{
		GL_ClientState(tr.genericSingleShader.attribs);

		qglColor4fv(tess.svars.color);
	}
	GL_SetVertexAttribs();

	// set uniforms
	qglUniform1iARB(tr.genericSingleShader.u_InverseVertexColor, pStage->inverseVertexColor);

	// bind u_ColorMap
	GL_SelectTexture(0);
	BindAnimatedImage(&pStage->bundle[TB_COLORMAP]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	GL_CheckErrors();
}

static void Render_geometricFill_DBS(int stage)
{
	shaderStage_t  *pStage;
	unsigned        stateBits;
	vec3_t          viewOrigin;
	float           specularExponent;

	GLimp_LogComment("--- Render_geometricFill_DBS ---\n");

	pStage = tess.surfaceStages[stage];

	// remove blend mode
	stateBits = pStage->stateBits;
	//stateBits &= ~(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);

	GL_State(stateBits);

	// enable shader, set arrays
	GL_Program(tr.geometricFillShader_DBS.program);
	GL_ClientState(tr.geometricFillShader_DBS.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	specularExponent = RB_EvalExpression(&pStage->specularExponentExp, r_specularExponent->value);

	qglUniform3fARB(tr.geometricFillShader_DBS.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform1fARB(tr.geometricFillShader_DBS.u_SpecularExponent, specularExponent);
	qglUniformMatrix4fvARB(tr.geometricFillShader_DBS.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);
	qglUniformMatrix4fvARB(tr.geometricFillShader_DBS.u_ModelViewMatrix, 1, GL_FALSE, backEnd.or.modelViewMatrix);

	if(r_parallaxMapping->integer)
	{
		float           depthScale;

		depthScale = RB_EvalExpression(&pStage->depthScaleExp, r_parallaxDepthScale->value);

		qglUniform1fARB(tr.geometricFillShader_DBS.u_DepthScale, depthScale);
	}

	// bind u_DiffuseMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_DIFFUSEMAP].image[0]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_DIFFUSEMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_NormalMap
	GL_SelectTexture(1);
	if(pStage->bundle[TB_NORMALMAP].image[0])
	{
		GL_Bind(pStage->bundle[TB_NORMALMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.flatImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_NORMALMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_SpecularMap
	GL_SelectTexture(2);
	if(pStage->bundle[TB_SPECULARMAP].image[0])
	{
		GL_Bind(pStage->bundle[TB_SPECULARMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.blackImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_SPECULARMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	GL_CheckErrors();
}


static void Render_depthFill(int stage)
{
	shaderStage_t  *pStage;
	unsigned        stateBits;
	float           alphaTest;
	vec4_t          ambientColor;

	GLimp_LogComment("--- Render_depthFill ---\n");

	pStage = tess.surfaceStages[stage];

	// remove alpha test
	stateBits = pStage->stateBits;
	stateBits &= ~(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS);

	GL_State(stateBits);

	// enable shader, set arrays
	GL_Program(tr.depthFillShader.program);
	GL_ClientState(tr.depthFillShader.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	if(pStage->stateBits & GLS_ATEST_BITS)
	{
		alphaTest = RB_EvalExpression(&pStage->alphaTestExp, 0.5);
	}
	else
	{
		alphaTest = -1.0;
	}

	VectorCopy(backEnd.currentEntity->ambientLight, ambientColor);
	ClampColor(ambientColor);

	qglUniform1fARB(tr.depthFillShader.u_AlphaTest, alphaTest);
	qglUniform3fARB(tr.depthFillShader.u_AmbientColor, ambientColor[0], ambientColor[1], ambientColor[2]);

	// bind u_ColorMap
	GL_SelectTexture(0);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_DIFFUSEMAP]);
	qglMatrixMode(GL_MODELVIEW);
	GL_Bind(pStage->bundle[TB_DIFFUSEMAP].image[0]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_shadowFill(int stage)
{
	shaderStage_t  *pStage;
	unsigned        stateBits;
	float           alphaTest;
	vec3_t          lightOrigin;


	GLimp_LogComment("--- Render_shadowFill ---\n");

	pStage = tess.surfaceStages[stage];

	// remove alpha test
	stateBits = pStage->stateBits;
	stateBits &= ~(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS);

	GL_State(stateBits);

	// enable shader, set arrays
	GL_Program(tr.shadowFillShader.program);
	GL_ClientState(tr.shadowFillShader.attribs);
	GL_SetVertexAttribs();

	if(r_debugShadowMaps->integer)
	{
		vec4_t          shadowMapColor;

		VectorCopy4(g_color_table[backEnd.pc.c_batches % 8], shadowMapColor);

		qglColor4fv(shadowMapColor);
	}

	// set uniforms
	if(pStage->stateBits & GLS_ATEST_BITS)
	{
		alphaTest = RB_EvalExpression(&pStage->alphaTestExp, 0.5);
	}
	else
	{
		alphaTest = -1.0;
	}
	VectorCopy(backEnd.currentLight->origin, lightOrigin);	// in world space

	qglUniform1fARB(tr.shadowFillShader.u_AlphaTest, alphaTest);
	qglUniform1fARB(tr.shadowFillShader.u_LightRadius, backEnd.currentLight->sphereRadius);
	qglUniform3fARB(tr.shadowFillShader.u_LightOrigin, lightOrigin[0], lightOrigin[1], lightOrigin[2]);
	qglUniformMatrix4fvARB(tr.shadowFillShader.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);

	if(pStage->stateBits & GLS_ATEST_BITS)
	{
		GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);
		qglMatrixMode(GL_TEXTURE);
		qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
		qglMatrixMode(GL_MODELVIEW);
	}
	else
	{
		GL_Bind(tr.whiteImage);
	}

	DrawElements();

	GL_CheckErrors();
}

static void Render_forwardLighting_DBS_omni(shaderStage_t * diffuseStage,
											shaderStage_t * attenuationXYStage,
											shaderStage_t * attenuationZStage, trRefLight_t * light)
{
	vec3_t          viewOrigin;
	vec3_t          lightOrigin;
	vec4_t          lightColor;
	float           shadowTexelSize;
	float           specularExponent;
	qboolean        shadowCompare;

	GLimp_LogComment("--- Render_forwardLighting_DBS_omni ---\n");

	// enable shader, set arrays
	GL_Program(tr.forwardLightingShader_DBS_omni.program);

	if(diffuseStage->vertexColor || diffuseStage->inverseVertexColor)
	{
		GL_ClientState(tr.forwardLightingShader_DBS_omni.attribs | GLCS_COLOR);
	}
	else
	{
		GL_ClientState(tr.forwardLightingShader_DBS_omni.attribs);

		qglColor4fv(colorWhite);
	}
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);
	VectorCopy(light->origin, lightOrigin);
	VectorCopy(tess.svars.color, lightColor);

	shadowCompare = !light->l.noShadows && light->shadowLOD >= 0;
	
	if(shadowCompare)
		shadowTexelSize = 1.0f / shadowMapResolutions[light->shadowLOD];
	else
		shadowTexelSize = 1.0f;
	
	specularExponent = RB_EvalExpression(&diffuseStage->specularExponentExp, r_specularExponent->value);

	qglUniform3fARB(tr.forwardLightingShader_DBS_omni.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_InverseVertexColor, diffuseStage->inverseVertexColor);
	qglUniform3fARB(tr.forwardLightingShader_DBS_omni.u_LightOrigin, lightOrigin[0], lightOrigin[1], lightOrigin[2]);
	qglUniform3fARB(tr.forwardLightingShader_DBS_omni.u_LightColor, lightColor[0], lightColor[1], lightColor[2]);
	qglUniform1fARB(tr.forwardLightingShader_DBS_omni.u_LightRadius, light->sphereRadius);
	qglUniform1fARB(tr.forwardLightingShader_DBS_omni.u_LightScale, r_lightScale->value);
	qglUniform1iARB(tr.forwardLightingShader_DBS_omni.u_ShadowCompare, shadowCompare);
	qglUniform1fARB(tr.forwardLightingShader_DBS_omni.u_ShadowTexelSize, shadowTexelSize);
	qglUniform1fARB(tr.forwardLightingShader_DBS_omni.u_ShadowBlur, r_shadowBlur->value);
	qglUniform1fARB(tr.forwardLightingShader_DBS_omni.u_SpecularExponent, specularExponent);
	qglUniformMatrix4fvARB(tr.forwardLightingShader_DBS_omni.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_DiffuseMap
	GL_SelectTexture(0);
	GL_Bind(diffuseStage->bundle[TB_DIFFUSEMAP].image[0]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_DIFFUSEMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_NormalMap
	GL_SelectTexture(1);
	if(diffuseStage->bundle[TB_NORMALMAP].image[0])
	{
		GL_Bind(diffuseStage->bundle[TB_NORMALMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.flatImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_NORMALMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_SpecularMap
	GL_SelectTexture(2);
	if(diffuseStage->bundle[TB_SPECULARMAP].image[0])
	{
		GL_Bind(diffuseStage->bundle[TB_SPECULARMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.blackImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_SPECULARMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_AttenuationMapXY
	GL_SelectTexture(3);
	BindAnimatedImage(&attenuationXYStage->bundle[TB_COLORMAP]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(light->attenuationMatrix2);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_AttenuationMapZ
	GL_SelectTexture(4);
	BindAnimatedImage(&attenuationZStage->bundle[TB_COLORMAP]);

	// bind u_ShadowMap
	if(r_shadows->integer >= 4 && shadowCompare)
	{
		GL_SelectTexture(5);
		GL_Bind(tr.shadowCubeFBOImage[light->shadowLOD]);
	}

	DrawElements();

	GL_CheckErrors();
}

static void Render_forwardLighting_DBS_proj(shaderStage_t * diffuseStage,
											shaderStage_t * attenuationXYStage,
											shaderStage_t * attenuationZStage, trRefLight_t * light)
{
	vec3_t          viewOrigin;
	vec3_t          lightOrigin;
	vec4_t          lightColor;
	float           shadowTexelSize;
	float           specularExponent;
	qboolean		shadowCompare;

	GLimp_LogComment("--- Render_fowardLighting_DBS_proj ---\n");

	// enable shader, set arrays
	GL_Program(tr.forwardLightingShader_DBS_proj.program);

	if(diffuseStage->vertexColor || diffuseStage->inverseVertexColor)
	{
		GL_ClientState(tr.forwardLightingShader_DBS_proj.attribs | GLCS_COLOR);
	}
	else
	{
		GL_ClientState(tr.forwardLightingShader_DBS_proj.attribs);

		qglColor4fv(colorWhite);
	}
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);
	VectorCopy(light->origin, lightOrigin);
	VectorCopy(tess.svars.color, lightColor);

	shadowCompare = !light->l.noShadows && light->shadowLOD >= 0;
	
	if(shadowCompare)
		shadowTexelSize = 1.0f / shadowMapResolutions[light->shadowLOD];
	else
		shadowTexelSize = 1.0f;
	
	specularExponent = RB_EvalExpression(&diffuseStage->specularExponentExp, r_specularExponent->value);

	qglUniform3fARB(tr.forwardLightingShader_DBS_proj.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_InverseVertexColor, diffuseStage->inverseVertexColor);
	qglUniform3fARB(tr.forwardLightingShader_DBS_proj.u_LightOrigin, lightOrigin[0], lightOrigin[1], lightOrigin[2]);
	qglUniform3fARB(tr.forwardLightingShader_DBS_proj.u_LightColor, lightColor[0], lightColor[1], lightColor[2]);
	qglUniform1fARB(tr.forwardLightingShader_DBS_proj.u_LightRadius, light->sphereRadius);
	qglUniform1fARB(tr.forwardLightingShader_DBS_proj.u_LightScale, r_lightScale->value);
	qglUniform1iARB(tr.forwardLightingShader_DBS_proj.u_ShadowCompare, shadowCompare);
	qglUniform1fARB(tr.forwardLightingShader_DBS_proj.u_ShadowTexelSize, shadowTexelSize);
	qglUniform1fARB(tr.forwardLightingShader_DBS_proj.u_ShadowBlur, r_shadowBlur->value);
	qglUniformMatrix4fvARB(tr.forwardLightingShader_DBS_proj.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);
	qglUniform1fARB(tr.forwardLightingShader_DBS_proj.u_SpecularExponent, specularExponent);
	qglUniformMatrix4fvARB(tr.forwardLightingShader_DBS_proj.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_DiffuseMap
	GL_SelectTexture(0);
	GL_Bind(diffuseStage->bundle[TB_DIFFUSEMAP].image[0]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_DIFFUSEMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_NormalMap
	GL_SelectTexture(1);
	if(diffuseStage->bundle[TB_NORMALMAP].image[0])
	{
		GL_Bind(diffuseStage->bundle[TB_NORMALMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.flatImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_NORMALMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_SpecularMap
	GL_SelectTexture(2);
	if(diffuseStage->bundle[TB_SPECULARMAP].image[0])
	{
		GL_Bind(diffuseStage->bundle[TB_SPECULARMAP].image[0]);
	}
	else
	{
		GL_Bind(tr.blackImage);
	}
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_SPECULARMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_AttenuationMapXY
	GL_SelectTexture(3);
	BindAnimatedImage(&attenuationXYStage->bundle[TB_COLORMAP]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(light->attenuationMatrix2);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_AttenuationMapZ
	GL_SelectTexture(4);
	BindAnimatedImage(&attenuationZStage->bundle[TB_COLORMAP]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(light->attenuationMatrix);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_ShadowMap
	if(r_shadows->integer >= 4 && shadowCompare)
	{
		GL_SelectTexture(5);
		GL_Bind(tr.shadowMapFBOImage[light->shadowLOD]);
	}

	DrawElements();

	GL_CheckErrors();
}

static void Render_reflection_C(int stage)
{
	vec3_t          viewOrigin;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_reflection_C ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.reflectionShader_C.program);
	GL_ClientState(tr.reflectionShader_C.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	qglUniform3fARB(tr.reflectionShader_C.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniformMatrix4fvARB(tr.reflectionShader_C.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_reflection_CB(int stage)
{
	vec3_t          viewOrigin;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_reflection_CB ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.reflectionShader_CB.program);
	GL_ClientState(tr.reflectionShader_CB.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	qglUniform3fARB(tr.reflectionShader_CB.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniformMatrix4fvARB(tr.reflectionShader_CB.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);

	// bind u_NormalMap
	GL_SelectTexture(1);
	GL_Bind(pStage->bundle[TB_NORMALMAP].image[0]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_NORMALMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	GL_CheckErrors();
}

static void Render_refraction_C(int stage)
{
	vec3_t          viewOrigin;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_refraction_C ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.refractionShader_C.program);
	GL_ClientState(tr.refractionShader_C.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	qglUniform3fARB(tr.refractionShader_C.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform1fARB(tr.refractionShader_C.u_RefractionIndex, RB_EvalExpression(&pStage->refractionIndexExp, 1.0));
	qglUniform1fARB(tr.refractionShader_C.u_FresnelPower, RB_EvalExpression(&pStage->fresnelPowerExp, 2.0));
	qglUniform1fARB(tr.refractionShader_C.u_FresnelScale, RB_EvalExpression(&pStage->fresnelScaleExp, 2.0));
	qglUniform1fARB(tr.refractionShader_C.u_FresnelBias, RB_EvalExpression(&pStage->fresnelBiasExp, 1.0));
	qglUniformMatrix4fvARB(tr.refractionShader_C.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_dispersion_C(int stage)
{
	vec3_t          viewOrigin;
	shaderStage_t  *pStage = tess.surfaceStages[stage];
	float           eta;
	float           etaDelta;

	GLimp_LogComment("--- Render_dispersion_C ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.dispersionShader_C.program);
	GL_ClientState(tr.dispersionShader_C.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	eta = RB_EvalExpression(&pStage->etaExp, (float)1.1);
	etaDelta = RB_EvalExpression(&pStage->etaDeltaExp, (float)-0.02);

	qglUniform3fARB(tr.dispersionShader_C.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform3fARB(tr.dispersionShader_C.u_EtaRatio, eta, eta + etaDelta, eta + (etaDelta * 2));
	qglUniform1fARB(tr.dispersionShader_C.u_FresnelPower, RB_EvalExpression(&pStage->fresnelPowerExp, 2.0f));
	qglUniform1fARB(tr.dispersionShader_C.u_FresnelScale, RB_EvalExpression(&pStage->fresnelScaleExp, 2.0f));
	qglUniform1fARB(tr.dispersionShader_C.u_FresnelBias, RB_EvalExpression(&pStage->fresnelBiasExp, 1.0f));
	qglUniformMatrix4fvARB(tr.dispersionShader_C.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_skybox(int stage)
{
	vec3_t          viewOrigin;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_skybox ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.skyBoxShader.program);
	GL_ClientState(tr.skyBoxShader.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space
	qglUniform3fARB(tr.skyBoxShader.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniformMatrix4fvARB(tr.skyBoxShader.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_screen(int stage)
{
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_screen ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.screenShader.program);

	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglColor4fv(tess.svars.color);
		GL_ClientState(GLCS_VERTEX);
	}
	else
	{
		GL_ClientState(GLCS_VERTEX | GLCS_COLOR);
	}

	GL_SetVertexAttribs();

	// set uniforms
	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	qglUniform2fARB(tr.screenShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.screenShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	// bind u_CurrentMap
	GL_SelectTexture(0);
	BindAnimatedImage(&pStage->bundle[TB_COLORMAP]);

	DrawElements();

	GL_CheckErrors();
}

static void Render_heatHaze(int stage)
{
	unsigned        stateBits;
	float           alphaTest;
	float           deformMagnitude;
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_heatHaze ---\n");

	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	/*
	   if(glConfig.framebufferObjectAvailable && glConfig.maxColorAttachments >= 4 &&
	   glConfig.drawBuffersAvailable && glConfig.maxDrawBuffers >= 4)
	   {
	   unsigned        stateBits;

	   // remove blend mode
	   stateBits = pStage->stateBits;
	   stateBits &= ~(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);

	   GL_State(stateBits);

	   // enable shader, set arrays
	   GL_Program(tr.depthTestShader.program);
	   GL_ClientState(tr.depthTestShader.attribs);
	   GL_SetVertexAttribs();

	   // set uniforms      
	   qglUniform2fARB(tr.depthTestShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	   qglUniform2fARB(tr.depthTestShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	   // bind u_ColorMap
	   GL_SelectTexture(0);
	   GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);
	   qglMatrixMode(GL_TEXTURE);
	   qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	   qglMatrixMode(GL_MODELVIEW);

	   // bind u_CurrentMap
	   GL_SelectTexture(1);
	   if(backEnd.viewParms.isPortal)
	   {
	   GL_Bind(tr.portalRenderFBOImage[0]);
	   GL_TextureFilter(tr.portalRenderFBOImage[0], FT_NEAREST);
	   }
	   else
	   {
	   GL_Bind(tr.currentRenderFBOImage[0]);
	   GL_TextureFilter(tr.currentRenderFBOImage[0], FT_NEAREST);
	   }
	   qglMatrixMode(GL_TEXTURE);
	   qglLoadIdentity();
	   qglMatrixMode(GL_MODELVIEW);

	   DrawElements();
	   }
	   else
	 */
	{
		unsigned        stateBits;

		// capture current color buffer for u_CurrentMap
		GL_SelectTexture(0);
		GL_Bind(tr.currentRenderImage);
		qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth,
							 tr.currentRenderImage->uploadHeight);

		// clear color buffer
		qglClear(GL_COLOR_BUFFER_BIT);

		// remove blend mode
		stateBits = pStage->stateBits;
		stateBits &= ~(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);

		GL_State(stateBits);

		// enable shader, set arrays
		GL_Program(tr.depthTestShader.program);
		GL_ClientState(tr.depthTestShader.attribs);
		GL_SetVertexAttribs();

		// set uniforms 
		qglUniform2fARB(tr.depthTestShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
		qglUniform2fARB(tr.depthTestShader.u_NPOTScale, npotWidthScale, npotHeightScale);

		// bind u_ColorMap
		GL_SelectTexture(0);
		GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);
		qglMatrixMode(GL_TEXTURE);
		qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
		qglMatrixMode(GL_MODELVIEW);

		// bind u_CurrentMap
		GL_SelectTexture(1);
		GL_Bind(tr.currentRenderImage);

		DrawElements();

		// capture current color buffer for u_ContrastMap 
		GL_SelectTexture(0);
		GL_Bind(tr.contrastRenderImage);
		qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.contrastRenderImage->uploadWidth,
							 tr.contrastRenderImage->uploadHeight);


		//
		// restore color buffer
		//

		// enable shader, set arrays
		GL_Program(tr.screenShader.program);
		GL_State(GLS_DEPTHTEST_DISABLE);
		qglColor4fv(colorWhite);
		//GL_ClientState(tr.screenShader.attribs);
		//GL_SetVertexAttribs();
		GL_Cull(CT_TWO_SIDED);

		// set uniforms
		qglUniform2fARB(tr.screenShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
		qglUniform2fARB(tr.screenShader.u_NPOTScale, npotWidthScale, npotHeightScale);

		// bind u_CurrentMap
		GL_SelectTexture(0);
		GL_Bind(tr.currentRenderImage);

		// set 2D virtual screen size
		qglPushMatrix();
		qglLoadIdentity();
		qglMatrixMode(GL_PROJECTION);
		qglPushMatrix();
		qglLoadIdentity();
		qglOrtho(backEnd.viewParms.viewportX,
				 backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
				 backEnd.viewParms.viewportY, backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999);

		qglBegin(GL_QUADS);
		qglVertex2f(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY);
		qglVertex2f(backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportY);
		qglVertex2f(backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
					backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
		qglVertex2f(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
		qglEnd();

		qglPopMatrix();
		qglMatrixMode(GL_MODELVIEW);
		qglPopMatrix();
	}

#if 1
	// remove alpha test
	stateBits = pStage->stateBits;
	stateBits &= ~GLS_ATEST_BITS;

	GL_State(stateBits);

	// enable shader, set arrays
	GL_Program(tr.heatHazeShader.program);
	GL_ClientState(tr.heatHazeShader.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	if(pStage->stateBits & GLS_ATEST_BITS)
	{
		alphaTest = RB_EvalExpression(&pStage->alphaTestExp, 0.5);
	}
	else
	{
		alphaTest = -1.0;
	}

	deformMagnitude = RB_EvalExpression(&pStage->deformMagnitudeExp, 1.0);

	qglUniform1fARB(tr.heatHazeShader.u_AlphaTest, alphaTest);
	qglUniform1fARB(tr.heatHazeShader.u_DeformMagnitude, deformMagnitude);
	qglUniform2fARB(tr.heatHazeShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.heatHazeShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	if(glConfig.hardwareType == GLHW_ATI)
	{
		matrix_t        projectionMatrixTranspose;

		MatrixTranspose(backEnd.viewParms.projectionMatrix, projectionMatrixTranspose);
		qglUniformMatrix4fvARB(tr.heatHazeShader.u_ProjectionMatrixTranspose, 1, GL_FALSE, projectionMatrixTranspose);
	}

	// bind u_NormalMap
	GL_SelectTexture(0);
	GL_Bind(pStage->bundle[TB_COLORMAP].image[0]);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	qglMatrixMode(GL_MODELVIEW);

	// bind u_CurrentMap
	GL_SelectTexture(1);
	/*
	   if(glConfig.framebufferObjectAvailable)
	   {
	   if(backEnd.viewParms.isPortal)
	   {
	   GL_Bind(tr.portalRenderFBOImage[0]);
	   GL_TextureFilter(tr.portalRenderFBOImage[0], FT_NEAREST);
	   }
	   else
	   {
	   GL_Bind(tr.currentRenderFBOImage[0]);
	   GL_TextureFilter(tr.currentRenderFBOImage[0], FT_NEAREST);
	   }
	   }
	   else
	 */
	{
		GL_Bind(tr.currentRenderImage);
		qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth,
							 tr.currentRenderImage->uploadHeight);
	}

	// bind u_ContrastMap
	GL_SelectTexture(2);
	/*
	   if(glConfig.framebufferObjectAvailable)
	   {
	   if(backEnd.viewParms.isPortal)
	   {
	   GL_Bind(tr.portalRenderFBOImage[1]);
	   GL_TextureFilter(tr.portalRenderFBOImage[1], FT_NEAREST);
	   }
	   else
	   {
	   GL_Bind(tr.currentRenderFBOImage[1]);
	   GL_TextureFilter(tr.currentRenderFBOImage[1], FT_NEAREST);
	   }
	   }
	   else
	 */
	{
		GL_Bind(tr.contrastRenderImage);
	}

	DrawElements();
#endif

	GL_CheckErrors();
}

static void Render_bloom(int stage)
{
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_bloom ---\n");

	GL_State(pStage->stateBits);

	// calc uniforms
	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	// render contrast
	GL_Program(tr.contrastShader.program);
	GL_ClientState(tr.contrastShader.attribs);
	GL_SetVertexAttribs();

	qglUniform2fARB(tr.contrastShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.contrastShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_SelectTexture(0);
	GL_Bind(tr.currentRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth, tr.currentRenderImage->uploadHeight);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	// render bloom
	GL_Program(tr.bloomShader.program);
	GL_ClientState(tr.bloomShader.attribs);
	GL_SetVertexAttribs();

	qglUniform1fARB(tr.bloomShader.u_BlurMagnitude, r_bloomBlur->value);
	qglUniform2fARB(tr.bloomShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.bloomShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_SelectTexture(1);
	GL_Bind(tr.contrastRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.contrastRenderImage->uploadWidth, tr.contrastRenderImage->uploadHeight);

	DrawElements();

	GL_CheckErrors();
}

static void Render_bloom2(int stage)
{
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_bloom2 ---\n");

	GL_State(pStage->stateBits);

	// calc uniforms
	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	// render contrast
	GL_Program(tr.contrastShader.program);
	GL_ClientState(tr.contrastShader.attribs);
	GL_SetVertexAttribs();

	qglUniform2fARB(tr.contrastShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.contrastShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_SelectTexture(0);
	GL_Bind(tr.currentRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth, tr.currentRenderImage->uploadHeight);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	// render blurX
	GL_Program(tr.blurXShader.program);
	GL_ClientState(tr.blurXShader.attribs);
	GL_SetVertexAttribs();

	qglUniform1fARB(tr.blurXShader.u_BlurMagnitude, r_bloomBlur->value);
	qglUniform2fARB(tr.blurXShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.blurXShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_Bind(tr.contrastRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.contrastRenderImage->uploadWidth, tr.contrastRenderImage->uploadHeight);

	DrawElements();

	// render blurY
	GL_Program(tr.blurYShader.program);
	GL_ClientState(tr.blurYShader.attribs);
	GL_SetVertexAttribs();

	qglUniform1fARB(tr.blurYShader.u_BlurMagnitude, r_bloomBlur->value);
	qglUniform2fARB(tr.blurYShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.blurYShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_Bind(tr.contrastRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.contrastRenderImage->uploadWidth, tr.contrastRenderImage->uploadHeight);

	DrawElements();

	// render bloom
	GL_Program(tr.bloomShader.program);
	GL_ClientState(tr.bloomShader.attribs);
	GL_SetVertexAttribs();

	qglUniform1fARB(tr.bloomShader.u_BlurMagnitude, r_bloomBlur->value);
	qglUniform2fARB(tr.bloomShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.bloomShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	GL_SelectTexture(0);
	GL_Bind(tr.currentRenderImage);

	GL_SelectTexture(1);
	GL_Bind(tr.contrastRenderImage);

	DrawElements();

	GL_CheckErrors();
}

static void Render_rotoscope(int stage)
{
	float           blurMagnitude;
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_rotoscope ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.rotoscopeShader.program);
	GL_ClientState(tr.rotoscopeShader.attribs);
	GL_SetVertexAttribs();

	// set uniforms
	blurMagnitude = RB_EvalExpression(&pStage->blurMagnitudeExp, 3.0);

	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	qglUniform1fARB(tr.rotoscopeShader.u_BlurMagnitude, blurMagnitude);
	qglUniform2fARB(tr.rotoscopeShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.rotoscopeShader.u_NPOTScale, npotWidthScale, npotHeightScale);

	// bind u_ColorMap
	GL_SelectTexture(0);
	GL_Bind(tr.currentRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth, tr.currentRenderImage->uploadHeight);
	qglMatrixMode(GL_TEXTURE);
	qglLoadMatrixf(tess.svars.texMatrices[TB_COLORMAP]);
	qglMatrixMode(GL_MODELVIEW);

	DrawElements();

	GL_CheckErrors();
}

static void Render_liquid(int stage)
{
	vec3_t          viewOrigin;
	float           fbufWidthScale, fbufHeightScale;
	float           npotWidthScale, npotHeightScale;
	shaderStage_t  *pStage = tess.surfaceStages[stage];

	GLimp_LogComment("--- Render_liquid ---\n");

	GL_State(pStage->stateBits);

	// enable shader, set arrays
	GL_Program(tr.liquidShader.program);
	GL_ClientState(tr.liquidShader.attribs);
	GL_SetVertexAttribs();

	qglColor4fv(tess.svars.color);

	// set uniforms
	VectorCopy(backEnd.viewParms.or.origin, viewOrigin);	// in world space

	fbufWidthScale = Q_recip((float)glConfig.vidWidth);
	fbufHeightScale = Q_recip((float)glConfig.vidHeight);

	if(glConfig.textureNPOTAvailable)
	{
		npotWidthScale = 1;
		npotHeightScale = 1;
	}
	else
	{
		npotWidthScale = (float)glConfig.vidWidth / (float)NearestPowerOfTwo(glConfig.vidWidth);
		npotHeightScale = (float)glConfig.vidHeight / (float)NearestPowerOfTwo(glConfig.vidHeight);
	}

	qglUniform3fARB(tr.liquidShader.u_ViewOrigin, viewOrigin[0], viewOrigin[1], viewOrigin[2]);
	qglUniform1fARB(tr.liquidShader.u_RefractionIndex, RB_EvalExpression(&pStage->refractionIndexExp, 1.0));
	qglUniform1fARB(tr.liquidShader.u_FresnelPower, RB_EvalExpression(&pStage->fresnelPowerExp, 2.0));
	qglUniform1fARB(tr.liquidShader.u_FresnelScale, RB_EvalExpression(&pStage->fresnelScaleExp, 2.0));
	qglUniform1fARB(tr.liquidShader.u_FresnelBias, RB_EvalExpression(&pStage->fresnelBiasExp, 1.0));
	qglUniform2fARB(tr.liquidShader.u_FBufScale, fbufWidthScale, fbufHeightScale);
	qglUniform2fARB(tr.liquidShader.u_NPOTScale, npotWidthScale, npotHeightScale);
	qglUniformMatrix4fvARB(tr.liquidShader.u_ModelMatrix, 1, GL_FALSE, backEnd.or.transformMatrix);

	// capture current color buffer for u_CurrentMap
	GL_SelectTexture(0);
	GL_Bind(tr.currentRenderImage);
	qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.currentRenderImage->uploadWidth, tr.currentRenderImage->uploadHeight);

	// bind u_PortalMap
	GL_SelectTexture(1);
	GL_Bind(tr.portalRenderImage);

	DrawElements();

	GL_CheckErrors();
}

/*
===============
Tess_ComputeColor
===============
*/
void Tess_ComputeColor(shaderStage_t * pStage)
{
	float           rgb;
	float           red;
	float           green;
	float           blue;
	float           alpha;

	GLimp_LogComment("--- Tess_ComputeColor ---\n");

	// rgbGen
	switch (pStage->rgbGen)
	{
		case CGEN_IDENTITY:
		{
			tess.svars.color[0] = 1.0;
			tess.svars.color[1] = 1.0;
			tess.svars.color[2] = 1.0;
			tess.svars.color[3] = 1.0;
			break;
		}

		default:
		case CGEN_IDENTITY_LIGHTING:
		{
			tess.svars.color[0] = tr.identityLight;
			tess.svars.color[1] = tr.identityLight;
			tess.svars.color[2] = tr.identityLight;
			tess.svars.color[3] = tr.identityLight;
			break;
		}

		case CGEN_CONST:
		{
			tess.svars.color[0] = pStage->constantColor[0] * (1.0 / 255.0);
			tess.svars.color[1] = pStage->constantColor[1] * (1.0 / 255.0);
			tess.svars.color[2] = pStage->constantColor[2] * (1.0 / 255.0);
			tess.svars.color[3] = pStage->constantColor[3] * (1.0 / 255.0);
			break;
		}

		case CGEN_ENTITY:
		{
			if(backEnd.currentLight)
			{
				tess.svars.color[0] = Q_bound(0.0, backEnd.currentLight->l.color[0], 1.0);
				tess.svars.color[1] = Q_bound(0.0, backEnd.currentLight->l.color[1], 1.0);
				tess.svars.color[2] = Q_bound(0.0, backEnd.currentLight->l.color[2], 1.0);
				tess.svars.color[3] = 1.0;
			}
			else if(backEnd.currentEntity)
			{
				tess.svars.color[0] = Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[0] * (1.0 / 255.0), 1.0);
				tess.svars.color[1] = Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[1] * (1.0 / 255.0), 1.0);
				tess.svars.color[2] = Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[2] * (1.0 / 255.0), 1.0);
				tess.svars.color[3] = Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[3] * (1.0 / 255.0), 1.0);
			}
			else
			{
				tess.svars.color[0] = 1.0;
				tess.svars.color[1] = 1.0;
				tess.svars.color[2] = 1.0;
				tess.svars.color[3] = 1.0;
			}
			break;
		}

		case CGEN_ONE_MINUS_ENTITY:
		{
			if(backEnd.currentLight)
			{
				tess.svars.color[0] = 1.0 - Q_bound(0.0, backEnd.currentLight->l.color[0], 1.0);
				tess.svars.color[1] = 1.0 - Q_bound(0.0, backEnd.currentLight->l.color[1], 1.0);
				tess.svars.color[2] = 1.0 - Q_bound(0.0, backEnd.currentLight->l.color[2], 1.0);
				tess.svars.color[3] = 0.0;	// FIXME
			}
			else if(backEnd.currentEntity)
			{
				tess.svars.color[0] = 1.0 - Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[0] * (1.0 / 255.0), 1.0);
				tess.svars.color[1] = 1.0 - Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[1] * (1.0 / 255.0), 1.0);
				tess.svars.color[2] = 1.0 - Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[2] * (1.0 / 255.0), 1.0);
				tess.svars.color[3] = 1.0 - Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[3] * (1.0 / 255.0), 1.0);
			}
			else
			{
				tess.svars.color[0] = 0.0;
				tess.svars.color[1] = 0.0;
				tess.svars.color[2] = 0.0;
				tess.svars.color[3] = 0.0;
			}
			break;
		}

		case CGEN_WAVEFORM:
		{
			float           glow;
			waveForm_t     *wf;

			wf = &pStage->rgbWave;

			if(wf->func == GF_NOISE)
			{
				glow = wf->base + R_NoiseGet4f(0, 0, 0, (backEnd.refdef.floatTime + wf->phase) * wf->frequency) * wf->amplitude;
			}
			else
			{
				glow = RB_EvalWaveForm(wf) * tr.identityLight;
			}

			if(glow < 0)
			{
				glow = 0;
			}
			else if(glow > 1)
			{
				glow = 1;
			}

			tess.svars.color[0] = glow;
			tess.svars.color[1] = glow;
			tess.svars.color[2] = glow;
			tess.svars.color[3] = 1.0;
			break;
		}

		case CGEN_CUSTOM_RGB:
		{
			rgb = Q_bound(0.0, RB_EvalExpression(&pStage->rgbExp, 1.0), 1.0);

			tess.svars.color[0] = rgb;
			tess.svars.color[1] = rgb;
			tess.svars.color[2] = rgb;
			break;
		}

		case CGEN_CUSTOM_RGBs:
		{
			if(backEnd.currentLight)
			{
				red = Q_bound(0.0, RB_EvalExpression(&pStage->redExp, backEnd.currentLight->l.color[0]), 1.0);
				green = Q_bound(0.0, RB_EvalExpression(&pStage->greenExp, backEnd.currentLight->l.color[1]), 1.0);
				blue = Q_bound(0.0, RB_EvalExpression(&pStage->blueExp, backEnd.currentLight->l.color[2]), 1.0);
			}
			else if(backEnd.currentEntity)
			{
				red =
					Q_bound(0.0, RB_EvalExpression(&pStage->redExp, backEnd.currentEntity->e.shaderRGBA[0] * (1.0 / 255.0)), 1.0);
				green =
					Q_bound(0.0, RB_EvalExpression(&pStage->greenExp, backEnd.currentEntity->e.shaderRGBA[1] * (1.0 / 255.0)),
							1.0);
				blue =
					Q_bound(0.0, RB_EvalExpression(&pStage->blueExp, backEnd.currentEntity->e.shaderRGBA[2] * (1.0 / 255.0)),
							1.0);
			}
			else
			{
				red = Q_bound(0.0, RB_EvalExpression(&pStage->redExp, 1.0), 1.0);
				green = Q_bound(0.0, RB_EvalExpression(&pStage->greenExp, 1.0), 1.0);
				blue = Q_bound(0.0, RB_EvalExpression(&pStage->blueExp, 1.0), 1.0);
			}

			tess.svars.color[0] = red;
			tess.svars.color[1] = green;
			tess.svars.color[2] = blue;
			break;
		}
	}

	// alphaGen
	switch (pStage->alphaGen)
	{
		default:
		case AGEN_IDENTITY:
		{
			if(pStage->rgbGen != CGEN_IDENTITY)
			{
				tess.svars.color[3] = 1.0;
			}
			break;
		}

		case AGEN_CONST:
		{
			if(pStage->rgbGen != CGEN_CONST)
			{
				tess.svars.color[3] = pStage->constantColor[3] * (1.0 / 255.0);
			}
			break;
		}

		case AGEN_ENTITY:
		{
			if(backEnd.currentLight)
			{
				tess.svars.color[3] = 1.0;	// FIXME ?
			}
			else if(backEnd.currentEntity)
			{
				tess.svars.color[3] = Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[3] * (1.0 / 255.0), 1.0);
			}
			else
			{
				tess.svars.color[3] = 1.0;
			}
			break;
		}

		case AGEN_ONE_MINUS_ENTITY:
		{
			if(backEnd.currentLight)
			{
				tess.svars.color[3] = 0.0;	// FIXME ?
			}
			else if(backEnd.currentEntity)
			{
				tess.svars.color[3] = 1.0 - Q_bound(0.0, backEnd.currentEntity->e.shaderRGBA[3] * (1.0 / 255.0), 1.0);
			}
			else
			{
				tess.svars.color[3] = 0.0;
			}
			break;
		}

		case AGEN_WAVEFORM:
		{
			float           glow;
			waveForm_t     *wf;

			wf = &pStage->alphaWave;

			glow = RB_EvalWaveFormClamped(wf);

			tess.svars.color[3] = glow;
			break;
		}

		case AGEN_CUSTOM:
		{
			alpha = Q_bound(0.0, RB_EvalExpression(&pStage->alphaExp, 1.0), 1.0);

			tess.svars.color[3] = alpha;
			break;
		}
	}
}


/*
===============
Tess_ComputeTexMatrices
===============
*/
static void Tess_ComputeTexMatrices(shaderStage_t * pStage)
{
	int             i;
	vec_t          *matrix;

	GLimp_LogComment("--- Tess_ComputeTexMatrices ---\n");

	for(i = 0; i < MAX_TEXTURE_BUNDLES; i++)
	{
		matrix = tess.svars.texMatrices[i];

		RB_CalcTexMatrix(&pStage->bundle[i], matrix);
	}
}

void Tess_StageIteratorGeneric()
{
	int             stage;

	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorGeneric( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
						  tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	Tess_DeformGeometry();

	// set face culling appropriately
	GL_Cull(tess.surfaceShader->cullType);

	// set polygon offset if necessary
	if(tess.surfaceShader->polygonOffset)
	{
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor->value, r_offsetUnits->value);
	}

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	// call shader function
	for(stage = 0; stage < MAX_SHADER_STAGES; stage++)
	{
		shaderStage_t  *pStage = tess.surfaceStages[stage];

		if(!pStage)
		{
			break;
		}

		if(!RB_EvalExpression(&pStage->ifExp, 1.0))
		{
			continue;
		}

		Tess_ComputeColor(pStage);
		Tess_ComputeTexMatrices(pStage);

		switch (pStage->type)
		{
			case ST_COLORMAP:
			{
				Render_genericSingle(stage);
				break;
			}

			case ST_DIFFUSEMAP:
			case ST_COLLAPSE_lighting_DB:
			case ST_COLLAPSE_lighting_DBS:
			{
				if(tess.surfaceShader->sort <= SS_OPAQUE)
				{
					Render_depthFill(stage);
				}
				break;
			}

			case ST_COLLAPSE_reflection_CB:
			{
				Render_reflection_CB(stage);
				break;
			}

			case ST_REFLECTIONMAP:
			{
				Render_reflection_C(stage);
				break;
			}

			case ST_REFRACTIONMAP:
			{
				Render_refraction_C(stage);
				break;
			}

			case ST_DISPERSIONMAP:
			{
				Render_dispersion_C(stage);
				break;
			}

			case ST_SKYBOXMAP:
			{
				Render_skybox(stage);
				break;
			}

			case ST_SCREENMAP:
			{
				Render_screen(stage);
				break;
			}

			case ST_HEATHAZEMAP:
			{
				Render_heatHaze(stage);
				break;
			}

			case ST_BLOOMMAP:
			{
				Render_bloom(stage);
				break;
			}

			case ST_BLOOM2MAP:
			{
				Render_bloom2(stage);
				break;
			}

			case ST_ROTOSCOPEMAP:
			{
				Render_rotoscope(stage);
				break;
			}

			case ST_LIQUIDMAP:
			{
				Render_liquid(stage);
				break;
			}

			default:
				break;
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}

	// reset polygon offset
	if(tess.surfaceShader->polygonOffset)
	{
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}
}

void Tess_StageIteratorGBuffer()
{
	int             stage;

	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorGBuffer( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
						  tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	Tess_DeformGeometry();

	// set face culling appropriately   
	GL_Cull(tess.surfaceShader->cullType);

	// set polygon offset if necessary
	if(tess.surfaceShader->polygonOffset)
	{
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor->value, r_offsetUnits->value);
	}

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	// call shader function
	for(stage = 0; stage < MAX_SHADER_STAGES; stage++)
	{
		shaderStage_t  *pStage = tess.surfaceStages[stage];

		if(!pStage)
		{
			break;
		}

		if(!RB_EvalExpression(&pStage->ifExp, 1.0))
		{
			continue;
		}

		Tess_ComputeColor(pStage);
		Tess_ComputeTexMatrices(pStage);

		switch (pStage->type)
		{
			case ST_COLORMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);

				//Render_depthFill(stage);
				Render_genericSingle(stage);

				//R_BindFBO(tr.geometricRenderFBO);
				//Render_geometricFill_D(stage);
				break;
			}

			case ST_DIFFUSEMAP:
			case ST_COLLAPSE_lighting_DB:
			case ST_COLLAPSE_lighting_DBS:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_depthFill(stage);


				R_BindFBO(tr.geometricRenderFBO);
				Render_geometricFill_DBS(stage);
				break;
			}

			case ST_COLLAPSE_reflection_CB:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_reflection_CB(stage);
				break;
			}

			case ST_REFLECTIONMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_reflection_C(stage);
				break;
			}

			case ST_REFRACTIONMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_refraction_C(stage);
				break;
			}

			case ST_DISPERSIONMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_dispersion_C(stage);
				break;
			}

			case ST_SKYBOXMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_skybox(stage);
				break;
			}

			case ST_SCREENMAP:
			{
				R_BindFBO(tr.deferredRenderFBO);
				Render_screen(stage);
				break;
			}

			default:
				break;
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}

	// reset polygon offset
	qglDisable(GL_POLYGON_OFFSET_FILL);
}

void Tess_StageIteratorShadowFill()
{
	int             stage;

	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorShadowFill( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
						  tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	Tess_DeformGeometry();

	// set face culling appropriately   
	GL_Cull(tess.surfaceShader->cullType);

	// set polygon offset if necessary
	qglEnable(GL_POLYGON_OFFSET_FILL);
	qglPolygonOffset(r_shadowOffsetFactor->value, r_shadowOffsetUnits->value);

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	// call shader function
	for(stage = 0; stage < MAX_SHADER_STAGES; stage++)
	{
		shaderStage_t  *pStage = tess.surfaceStages[stage];

		if(!pStage)
		{
			break;
		}

		if(!RB_EvalExpression(&pStage->ifExp, 1.0))
		{
			continue;
		}

		Tess_ComputeTexMatrices(pStage);

		switch (pStage->type)
		{
			case ST_COLORMAP:
			{
				if(tess.surfaceShader->sort <= SS_OPAQUE)
				{
					Render_shadowFill(stage);
				}
				break;
			}

			case ST_DIFFUSEMAP:
			case ST_COLLAPSE_lighting_DB:
			case ST_COLLAPSE_lighting_DBS:
			{
				Render_shadowFill(stage);
				break;
			}

			default:
				break;
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}

	// reset polygon offset
	qglDisable(GL_POLYGON_OFFSET_FILL);
}

void Tess_StageIteratorStencilShadowVolume()
{
	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorStencilShadowVolume( %s, %i vertices, %i triangles ) ---\n",
						  tess.surfaceShader->name, tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	if(r_showShadowVolumes->integer)
	{
		//GL_State(GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		GL_State(GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		//GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		//GL_State(GLS_DEPTHMASK_TRUE);
#if 1
		GL_Cull(CT_FRONT_SIDED);
		//qglColor4f(1.0f, 1.0f, 0.7f, 0.05f);
		qglColor4f(1.0f, 0.0f, 0.0f, 0.05f);
		DrawElements();
#endif

#if 1
		GL_Cull(CT_BACK_SIDED);
		qglColor4f(0.0f, 1.0f, 0.0f, 0.05f);
		DrawElements();
#endif

#if 1
		GL_State(GLS_DEPTHFUNC_LESS | GLS_POLYMODE_LINE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		GL_Cull(CT_TWO_SIDED);
		qglColor4f(0.0f, 0.0f, 1.0f, 0.05f);
		DrawElements();
#endif
	}
	else
	{
		if(backEnd.currentEntity->needZFail)
		{
			// mirrors have the culling order reversed
			//if(backEnd.viewParms.isMirror)
			//  qglFrontFace(GL_CW);

			if(qglStencilFuncSeparateATI && qglStencilOpSeparateATI && glConfig.stencilWrapAvailable)
			{
				GL_Cull(CT_TWO_SIDED);

				qglStencilFuncSeparateATI(GL_ALWAYS, GL_ALWAYS, 0, (GLuint) ~ 0);

				qglStencilOpSeparateATI(GL_BACK, GL_KEEP, GL_DECR_WRAP_EXT, GL_KEEP);
				qglStencilOpSeparateATI(GL_FRONT, GL_KEEP, GL_INCR_WRAP_EXT, GL_KEEP);

				DrawElements();
			}
			else if(qglActiveStencilFaceEXT)
			{
				// render both sides at once
				GL_Cull(CT_TWO_SIDED);

				qglEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);

				qglActiveStencilFaceEXT(GL_BACK);
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_DECR_WRAP_EXT, GL_KEEP);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
				}

				qglActiveStencilFaceEXT(GL_FRONT);
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_INCR_WRAP_EXT, GL_KEEP);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
				}

				DrawElements();

				qglDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
			}
			else
			{
				// draw only the front faces of the shadow volume
				GL_Cull(CT_FRONT_SIDED);

				// increment the stencil value on zfail
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_INCR_WRAP_EXT, GL_KEEP);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
				}

				DrawElements();

				// draw only the back faces of the shadow volume
				GL_Cull(CT_BACK_SIDED);

				// decrement the stencil value on zfail
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_DECR_WRAP_EXT, GL_KEEP);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
				}

				DrawElements();
			}

			//if(backEnd.viewParms.isMirror)
			//  qglFrontFace(GL_CCW);
		}
		else
		{
			// Tr3B - zpass rendering is cheaper because we can skip the lightcap and darkcap
			// see GPU Gems1 9.5.4

			// mirrors have the culling order reversed
			//if(backEnd.viewParms.isMirror)
			//  qglFrontFace(GL_CW);

			if(qglStencilFuncSeparateATI && qglStencilOpSeparateATI && glConfig.stencilWrapAvailable)
			{
				GL_Cull(CT_TWO_SIDED);

				qglStencilFuncSeparateATI(GL_ALWAYS, GL_ALWAYS, 0, (GLuint) ~ 0);

				qglStencilOpSeparateATI(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP_EXT);
				qglStencilOpSeparateATI(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP_EXT);

				DrawElements();
			}
			else if(qglActiveStencilFaceEXT)
			{
				// render both sides at once
				GL_Cull(CT_TWO_SIDED);

				qglEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);

				qglActiveStencilFaceEXT(GL_BACK);
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP_EXT);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
				}

				qglActiveStencilFaceEXT(GL_FRONT);
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP_EXT);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
				}

				DrawElements();

				qglDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
			}
			else
			{
				// draw only the back faces of the shadow volume
				GL_Cull(CT_BACK_SIDED);

				// increment the stencil value on zpass
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP_EXT);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
				}

				DrawElements();

				// draw only the front faces of the shadow volume
				GL_Cull(CT_FRONT_SIDED);

				// decrement the stencil value on zpass
				if(glConfig.stencilWrapAvailable)
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP_EXT);
				}
				else
				{
					qglStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
				}

				DrawElements();
			}

			//if(backEnd.viewParms.isMirror)
			//  qglFrontFace(GL_CCW);
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}
}

void Tess_StageIteratorStencilLighting()
{
	int             i, j;
	trRefLight_t   *light;
	shaderStage_t  *attenuationXYStage;
	shaderStage_t  *attenuationZStage;

	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorStencilLighting( %s, %s, %i vertices, %i triangles ) ---\n",
						  tess.surfaceShader->name, tess.lightShader->name, tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	light = backEnd.currentLight;

	Tess_DeformGeometry();

	// set OpenGL state for lighting
#if 0
	if(!light->additive)
	{
		GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL | GLS_STENCILTEST_ENABLE);
	}
	else
#endif
	{
		if(tess.surfaceShader->sort > SS_OPAQUE)
		{
			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_STENCILTEST_ENABLE);
		}
		else
		{
			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL | GLS_STENCILTEST_ENABLE);
		}
	}

	// set face culling appropriately
	GL_Cull(tess.surfaceShader->cullType);

	// set polygon offset if necessary
	if(tess.surfaceShader->polygonOffset)
	{
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor->value, r_offsetUnits->value);
	}

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	// call shader function
	attenuationZStage = tess.lightShader->stages[0];

	for(i = 0; i < MAX_SHADER_STAGES; i++)
	{
		shaderStage_t  *diffuseStage = tess.surfaceStages[i];

		if(!diffuseStage)
		{
			break;
		}

		if(!RB_EvalExpression(&diffuseStage->ifExp, 1.0))
		{
			continue;
		}

		Tess_ComputeTexMatrices(diffuseStage);

		for(j = 1; j < MAX_SHADER_STAGES; j++)
		{
			attenuationXYStage = tess.lightShader->stages[j];

			if(!attenuationXYStage)
			{
				break;
			}

			if(attenuationXYStage->type != ST_ATTENUATIONMAP_XY)
			{
				continue;
			}

			if(!RB_EvalExpression(&attenuationXYStage->ifExp, 1.0))
			{
				continue;
			}

			Tess_ComputeColor(attenuationXYStage);
			R_ComputeFinalAttenuation(attenuationXYStage, light);

			switch (diffuseStage->type)
			{
				case ST_DIFFUSEMAP:
				case ST_COLLAPSE_lighting_DB:
				case ST_COLLAPSE_lighting_DBS:
					if(light->l.rlType == RL_OMNI)
					{
						Render_forwardLighting_DBS_omni(diffuseStage, attenuationXYStage, attenuationZStage, light);
					}
					else if(light->l.rlType == RL_PROJ)
					{
						Render_forwardLighting_DBS_proj(diffuseStage, attenuationXYStage, attenuationZStage, light);
					}
					else
					{
						// TODO ?
					}
					break;

				default:
					break;
			}
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}

	// reset polygon offset
	if(tess.surfaceShader->polygonOffset)
	{
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}
}

void Tess_StageIteratorLighting()
{
	int             i, j;
	trRefLight_t   *light;
	shaderStage_t  *attenuationXYStage;
	shaderStage_t  *attenuationZStage;

	// log this call
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment(va
						 ("--- Tess_StageIteratorLighting( %s, %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
						  tess.lightShader->name, tess.numVertexes, tess.numIndexes / 3));
	}

	GL_CheckErrors();

	light = backEnd.currentLight;

	Tess_DeformGeometry();

	// set OpenGL state for lighting
#if 0
	if(!light->additive)
	{
		GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);
	}
	else
#endif
	{
		if(tess.surfaceShader->sort > SS_OPAQUE)
		{
			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		}
		else
		{
			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);
		}
	}

	// set face culling appropriately
	GL_Cull(tess.surfaceShader->cullType);

	// set polygon offset if necessary
	if(tess.surfaceShader->polygonOffset)
	{
		qglEnable(GL_POLYGON_OFFSET_FILL);
		qglPolygonOffset(r_offsetFactor->value, r_offsetUnits->value);
	}

	// lock XYZ
	if(glConfig.vertexBufferObjectAvailable && glState.currentVBO)
	{
		qglVertexPointer(4, GL_FLOAT, 0, BUFFER_OFFSET(glState.currentVBO->ofsXYZ));
	}
	else
	{
		qglVertexPointer(4, GL_FLOAT, 0, tess.xyz);
	}

	if(qglLockArraysEXT)
	{
		qglLockArraysEXT(0, tess.numVertexes);
		GLimp_LogComment("glLockArraysEXT\n");
	}

	// call shader function
	attenuationZStage = tess.lightShader->stages[0];

	for(i = 0; i < MAX_SHADER_STAGES; i++)
	{
		shaderStage_t  *diffuseStage = tess.surfaceStages[i];

		if(!diffuseStage)
		{
			break;
		}

		if(!RB_EvalExpression(&diffuseStage->ifExp, 1.0))
		{
			continue;
		}

		Tess_ComputeTexMatrices(diffuseStage);

		for(j = 1; j < MAX_SHADER_STAGES; j++)
		{
			attenuationXYStage = tess.lightShader->stages[j];

			if(!attenuationXYStage)
			{
				break;
			}

			if(attenuationXYStage->type != ST_ATTENUATIONMAP_XY)
			{
				continue;
			}

			if(!RB_EvalExpression(&attenuationXYStage->ifExp, 1.0))
			{
				continue;
			}

			Tess_ComputeColor(attenuationXYStage);
			R_ComputeFinalAttenuation(attenuationXYStage, light);

			switch (diffuseStage->type)
			{
				case ST_DIFFUSEMAP:
				case ST_COLLAPSE_lighting_DB:
				case ST_COLLAPSE_lighting_DBS:
					if(light->l.rlType == RL_OMNI)
					{
						Render_forwardLighting_DBS_omni(diffuseStage, attenuationXYStage, attenuationZStage, light);
					}
					else if(light->l.rlType == RL_PROJ)
					{
						Render_forwardLighting_DBS_proj(diffuseStage, attenuationXYStage, attenuationZStage, light);
					}
					else
					{
						// TODO ?
					}
					break;

				default:
					break;
			}
		}
	}

	// unlock arrays
	if(qglUnlockArraysEXT)
	{
		qglUnlockArraysEXT();
		GLimp_LogComment("glUnlockArraysEXT\n");
	}

	// reset polygon offset
	if(tess.surfaceShader->polygonOffset)
	{
		qglDisable(GL_POLYGON_OFFSET_FILL);
	}
}

/*
=================
Tess_End

Render tesselated data
=================
*/
void Tess_End()
{
	if(tess.numIndexes == 0)
	{
		return;
	}

	if(tess.indexes[SHADER_MAX_INDEXES - 1] != 0)
	{
		ri.Error(ERR_DROP, "Tess_End() - SHADER_MAX_INDEXES hit");
	}
	if(tess.xyz[SHADER_MAX_VERTEXES - 1][0] != 0)
	{
		ri.Error(ERR_DROP, "Tess_End() - SHADER_MAX_VERTEXES hit");
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if(r_debugSort->integer && r_debugSort->integer < tess.surfaceShader->sort)
	{
		return;
	}

	// update performance counter
	backEnd.pc.c_batches++;

	// call off to shader specific tess end function
	tess.stageIteratorFunc();

	if(!tess.shadowVolume)
	{
		// draw debugging stuff
		if(r_showTris->integer || r_showBatches->integer ||
		   (r_showLightBatches->integer && (tess.stageIteratorFunc == Tess_StageIteratorLighting)))
		{
			DrawTris();
		}

		if(r_showNormals->integer)
		{
			DrawNormals();
		}

		if(r_showTangentSpaces->integer)
		{
			DrawTangentSpaces();
		}
	}

	// unbind VBO
	if(glConfig.vertexBufferObjectAvailable)
	{
		R_BindNullVBO();
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.numVertexes = 0;

	GLimp_LogComment("--- Tess_End ---\n");
}
