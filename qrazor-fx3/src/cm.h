/// ============================================================================
/*
Copyright (C) 1997-2001 Id Software, Inc.
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
#ifndef CM_H
#define CM_H


/// includes ===================================================================
// system -------------------------------------------------------------------
// qrazor-fx ----------------------------------------------------------------
#include "files.h"
// xreal --------------------------------------------------------------------
// shared -------------------------------------------------------------------
#include "x_shared.h"



//
// public interface
//
void			CM_BeginRegistration(const std::string &name, bool clientload, unsigned *checksum);
cmodel_c*		CM_RegisterModel(const std::string &name);
cskel_animation_c*	CM_RegisterAnimation(const std::string &name);
void			CM_EndRegistration();

cmodel_c*		CM_GetModelByName(const std::string &name);
cmodel_c*		CM_GetModelByNum(int num);

cskel_animation_c*	CM_GetAnimationByName(const std::string &name);
cskel_animation_c*	CM_GetAnimationByNum(int num);

int		CM_NumModels();

const char*	CM_EntityString();

// returns an ORed contents mask
int		CM_PointContents(const vec3_c &p, int headnode);
int		CM_TransformedPointContents(const vec3_c &p, int headnode, const vec3_c &origin, const quaternion_c &quat);

int		CM_PointAreanum(const vec3_c &p);

// call with topnode set to the headnode, returns with topnode
// set to the first node that splits the box
int		CM_BoxLeafnums(const cbbox_c &bbox, int *list, int listsize, int *topnode);

int		CM_GetClosestAreaPortal(const vec3_c &p);
bool		CM_GetAreaPortalState(int portal);
void		CM_SetAreaPortalState(int portal, bool open);
bool		CM_AreasConnected(int area1, int area2);

int		CM_WriteAreaBits(byte *buffer, int area);
void		CM_MergeAreaBits(byte *buffer, int area);

bool		CM_HeadnodeVisible(int headnode, byte *visbits);

#endif	// CM_H


