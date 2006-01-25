/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"

backEndData_t  *backEndData[SMP_FRAMES];
backEndState_t  backEnd;



/*
** GL_Bind
*/
void GL_Bind(image_t * image)
{
	int             texnum;

	if(!image)
	{
		ri.Printf(PRINT_WARNING, "GL_Bind: NULL image\n");
		texnum = tr.defaultImage->texnum;
	}
	else
	{
		texnum = image->texnum;
	}

	if(r_nobind->integer && tr.dlightImage)
	{							// performance evaluation option
		texnum = tr.dlightImage->texnum;
	}

	if(glState.currenttextures[glState.currenttmu] != texnum)
	{
		image->frameUsed = tr.frameCount;
		glState.currenttextures[glState.currenttmu] = texnum;
		qglBindTexture(image->type, texnum);
	}
}

void GL_Program(GLhandleARB program)
{
	if(glConfig2.shadingLanguage100Available)
	{
		if(glState.currentProgram != program)
		{
			glState.currentProgram = program;
			qglUseProgramObjectARB(program);
		}
	}
}

/*
** GL_SelectTexture
*/
void GL_SelectTexture(int unit)
{
	if(glState.currenttmu == unit)
	{
		return;
	}

	if(unit >= 0 && unit <= 7)
	{
		qglActiveTextureARB(GL_TEXTURE0_ARB + unit);
		qglClientActiveTextureARB(GL_TEXTURE0_ARB + unit);
		
		if(r_logFile->integer)
		{
			GLimp_LogComment(va("glActiveTextureARB( GL_TEXTURE%i_ARB )\n", unit));
			GLimp_LogComment(va("glClientActiveTextureARB( GL_TEXTURE%i_ARB )\n", unit));
		}
	}
	else
	{
		ri.Error(ERR_DROP, "GL_SelectTexture: unit = %i", unit);	
	}

	glState.currenttmu = unit;
}


/*
** GL_BindMultitexture
*/
void GL_BindMultitexture(image_t * image0, GLuint env0, image_t * image1, GLuint env1)
{
	int             texnum0, texnum1;

	texnum0 = image0->texnum;
	texnum1 = image1->texnum;

	if(r_nobind->integer && tr.dlightImage)
	{							// performance evaluation option
		texnum0 = texnum1 = tr.dlightImage->texnum;
	}

	if(glState.currenttextures[1] != texnum1)
	{
		GL_SelectTexture(1);
		image1->frameUsed = tr.frameCount;
		glState.currenttextures[1] = texnum1;
		qglBindTexture(GL_TEXTURE_2D, texnum1);
	}
	if(glState.currenttextures[0] != texnum0)
	{
		GL_SelectTexture(0);
		image0->frameUsed = tr.frameCount;
		glState.currenttextures[0] = texnum0;
		qglBindTexture(GL_TEXTURE_2D, texnum0);
	}
}


/*
** GL_Cull
*/
void GL_Cull(int cullType)
{
	if(glState.faceCulling == cullType)
	{
		return;
	}

	glState.faceCulling = cullType;

	if(cullType == CT_TWO_SIDED)
	{
		qglDisable(GL_CULL_FACE);
	}
	else
	{
		qglEnable(GL_CULL_FACE);

		if(cullType == CT_BACK_SIDED)
		{
			if(backEnd.viewParms.isMirror)
			{
				qglCullFace(GL_FRONT);
			}
			else
			{
				qglCullFace(GL_BACK);
			}
		}
		else
		{
			if(backEnd.viewParms.isMirror)
			{
				qglCullFace(GL_BACK);
			}
			else
			{
				qglCullFace(GL_FRONT);
			}
		}
	}
}

/*
** GL_TexEnv
*/
void GL_TexEnv(int env)
{
	if(env == glState.texEnv[glState.currenttmu])
	{
		return;
	}

	glState.texEnv[glState.currenttmu] = env;


	switch (env)
	{
		case GL_MODULATE:
			qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			break;
		case GL_REPLACE:
			qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			break;
		case GL_DECAL:
			qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
			break;
		case GL_ADD:
			qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			break;
		default:
			ri.Error(ERR_DROP, "GL_TexEnv: invalid env '%d' passed\n", env);
			break;
	}
}

/*
** GL_State
**
** This routine is responsible for setting the most commonly changed state
** in Q3.
*/
void GL_State(unsigned long stateBits)
{
	unsigned long   diff = stateBits ^ glState.glStateBits;

	if(!diff)
	{
		return;
	}

	// check depthFunc bits
	if(diff & GLS_DEPTHFUNC_EQUAL)
	{
		if(stateBits & GLS_DEPTHFUNC_EQUAL)
		{
			qglDepthFunc(GL_EQUAL);
		}
		else
		{
			qglDepthFunc(GL_LEQUAL);
		}
	}

	// check blend bits
	if(diff & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS))
	{
		GLenum          srcFactor, dstFactor;

		if(stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS))
		{
			switch (stateBits & GLS_SRCBLEND_BITS)
			{
				case GLS_SRCBLEND_ZERO:
					srcFactor = GL_ZERO;
					break;
				case GLS_SRCBLEND_ONE:
					srcFactor = GL_ONE;
					break;
				case GLS_SRCBLEND_DST_COLOR:
					srcFactor = GL_DST_COLOR;
					break;
				case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
					srcFactor = GL_ONE_MINUS_DST_COLOR;
					break;
				case GLS_SRCBLEND_SRC_ALPHA:
					srcFactor = GL_SRC_ALPHA;
					break;
				case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
					srcFactor = GL_ONE_MINUS_SRC_ALPHA;
					break;
				case GLS_SRCBLEND_DST_ALPHA:
					srcFactor = GL_DST_ALPHA;
					break;
				case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
					srcFactor = GL_ONE_MINUS_DST_ALPHA;
					break;
				case GLS_SRCBLEND_ALPHA_SATURATE:
					srcFactor = GL_SRC_ALPHA_SATURATE;
					break;
				default:
					srcFactor = GL_ONE;	// to get warning to shut up
					ri.Error(ERR_DROP, "GL_State: invalid src blend state bits\n");
					break;
			}

			switch (stateBits & GLS_DSTBLEND_BITS)
			{
				case GLS_DSTBLEND_ZERO:
					dstFactor = GL_ZERO;
					break;
				case GLS_DSTBLEND_ONE:
					dstFactor = GL_ONE;
					break;
				case GLS_DSTBLEND_SRC_COLOR:
					dstFactor = GL_SRC_COLOR;
					break;
				case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
					dstFactor = GL_ONE_MINUS_SRC_COLOR;
					break;
				case GLS_DSTBLEND_SRC_ALPHA:
					dstFactor = GL_SRC_ALPHA;
					break;
				case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
					dstFactor = GL_ONE_MINUS_SRC_ALPHA;
					break;
				case GLS_DSTBLEND_DST_ALPHA:
					dstFactor = GL_DST_ALPHA;
					break;
				case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
					dstFactor = GL_ONE_MINUS_DST_ALPHA;
					break;
				default:
					dstFactor = GL_ONE;	// to get warning to shut up
					ri.Error(ERR_DROP, "GL_State: invalid dst blend state bits\n");
					break;
			}

			qglEnable(GL_BLEND);
			qglBlendFunc(srcFactor, dstFactor);
		}
		else
		{
			qglDisable(GL_BLEND);
		}
	}
	
	// check colormask
	if(diff & GLS_COLORMASK_BITS)
	{
		if(stateBits & GLS_COLORMASK_BITS)
		{
			qglColorMask(
				(stateBits & GLS_REDMASK_FALSE) ? GL_FALSE : GL_TRUE,
				(stateBits & GLS_GREENMASK_FALSE) ? GL_FALSE : GL_TRUE,
				(stateBits & GLS_BLUEMASK_FALSE) ? GL_FALSE : GL_TRUE,
				(stateBits & GLS_ALPHAMASK_FALSE) ? GL_FALSE : GL_TRUE);
		}
		else
		{
			qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
	}

	// check depthmask
	if(diff & GLS_DEPTHMASK_TRUE)
	{
		if(stateBits & GLS_DEPTHMASK_TRUE)
		{
			qglDepthMask(GL_TRUE);
		}
		else
		{
			qglDepthMask(GL_FALSE);
		}
	}

	// fill/line mode
	if(diff & GLS_POLYMODE_LINE)
	{
		if(stateBits & GLS_POLYMODE_LINE)
		{
			qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}
		else
		{
			qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}

	// depthtest
	if(diff & GLS_DEPTHTEST_DISABLE)
	{
		if(stateBits & GLS_DEPTHTEST_DISABLE)
		{
			qglDisable(GL_DEPTH_TEST);
		}
		else
		{
			qglEnable(GL_DEPTH_TEST);
		}
	}

	// alpha test
	if(diff & GLS_ATEST_BITS)
	{
		switch (stateBits & GLS_ATEST_BITS)
		{
			case 0:
				qglDisable(GL_ALPHA_TEST);
				break;
			case GLS_ATEST_GT_0:
				qglEnable(GL_ALPHA_TEST);
				qglAlphaFunc(GL_GREATER, 0.0f);
				break;
			case GLS_ATEST_LT_80:
				qglEnable(GL_ALPHA_TEST);
				qglAlphaFunc(GL_LESS, 0.5f);
				break;
			case GLS_ATEST_GE_80:
				qglEnable(GL_ALPHA_TEST);
				qglAlphaFunc(GL_GEQUAL, 0.5f);
				break;
			case GLS_ATEST_GT_CUSTOM:
				// FIXME
				qglEnable(GL_ALPHA_TEST);
				qglAlphaFunc(GL_GREATER, 0.5f);
				break;
			default:
				assert(0);
				break;
		}
	}
	
	// stenciltest
	if(diff & GLS_STENCILTEST_ENABLE)
	{
		if(stateBits & GLS_STENCILTEST_ENABLE)
		{
			qglEnable(GL_STENCIL_TEST);
		}
		else
		{
			qglDisable(GL_STENCIL_TEST);
		}
	}

	glState.glStateBits = stateBits;
}



/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace(void)
{
	float           c;

	if(!backEnd.isHyperspace)
	{
		// do initialization shit
	}

	c = (backEnd.refdef.time & 255) / 255.0f;
	qglClearColor(c, c, c, 1);
	qglClear(GL_COLOR_BUFFER_BIT);

	backEnd.isHyperspace = qtrue;
}


static void SetViewportAndScissor(void)
{
#if 0
	extern const float	s_flipMatrix[16];
	matrix_t		projectionMatrix;
	
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	MatrixMultiply(backEnd.viewParms.projectionMatrix, s_flipMatrix, projectionMatrix);
	
	qglMatrixMode(GL_PROJECTION);
	qglLoadMatrixf(projectionMatrix);
	qglMatrixMode(GL_MODELVIEW);
#else
	qglMatrixMode(GL_PROJECTION);
	qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
	qglMatrixMode(GL_MODELVIEW);
#endif

	// set the window clipping
	qglViewport(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
				backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	
	qglScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
			   backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
}

/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView(void)
{
	int             	clearBits = 0;
	extern const float	s_flipMatrix[16];
	
	GLimp_LogComment("--- RB_BeginDrawingView ---\n");
	
	// sync with gl if needed
	if(r_finish->integer == 1 && !glState.finishCalled)
	{
		qglFinish();
		glState.finishCalled = qtrue;
	}
	if(r_finish->integer == 0)
	{
		glState.finishCalled = qtrue;
	}

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// set the modelview matrix for the viewer
	SetViewportAndScissor();

	// ensures that depth writes are enabled for the depth clear
	GL_State(GLS_DEFAULT);
	
	// clear relevant buffers
	clearBits = GL_DEPTH_BUFFER_BIT;

	if(r_measureOverdraw->integer || r_shadows->integer == 3)
	{
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}
	if(r_fastsky->integer && !(backEnd.refdef.rdflags & RDF_NOWORLDMODEL))
	{
		clearBits |= GL_COLOR_BUFFER_BIT;	// FIXME: only if sky shaders have been used
#ifdef _DEBUG
		qglClearColor(0.8f, 0.7f, 0.4f, 1.0f);	// FIXME: get color of sky
#else
		qglClearColor(0.0f, 0.0f, 1.0f, 1.0f);	// FIXME: get color of sky
#endif
	}
	qglClear(clearBits);

	if((backEnd.refdef.rdflags & RDF_HYPERSPACE))
	{
		RB_Hyperspace();
		return;
	}
	else
	{
		backEnd.isHyperspace = qfalse;
	}

	glState.faceCulling = -1;	// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	// clip to the plane of the portal
	if(backEnd.viewParms.isPortal)
	{
		float           plane[4];
		double          plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct(backEnd.viewParms.or.axis[0], plane);
		plane2[1] = DotProduct(backEnd.viewParms.or.axis[1], plane);
		plane2[2] = DotProduct(backEnd.viewParms.or.axis[2], plane);
		plane2[3] = DotProduct(plane, backEnd.viewParms.or.origin) - plane[3];

//		qglLoadIdentity();
		qglLoadMatrixf(s_flipMatrix);
		qglClipPlane(GL_CLIP_PLANE0, plane2);
		qglEnable(GL_CLIP_PLANE0);
	}
	else
	{
		qglDisable(GL_CLIP_PLANE0);
	}
}

void RB_RenderDrawSurfListFull(float originalTime, drawSurf_t * drawSurfs, int numDrawSurfs)
{
	shader_t       *shader, *oldShader;
	int             fogNum, oldFogNum;
	int             entityNum, oldEntityNum;
	qboolean        depthRange, oldDepthRange;
	int             i;
	drawSurf_t     *drawSurf;
	int             oldSort;
	
	GLimp_LogComment("--- RB_RenderDrawSurfListFull ---\n");

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;

	for(i = 0, drawSurf = drawSurfs; i < numDrawSurfs; i++, drawSurf++)
	{
		if(drawSurf->sort == oldSort)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
			continue;
		}
		oldSort = drawSurf->sort;
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);

		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if(shader != oldShader || fogNum != oldFogNum
		   || (entityNum != oldEntityNum && !shader->entityMergable))
		{
			if(oldShader != NULL)
			{
				RB_EndSurface();
			}
			RB_BeginSurface(shader, fogNum);
			oldShader = shader;
			oldFogNum = fogNum;
		}

		// change the modelview matrix if needed
		if(entityNum != oldEntityNum)
		{
			depthRange = qfalse;

			if(entityNum != ENTITYNUM_WORLD)
			{
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
				
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
				
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}

			qglLoadMatrixf(backEnd.or.modelViewMatrix);

			// change depthrange if needed
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					qglDepthRange(0, 0.3);
				}
				else
				{
					qglDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
	}

	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if(oldShader != NULL)
	{
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
}


void RB_RenderDrawSurfListZFill(float originalTime, drawSurf_t * drawSurfs, int numDrawSurfs)
{
	shader_t       *shader, *oldShader;
	int             fogNum, oldFogNum;
	int             entityNum, oldEntityNum;
	qboolean        depthRange, oldDepthRange;
	int             i;
	drawSurf_t     *drawSurf;
	int             oldSort;
	
	GLimp_LogComment("--- RB_RenderDrawSurfListZFill ---\n");

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;
	
	tess.currentStageIteratorType = SIT_ZFILL;

	for(i = 0, drawSurf = drawSurfs; i < numDrawSurfs; i++, drawSurf++)
	{
		if(drawSurf->sort == oldSort)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
			continue;
		}
		oldSort = drawSurf->sort;
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if(shader != oldShader || fogNum != oldFogNum || (entityNum != oldEntityNum && !shader->entityMergable))
		{
			if(oldShader != NULL)
			{
				RB_EndSurface();
			}
			RB_BeginSurface(shader, fogNum);
			oldShader = shader;
			oldFogNum = fogNum;
		}
		
		// Tr3B - skip all translucent surfaces that don't matter for zfill only pass
		if(shader->sort > SS_SEE_THROUGH && shader->isSky == qfalse)
			break;

		// change the modelview matrix if needed
		if(entityNum != oldEntityNum)
		{
			depthRange = qfalse;

			if(entityNum != ENTITYNUM_WORLD)
			{
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}

			qglLoadMatrixf(backEnd.or.modelViewMatrix);

			// change depthrange if needed
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					qglDepthRange(0, 0.3);
				}
				else
				{
					qglDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
	}

	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if(oldShader != NULL)
	{
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
	
	// reset stage iterator
	tess.currentStageIteratorType = SIT_DEFAULT;
}


/*
=================
RB_RenderInteractions
=================
*/
void RB_RenderInteractions(float originalTime, interaction_t * interactions, int numInteractions)
{
	shader_t       *shader, *oldShader;
	trRefEntity_t  *entity, *oldEntity;
	trRefDlight_t  *light, *oldLight;
	interaction_t  *ia;
	qboolean        depthRange, oldDepthRange;
	int             iaCount;
	surfaceType_t  *surface;
	vec3_t          tmp;
	matrix_t		modelToLight;
	
	GLimp_LogComment("--- RB_RenderInteractions ---\n");

	// draw everything
	oldLight = NULL;
	oldEntity = NULL;
	oldShader = NULL;
	oldDepthRange = qfalse;
	depthRange = qfalse;
	
	tess.currentStageIteratorType = SIT_LIGHTING;

	// render interactions
	for(iaCount = 0, ia = &interactions[0]; iaCount < numInteractions;)
	{
		backEnd.currentLight = light = ia->dlight;
		backEnd.currentEntity = entity = ia->entity;
		surface = ia->surface;
		shader = tr.sortedShaders[ia->shaderNum & (MAX_SHADERS - 1)];
		
		if(light != oldLight)
		{
			/*
			if(!dl->additive)
			{
				GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);
			}
			else
			*/
			{
				GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);
			}
			
			backEnd.pc.c_dlights++;
		}
		backEnd.pc.c_dlightInteractions++;
		
		// Tr3B - this should never happen in the first iteration
		if(light == oldLight && entity == oldEntity && shader == oldShader)
		{
			// fast path, same as previous
			rb_surfaceTable[*surface] (surface);
		}
		else
		{
			// draw the contents of the last shader batch
			if(oldEntity != NULL || oldLight != NULL || oldShader != NULL)
			{
				backEnd.pc.c_dlightBatches++;
				RB_EndSurface();
			}
			
			// change the tess parameters if needed
			// a "entityMergable" shader is a shader that can have surfaces from seperate
			// entities merged into a single batch, like smoke and blood puff sprites
			
			// we need a new batch
			RB_BeginSurface(shader, 0);

			// change the modelview matrix if needed
			if(entity != oldEntity)
			{
				depthRange = qfalse;
	
				if(entity != &tr.worldEntity)
				{
					backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
					// we have to reset the shaderTime as well otherwise image animations start
					// from the wrong frame
					tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	
					// set up the transformation matrix
					R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);
	
					if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
					{
						// hack the depth range to prevent view model from poking into walls
						depthRange = qtrue;
					}
				}
				else
				{
					backEnd.refdef.floatTime = originalTime;
					backEnd.or = backEnd.viewParms.world;
					// we have to reset the shaderTime as well otherwise image animations on
					// the world (like water) continue with the wrong frame
					tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
				}
				
				qglLoadMatrixf(backEnd.or.modelViewMatrix);
	
				// change depthrange if needed
				if(oldDepthRange != depthRange)
				{
					if(depthRange)
					{
						qglDepthRange(0, 0.3);
					}
					else
					{
						qglDepthRange(0, 1);
					}
					oldDepthRange = depthRange;
				}
			}
			
			// change the attenuation matrix if needed
			if(light != oldLight || entity != oldEntity)
			{
				// transform light origin into model space for u_LightOrigin parameter
				VectorSubtract(light->l.origin, backEnd.or.origin, tmp);
				light->transformed[0] = DotProduct(tmp, backEnd.or.axis[0]);
				light->transformed[1] = DotProduct(tmp, backEnd.or.axis[1]);
				light->transformed[2] = DotProduct(tmp, backEnd.or.axis[2]);
				
				// finalize the attenuation matrix using the entity transform
				MatrixMultiply(light->viewMatrix, backEnd.or.transformMatrix, modelToLight);
				MatrixMultiply(light->attenuationMatrix, modelToLight, light->attenuationMatrix2);	
			}
			
			// add the triangles for this surface
			rb_surfaceTable[*surface] (surface);
		}
		
		// Tr3B - the data structure used here is not very cool but it works fine
		if(!ia->next)
		{
			if(iaCount < (numInteractions -1))
			{
				// jump to next interaction and continue
				ia++;
				iaCount++;
			}
			else
			{
				// increase last time to leave for loop
				iaCount++;
			}
			
			// draw the contents of the current shader batch
			backEnd.pc.c_dlightBatches++;
			RB_EndSurface();
		}
		else
		{
			// just continue
			ia = ia->next;
			iaCount++;
		}
		
		// remember values
		oldLight = light;
		oldEntity = entity;
		oldShader = shader;
		
		// Tr3B - draw useful info so we can see how bboxes intersect
		if(r_showLightInteractions->integer)
		{
			if(*surface == SF_FACE)
			{
				srfSurfaceFace_t *face;
				
				face = (srfSurfaceFace_t *) surface;
				R_DebugBoundingBox(vec3_origin, face->bounds[0], face->bounds[1], colorYellow);
			}
			else if(*surface == SF_GRID)
			{
				srfGridMesh_t  *grid;
				
				grid = (srfGridMesh_t *) surface;
				R_DebugBoundingBox(vec3_origin, grid->meshBounds[0], grid->meshBounds[1], colorMagenta);
			}
			else if(*surface == SF_TRIANGLES)
			{
				srfTriangles_t *tri;
				
				tri = (srfTriangles_t *) surface;
				R_DebugBoundingBox(vec3_origin, tri->bounds[0], tri->bounds[1], colorCyan);
			}
			else if(*surface == SF_MD3)
			{
				R_DebugBoundingBox(vec3_origin, entity->localBounds[0], entity->localBounds[1], colorMdGrey);
			}
		}
	}

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
	
	// reset stage iterator
	tess.currentStageIteratorType = SIT_DEFAULT;
}


/*
=================
RB_RenderInteractions2
=================
*/
void RB_RenderInteractions2(float originalTime, interaction_t * interactions, int numInteractions)
{
	shader_t       *shader, *oldShader;
	trRefEntity_t  *entity, *oldEntity;
	trRefDlight_t  *light, *oldLight;
	interaction_t  *ia;
	int             iaCount;
	int             iaFirst = 0;
	surfaceType_t  *surface;
	qboolean        depthRange, oldDepthRange;
	vec3_t          tmp;
	matrix_t		modelToLight;
	qboolean		drawShadows;
	
	GLimp_LogComment("--- RB_RenderInteractions2 ---\n");

	// draw everything
	oldLight = NULL;
	oldEntity = NULL;
	oldShader = NULL;
	oldDepthRange = qfalse;
	depthRange = qfalse;
	drawShadows = qtrue;
	
	tess.currentStageIteratorType = SIT_LIGHTING2;
	
	// store current OpenGL state 
	qglPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	qglDisable(GL_ALPHA_TEST);
	qglEnable(GL_CULL_FACE);
	qglEnable(GL_DEPTH_TEST);
	qglEnable(GL_STENCIL_TEST);
	qglEnable(GL_BLEND);

	// render interactions
	for(iaCount = 0, ia = &interactions[0]; iaCount < numInteractions;)
	{
		backEnd.currentLight = light = ia->dlight;
		backEnd.currentEntity = entity = ia->entity;
		surface = ia->surface;
		shader = tr.sortedShaders[ia->shaderNum & (MAX_SHADERS - 1)];
		
		// only iaFirst == iaCount if first iteration or counters were reset
		if(light != oldLight || iaFirst == iaCount)
		{
			iaFirst = iaCount;
			
			if(drawShadows)
			{
				qglClear(GL_STENCIL_BUFFER_BIT);
				
				// don't write to the color buffer or depth buffer
				// enable stencil testing for this light
				qglDepthFunc(GL_LEQUAL);
				qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				qglDepthMask(GL_FALSE);
				
				// set the reference stencil value
				qglStencilFunc(GL_ALWAYS, 0, ~0);
				qglStencilMask(~0);
				
				//qglStencilFunc(GL_ALWAYS, 128, ~0);
				//qglStencilMask(1);
			}
			else
			{
				// Tr3B - see RobustShadowVolumes.pdf by Nvidia
				// Set stencil testing to render only pixels with a zero
				// stencil value, i.e., visible fragments illuminated by the
				// current light. Use equal depth testing to update only the
				// visible fragments, and then, increment stencil to avoid
				// double blending. Re-enable color buffer writes again.
				
				/*
				if(!light->additive)
				{
					qglBlendFunc(GL_DST_COLOR, GL_ONE);
				}
				else
				*/
				{
					qglBlendFunc(GL_ONE, GL_ONE);
				}
				qglDepthFunc(GL_EQUAL);
				
				qglStencilFunc(GL_EQUAL, 0, ~0);
				qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
				
				//qglStencilFunc(GL_EQUAL, 128, ~0);
				//qglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
				
				qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			}
			
			if(light != oldLight)
			{
				backEnd.pc.c_dlights++;
			}
		}
		
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		
		// we need a new batch
		RB_BeginSurface(shader, 0);

		// change the modelview matrix if needed
		if(entity != oldEntity)
		{
			depthRange = qfalse;

			if(entity != &tr.worldEntity)
			{
				backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}
			
			qglLoadMatrixf(backEnd.or.modelViewMatrix);

			// change depthrange if needed
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					qglDepthRange(0, 0.3);
				}
				else
				{
					qglDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}
		}
		
		// change the attenuation matrix if needed
		if(light != oldLight || entity != oldEntity)
		{
			// transform light origin into model space for u_LightOrigin parameter
			if(entity != &tr.worldEntity)
			{
				VectorSubtract(light->l.origin, backEnd.or.origin, tmp);
				light->transformed[0] = DotProduct(tmp, backEnd.or.axis[0]);
				light->transformed[1] = DotProduct(tmp, backEnd.or.axis[1]);
				light->transformed[2] = DotProduct(tmp, backEnd.or.axis[2]);
			}
			else
			{
				VectorCopy(light->l.origin, light->transformed);	
			}
			
			// finalize the attenuation matrix using the entity transform
			MatrixMultiply(light->viewMatrix, backEnd.or.transformMatrix, modelToLight);
			MatrixMultiply(light->attenuationMatrix, modelToLight, light->attenuationMatrix2);
		}
		
		// add the triangles for this surface
		rb_surfaceTable[*surface] (surface);
		
		// draw the contents of the current shader batch
		if(drawShadows)
		{
			if(//(entity != &tr.worldEntity) &&
				!(entity->e.renderfx & (RF_NOSHADOW | RF_DEPTHHACK)) && shader->sort == SS_OPAQUE && !shader->noShadows)
			{
				RB_ShadowTessEnd();
				backEnd.pc.c_shadows++;
			}
		}
		else
		{
			RB_EndSurface();
			backEnd.pc.c_dlightInteractions++;
			backEnd.pc.c_dlightBatches++;
		}
		
		// Tr3B - the data structure used here is not very cool but it works fine
		if(!ia->next)
		{
			if(drawShadows)
			{
				// jump back to first interaction of this light and start lighting
				ia = &interactions[iaFirst];
				iaCount = iaFirst;
				drawShadows = qfalse;
			}
			else
			{
				if(iaCount < (numInteractions -1))
				{
					// jump to next interaction and start shadowing
					ia++;
					iaCount++;
					drawShadows = qtrue;
				}
				else
				{
					// increase last time to leave for loop
					iaCount++;
				}
			}
		}
		else
		{
			// just continue
			ia = ia->next;
			iaCount++;
		}
		
		// remember values
		oldLight = light;
		oldEntity = entity;
		oldShader = shader;
		
	}

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
	
	// restore OpenGL state
	qglDisable(GL_CULL_FACE);
	qglDisable(GL_DEPTH_TEST);
	qglDisable(GL_STENCIL_TEST);
	qglDisable(GL_BLEND);
	qglPopAttrib();
	
	// reset stage iterator
	tess.currentStageIteratorType = SIT_DEFAULT;
}


static void RB_RenderLightScale()
{
	float lightScale;
	
	GLimp_LogComment("--- RB_RenderLightScale ---\n");
	
	lightScale = r_lightScale->value;
	if(lightScale < 1.0 || (backEnd.refdef.rdflags & RDF_NOLIGHTSCALE))
	{
		return;
	}
	
	qglDisable(GL_CLIP_PLANE0);
	qglDisable(GL_CULL_FACE);

	GL_Program(0);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR);
	
	GL_SelectTexture(0);
	GL_Bind(tr.whiteImage);
	qglLoadIdentity();

	// draw fullscreen quads
	qglMatrixMode(GL_PROJECTION);
	qglPushMatrix();
	qglLoadIdentity();

	while(lightScale >= 2.0)
	{
		lightScale *= 0.5;

		qglColor3f(1.0, 1.0, 1.0);

		qglBegin(GL_QUADS);
		qglVertex3f(-1.0, -1.0, -1.0);
		qglVertex3f( 1.0, -1.0, -1.0);
		qglVertex3f( 1.0,  1.0, -1.0);
		qglVertex3f(-1.0,  1.0, -1.0);
		qglEnd();
	}

	if(lightScale > 1.0)
	{
		lightScale *= 0.5;

		qglColor3f(lightScale, lightScale, lightScale);

		qglBegin(GL_QUADS);
		qglVertex3f(-1.0, -1.0, -1.0);
		qglVertex3f( 1.0, -1.0, -1.0);
		qglVertex3f( 1.0,  1.0, -1.0);
		qglVertex3f(-1.0,  1.0, -1.0);
		qglEnd();
	}

	qglMatrixMode(GL_PROJECTION);
	qglPopMatrix();
	qglMatrixMode(GL_MODELVIEW);

	qglColor4f(1, 1, 1, 1);
}

void RB_RenderDrawSurfListFog(float originalTime, drawSurf_t * drawSurfs, int numDrawSurfs)
{
	shader_t       *shader, *oldShader;
	int             fogNum, oldFogNum;
	int             entityNum, oldEntityNum;
	qboolean        depthRange, oldDepthRange;
	int             i;
	drawSurf_t     *drawSurf;
	int             oldSort;
	
	GLimp_LogComment("--- RB_RenderDrawSurfListFog ---\n");

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;
	
	tess.currentStageIteratorType = SIT_FOG;

	for(i = 0, drawSurf = drawSurfs; i < numDrawSurfs; i++, drawSurf++)
	{
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		
		// Tr3B - skip all non fogged surfaces
		if(!fogNum)
			continue;
		
		backEnd.pc.c_fogSurfaces++;
		
		if(drawSurf->sort == oldSort)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
			continue;
		}
		
		oldSort = drawSurf->sort;

		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if(shader != oldShader || fogNum != oldFogNum || (entityNum != oldEntityNum && !shader->entityMergable))
		{
			if(oldShader != NULL)
			{
				backEnd.pc.c_fogBatches++;
				RB_EndSurface();
			}
			RB_BeginSurface(shader, fogNum);
			oldShader = shader;
			oldFogNum = fogNum;
		}

		// change the modelview matrix if needed
		if(entityNum != oldEntityNum)
		{
			depthRange = qfalse;

			if(entityNum != ENTITYNUM_WORLD)
			{
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
				
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
				
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}

			qglLoadMatrixf(backEnd.or.modelViewMatrix);

			// change depthrange if needed
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					qglDepthRange(0, 0.3);
				}
				else
				{
					qglDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
	}

	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if(oldShader != NULL)
	{
		backEnd.pc.c_fogBatches++;
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
	
	// reset stage iterator
	tess.currentStageIteratorType = SIT_DEFAULT;
}

void RB_RenderDebugUtils()
{
	if(r_showLightTransforms->integer)
	{
		trRefDlight_t  *dl;
		int             i;

		dl = backEnd.refdef.dlights;
		for(i = 0; i < backEnd.refdef.numDlights; i++, dl++)
		{
			// set up the transformation matrix
			R_RotateForDlight(dl, &backEnd.viewParms, &backEnd.or);
			qglLoadMatrixf(backEnd.or.modelViewMatrix);
			
			R_DebugAxis(vec3_origin, matrixIdentity);
			R_DebugBoundingBox(vec3_origin, dl->localBounds[0], dl->localBounds[1], colorBlue);
			
			// go back to the world modelview matrix
			backEnd.or = backEnd.viewParms.world;
			qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
			
			R_DebugBoundingBox(vec3_origin, dl->worldBounds[0], dl->worldBounds[1], colorGreen);
		}
	}
	
	if(r_showEntityTransforms->integer)
	{
		trRefEntity_t  *ent;
		int             i;

		ent = backEnd.refdef.entities;
		for(i = 0; i < backEnd.refdef.numEntities; i++, ent++)
		{
			// set up the transformation matrix
			R_RotateForEntity(ent, &backEnd.viewParms, &backEnd.or);
			qglLoadMatrixf(backEnd.or.modelViewMatrix);
			
			R_DebugAxis(vec3_origin, matrixIdentity);
			R_DebugBoundingBox(vec3_origin, ent->localBounds[0], ent->localBounds[1], colorYellow);
			
			// go back to the world modelview matrix
			backEnd.or = backEnd.viewParms.world;
			qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
			
			R_DebugBoundingBox(vec3_origin, ent->worldBounds[0], ent->worldBounds[1], colorRed);
		}
	}
}


void RB_RenderDrawSurfListTranslucent(float originalTime, drawSurf_t * drawSurfs, int numDrawSurfs)
{
	shader_t       *shader, *oldShader;
	int             fogNum, oldFogNum;
	int             entityNum, oldEntityNum;
	qboolean        depthRange, oldDepthRange;
	int             i;
	drawSurf_t     *drawSurf;
	int             oldSort;
	
	GLimp_LogComment("--- RB_RenderDrawSurfListTranslucent ---\n");

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;
	
	tess.currentStageIteratorType = SIT_TRANSLUCENT;

	for(i = 0, drawSurf = drawSurfs; i < numDrawSurfs; i++, drawSurf++)
	{
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		
		// Tr3B - skip all opaque surfaces that don't matter for translucent pass
		if(shader->sort <= SS_SEE_THROUGH || shader->isSky == qtrue)
			continue;
		
		if(drawSurf->sort == oldSort)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
			continue;
		}
		
		oldSort = drawSurf->sort;
		
		
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if(shader != oldShader || fogNum != oldFogNum || (entityNum != oldEntityNum && !shader->entityMergable))
		{
			if(oldShader != NULL)
			{
				RB_EndSurface();
			}
			RB_BeginSurface(shader, fogNum);
			oldShader = shader;
			oldFogNum = fogNum;
		}

		// change the modelview matrix if needed
		if(entityNum != oldEntityNum)
		{
			depthRange = qfalse;

			if(entityNum != ENTITYNUM_WORLD)
			{
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.or);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}

			qglLoadMatrixf(backEnd.or.modelViewMatrix);

			// change depthrange if needed
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					qglDepthRange(0, 0.3);
				}
				else
				{
					qglDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface] (drawSurf->surface);
	}

	backEnd.refdef.floatTime = originalTime;

	// draw the contents of the last shader batch
	if(oldShader != NULL)
	{
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	qglLoadMatrixf(backEnd.viewParms.world.modelViewMatrix);
	if(depthRange)
	{
		qglDepthRange(0, 1);
	}
	
	// reset stage iterator
	tess.currentStageIteratorType = SIT_DEFAULT;
}

/*
==================
RB_RenderDrawSurfList
==================
*/
void RB_RenderDrawSurfList(drawSurf_t * drawSurfs, int numDrawSurfs, interaction_t * interactions, int numInteractions)
{
	float           originalTime;
	
	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get a call to va() every frame!
		GLimp_LogComment(va("--- RB_RenderDrawSurfList( %i, %i ) ---\n", numDrawSurfs, numInteractions));
	}
	
	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;
	
	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();
	
	backEnd.pc.c_surfaces += numDrawSurfs;
	
#if 1
	// Tr3B - an approach that is more oldschool
	
	// draw everything but lighting and fog
	RB_RenderDrawSurfListFull(originalTime, drawSurfs, numDrawSurfs);
	
	if(r_shadows->integer == 3)
	{
		// render dynamic shadowing and lighting using stencil shadow volumes
		RB_RenderInteractions2(originalTime, interactions, numInteractions);
	}
	else
	{
		// render dynamic lighting
		RB_RenderInteractions(originalTime, interactions, numInteractions);
	}
	
	// render light scale hack to brighten up the scene
	RB_RenderLightScale();
	
#else
	// Tr3B - draw everything in a similar order as Doom3 does

	// lay down z buffer
	RB_RenderDrawSurfListZFill(originalTime, drawSurfs, numDrawSurfs);

	// render shadowing and lighting
	RB_RenderInteractions2(originalTime, interactions, numInteractions);

	// render light scale hack to brighten up the scene
	RB_RenderLightScale();

	// draw fog
	//RB_RenderDrawSurfListFog(originalTime, drawSurfs, numDrawSurfs);

	// render translucent surfaces
	//RB_RenderDrawSurfListTranslucent(originalTime, drawSurfs, numDrawSurfs);
#endif

	// render debug information
	RB_RenderDebugUtils();

#if 0
	RB_DrawSun();
#endif

#if 0
	// add light flares on lights that aren't obscured
	RB_RenderFlares();
#endif
}


/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D
================
*/
void RB_SetGL2D(void)
{
	GLimp_LogComment("--- RB_SetGL2D ---\n");
	
	backEnd.projection2D = qtrue;

	// set 2D virtual screen size
	qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	qglMatrixMode(GL_PROJECTION);
	qglLoadIdentity();
	qglOrtho(0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1);
	qglMatrixMode(GL_MODELVIEW);
	qglLoadIdentity();

	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);

	qglDisable(GL_CULL_FACE);
	qglDisable(GL_CLIP_PLANE0);

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw(int x, int y, int w, int h, int cols, int rows, const byte * data, int client, qboolean dirty)
{
	int             i, j;
	int             start, end;

	if(!tr.registered)
	{
		return;
	}
	R_SyncRenderThread();

	// we definately want to sync every frame for the cinematics
	qglFinish();

	start = end = 0;
	if(r_speeds->integer)
	{
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	for(i = 0; (1 << i) < cols; i++)
	{
	}
	for(j = 0; (1 << j) < rows; j++)
	{
	}
	if((1 << i) != cols || (1 << j) != rows)
	{
		ri.Error(ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	GL_SelectTexture(0);
	GL_Bind(tr.scratchImage[client]);

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if(cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height)
	{
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	}
	else
	{
		if(dirty)
		{
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
	}

	if(r_speeds->integer)
	{
		end = ri.Milliseconds();
		ri.Printf(PRINT_ALL, "qglTexSubImage2D %i, %i: %i msec\n", cols, rows, end - start);
	}

	RB_SetGL2D();

	qglColor3f(tr.identityLight, tr.identityLight, tr.identityLight);

	qglBegin(GL_QUADS);
	qglTexCoord2f(0.5f / cols, 0.5f / rows);
	qglVertex2f(x, y);
	qglTexCoord2f((cols - 0.5f) / cols, 0.5f / rows);
	qglVertex2f(x + w, y);
	qglTexCoord2f((cols - 0.5f) / cols, (rows - 0.5f) / rows);
	qglVertex2f(x + w, y + h);
	qglTexCoord2f(0.5f / cols, (rows - 0.5f) / rows);
	qglVertex2f(x, y + h);
	qglEnd();
}

void RE_UploadCinematic(int w, int h, int cols, int rows, const byte * data, int client, qboolean dirty)
{
	GL_Bind(tr.scratchImage[client]);

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if(cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height)
	{
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	}
	else
	{
		if(dirty)
		{
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
	}
}


/*
=============
RB_SetColor
=============
*/
const void     *RB_SetColor(const void *data)
{
	const setColorCommand_t *cmd;
	
	GLimp_LogComment("--- RB_SetColor ---\n");

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0] * 255;
	backEnd.color2D[1] = cmd->color[1] * 255;
	backEnd.color2D[2] = cmd->color[2] * 255;
	backEnd.color2D[3] = cmd->color[3] * 255;

	return (const void *)(cmd + 1);
}

/*
=============
RB_StretchPic
=============
*/
const void     *RB_StretchPic(const void *data)
{
	const stretchPicCommand_t *cmd;
	shader_t       *shader;
	int             numVerts, numIndexes;
	
	GLimp_LogComment("--- RB_StretchPic ---\n");

	cmd = (const stretchPicCommand_t *)data;

	if(!backEnd.projection2D)
	{
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if(shader != tess.shader)
	{
		if(tess.numIndexes)
		{
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface(shader, 0);
	}

	RB_CHECKOVERFLOW(4, 6);
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[numIndexes] = numVerts + 3;
	tess.indexes[numIndexes + 1] = numVerts + 0;
	tess.indexes[numIndexes + 2] = numVerts + 2;
	tess.indexes[numIndexes + 3] = numVerts + 2;
	tess.indexes[numIndexes + 4] = numVerts + 0;
	tess.indexes[numIndexes + 5] = numVerts + 1;

	*(int *)tess.vertexColors[numVerts] =
		*(int *)tess.vertexColors[numVerts + 1] =
		*(int *)tess.vertexColors[numVerts + 2] =
		*(int *)tess.vertexColors[numVerts + 3] = *(int *)backEnd.color2D;

	tess.xyz[numVerts][0] = cmd->x;
	tess.xyz[numVerts][1] = cmd->y;
	tess.xyz[numVerts][2] = 0;

	tess.texCoords[numVerts][0][0] = cmd->s1;
	tess.texCoords[numVerts][0][1] = cmd->t1;

	tess.xyz[numVerts + 1][0] = cmd->x + cmd->w;
	tess.xyz[numVerts + 1][1] = cmd->y;
	tess.xyz[numVerts + 1][2] = 0;

	tess.texCoords[numVerts + 1][0][0] = cmd->s2;
	tess.texCoords[numVerts + 1][0][1] = cmd->t1;

	tess.xyz[numVerts + 2][0] = cmd->x + cmd->w;
	tess.xyz[numVerts + 2][1] = cmd->y + cmd->h;
	tess.xyz[numVerts + 2][2] = 0;

	tess.texCoords[numVerts + 2][0][0] = cmd->s2;
	tess.texCoords[numVerts + 2][0][1] = cmd->t2;

	tess.xyz[numVerts + 3][0] = cmd->x;
	tess.xyz[numVerts + 3][1] = cmd->y + cmd->h;
	tess.xyz[numVerts + 3][2] = 0;

	tess.texCoords[numVerts + 3][0][0] = cmd->s1;
	tess.texCoords[numVerts + 3][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawSurfs
=============
*/
const void     *RB_DrawSurfs(const void *data)
{
	const drawSurfsCommand_t *cmd;
	
	GLimp_LogComment("--- RB_DrawSurfs ---\n");

	// finish any 2D drawing if needed
	if(tess.numIndexes)
	{
		RB_EndSurface();
	}

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	RB_RenderDrawSurfList(cmd->drawSurfs, cmd->numDrawSurfs, cmd->interactions, cmd->numInteractions);

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawBuffer
=============
*/
const void     *RB_DrawBuffer(const void *data)
{
	const drawBufferCommand_t *cmd;
	
	GLimp_LogComment("--- RB_DrawBuffer ---\n");

	cmd = (const drawBufferCommand_t *)data;

	qglDrawBuffer(cmd->buffer);

	// clear screen for debugging
	if(r_clear->integer)
	{
//		qglClearColor(1, 0, 0.5, 1);
		qglClearColor(0, 0, 1, 1);
		qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	return (const void *)(cmd + 1);
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
void RB_ShowImages(void)
{
	int             i;
	image_t        *image;
	float           x, y, w, h;
	int             start, end;
	
	GLimp_LogComment("--- RB_ShowImages ---\n");

	if(!backEnd.projection2D)
	{
		RB_SetGL2D();
	}

	qglClear(GL_COLOR_BUFFER_BIT);

	qglFinish();
	
	GL_SelectTexture(0);

	start = ri.Milliseconds();

	for(i = 0; i < tr.numImages; i++)
	{
		image = tr.images[i];

		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if(r_showImages->integer == 2)
		{
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		GL_Bind(image);
		qglBegin(GL_QUADS);
		qglTexCoord2f(0, 0);
		qglVertex2f(x, y);
		qglTexCoord2f(1, 0);
		qglVertex2f(x + w, y);
		qglTexCoord2f(1, 1);
		qglVertex2f(x + w, y + h);
		qglTexCoord2f(0, 1);
		qglVertex2f(x, y + h);
		qglEnd();
	}

	qglFinish();

	end = ri.Milliseconds();
	ri.Printf(PRINT_ALL, "%i msec to draw all images\n", end - start);

}


/*
=============
RB_SwapBuffers
=============
*/
const void     *RB_SwapBuffers(const void *data)
{
	const swapBuffersCommand_t *cmd;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
	{
		RB_EndSurface();
	}

	// texture swapping test
	if(r_showImages->integer)
	{
		RB_ShowImages();
	}

	cmd = (const swapBuffersCommand_t *)data;

	// we measure overdraw by reading back the stencil buffer and
	// counting up the number of increments that have happened
	if(r_measureOverdraw->integer)
	{
		int             i;
		long            sum = 0;
		unsigned char  *stencilReadback;

		stencilReadback = ri.Hunk_AllocateTempMemory(glConfig.vidWidth * glConfig.vidHeight);
		qglReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX,
					  GL_UNSIGNED_BYTE, stencilReadback);

		for(i = 0; i < glConfig.vidWidth * glConfig.vidHeight; i++)
		{
			sum += stencilReadback[i];
		}

		backEnd.pc.c_overDraw += sum;
		ri.Hunk_FreeTempMemory(stencilReadback);
	}


	if(!glState.finishCalled)
	{
		qglFinish();
	}
	
	GLimp_LogComment("***************** RB_SwapBuffers *****************\n\n\n");

	GLimp_EndFrame();

	backEnd.projection2D = qfalse;

	return (const void *)(cmd + 1);
}

/*
====================
RB_ExecuteRenderCommands

This function will be called synchronously if running without
smp extensions, or asynchronously by another thread.
====================
*/
void RB_ExecuteRenderCommands(const void *data)
{
	int             t1, t2;
	
	GLimp_LogComment("--- RB_ExecuteRenderCommands ---\n");

	t1 = ri.Milliseconds();

	if(!r_smp->integer || data == backEndData[0]->commands.cmds)
	{
		backEnd.smpFrame = 0;
	}
	else
	{
		backEnd.smpFrame = 1;
	}

	while(1)
	{
		switch (*(const int *)data)
		{
			case RC_SET_COLOR:
				data = RB_SetColor(data);
				break;
			case RC_STRETCH_PIC:
				data = RB_StretchPic(data);
				break;
			case RC_DRAW_SURFS:
				data = RB_DrawSurfs(data);
				break;
			case RC_DRAW_BUFFER:
				data = RB_DrawBuffer(data);
				break;
			case RC_SWAP_BUFFERS:
				data = RB_SwapBuffers(data);
				break;
			case RC_SCREENSHOT:
				data = RB_TakeScreenshotCmd(data);
				break;

			case RC_END_OF_LIST:
			default:
				// stop rendering on this thread
				t2 = ri.Milliseconds();
				backEnd.pc.msec = t2 - t1;
				return;
		}
	}
}


/*
================
RB_RenderThread
================
*/
void RB_RenderThread(void)
{
	const void     *data;

	// wait for either a rendering command or a quit command
	while(1)
	{
		// sleep until we have work to do
		data = GLimp_RendererSleep();

		if(!data)
		{
			return;				// all done, renderer is shutting down
		}

		renderThreadActive = qtrue;

		RB_ExecuteRenderCommands(data);

		renderThreadActive = qfalse;
	}
}
