/*
Copyright (c) 2001, Loki software, inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list 
of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

Neither the name of Loki software nor the names of its contributors may be used 
to endorse or promote products derived from this software without specific prior 
written permission. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY 
DIRECT,INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
*/

#if !defined(INCLUDED_GTKDLGS_H)
#define INCLUDED_GTKDLGS_H

#include "qerplugin.h"
#include "string/string.h"

EMessageBoxReturn DoLightIntensityDlg (int *intensity);
EMessageBoxReturn DoShaderTagDlg (CopiedString *tag, char* title);
EMessageBoxReturn DoShaderInfoDlg (const char* name, const char* filename, char* title);
EMessageBoxReturn DoTextureLayout (float *fx, float *fy);
void DoTextEditor (const char* filename, int cursorpos);

void DoProjectSettings();

void DoFind();
void DoSides(int type, int axis);
void DoAbout();


#ifdef WIN32
extern bool g_TextEditor_useWin32Editor;
#else
#include "string/stringfwd.h"
extern bool g_TextEditor_useCustomEditor;
extern CopiedString g_TextEditor_editorCommand;
#endif


#endif
