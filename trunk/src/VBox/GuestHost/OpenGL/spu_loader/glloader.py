# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.


import sys
import apiutil


keys = apiutil.GetDispatchedFunctions(sys.argv[1]+"/APIspec.txt")

apiutil.CopyrightC()

print """
/* DO NOT EDIT - THIS FILE GENERATED BY THE glloader.py SCRIPT */
#include "cr_error.h"
#include "cr_dll.h"
#include "cr_spu.h"
#include "cr_string.h"
#include "cr_error.h"
#include "cr_environment.h"

#include <stdio.h>
#if defined(WINDOWS)
#include <windows.h>
#include <process.h>
#include <direct.h>
#define SYSTEM_GL "opengl32.dll"
#elif defined (DARWIN)
#define SYSTEM_GL "libGL.dylib"
#define SYSTEM_CGL "OpenGL"
#define SYSTEM_AGL "AGL"
#elif defined(IRIX) || defined(IRIX64) || defined(Linux) || defined(FreeBSD) || defined(AIX) || defined(SunOS) || defined(OSF1)
#if defined(Linux)
#include <string.h>
#endif
#if defined(AIX)
#define SYSTEM_GL "libGL.o"
#else
#define SYSTEM_GL "libGL.so.1"
#endif
typedef void (*glxfuncptr)();
extern glxfuncptr glxGetProcAddressARB( const GLubyte *name );
#else
#error I don't know where your system's GL lives.  Too bad.
#endif

static CRDLL *glDll = NULL;

#ifdef DARWIN
#define SYSTEM_GL_LIB_DIR   "/System/Library/Frameworks/OpenGL.framework/Libraries"
#define SYSTEM_CGL_DIR  "/System/Library/Frameworks/OpenGL.framework"
#define SYSTEM_AGL_DIR  "/System/Library/Frameworks/AGL.framework"

static CRDLL *cglDll = NULL;
static CRDLL *aglDll = NULL;
#endif

#if defined(WINDOWS)
#define GLLOADER_APIENTRY __stdcall
#else
#define GLLOADER_APIENTRY
#endif

#define CR_NO_GL_SYSTEM_PATH 1

/*
 * Add an entry to the SPUNamedFunctionTable
 */
static int
fillin( SPUNamedFunctionTable *entry, const char *funcName, SPUGenericFunction funcPtr )
{
	if (funcPtr) {
		entry->name = crStrdup( funcName );
		entry->fn = funcPtr;
		return 1;
	}
	return 0;
}

#ifndef WINDOWS
static int FileExists(char *directory, char *filename)
{
	FILE *f;
	char fullFilename[8096];

	crStrcpy(fullFilename, directory);
	crStrcat(fullFilename, "/");
	crStrcat(fullFilename, filename);

	f = fopen(fullFilename, "r");
	if (f) {
		fclose(f);
		return 1;
	}
	else {
	    return 0;
	}
}
#endif


/*
 * Locate the native OpenGL library, open it and return shared library
 * handle.
 */
static CRDLL *
__findSystemLib( const char *provided_system_path, char *lib )
{
	CRDLL *dll;
	char system_path[8096];

	memset(system_path, 0, sizeof(system_path));

	if (provided_system_path && (crStrlen(provided_system_path) > 0) )
	{
		crStrcpy( system_path, provided_system_path );
	}
	else
	{
#if defined(WINDOWS)
		GetSystemDirectory(system_path, MAX_PATH);
#elif defined(IRIX) || defined(IRIX64)
#ifdef IRIX_64BIT
		crStrcpy( system_path, "/usr/lib64" );
#else
		crStrcpy( system_path, "/usr/lib32" );
#endif
#elif defined(PLAYSTATION2)
		crStrcpy( system_path, "/usr/X11R6/lib" );
#else
		/* On RedHat 9, the correct default system directory
		 * is /usr/lib/tls/ (and if /usr/lib/ is used,
		 * the dynamic loader will generate a floating point
		 * exception SIGFPE).  On other systems, including
		 * earlier versions of RedHat, the OpenGL library
		 * lives in /usr/lib.   We'll use the /usr/lib/tls/
		 * version if it exists; otherwise, we'll use /usr/lib.
		 */
		/*crStrcpy(system_path, "/usr/lib");*/
#if defined(__linux__) && defined(__amd64__)
		/*if (sizeof(void *) == 8 && FileExists("/usr/lib64", lib)) {
			crStrcat(system_path, "64");
		}*/
#endif
		/*if (FileExists("/usr/lib/tls", lib) ||
		    FileExists("/usr/lib64/tls", lib)) {
			crStrcat(system_path, "/tls");
		}*/
#endif
	}
#if !defined(__linux__) && !defined(SunOS) && !defined(__FreeBSD__)
	crStrcat( system_path, "/" );
#endif
#if !defined(CR_NO_GL_SYSTEM_PATH)
	crStrcat( system_path, lib );
	dll = crDLLOpen(system_path, 1 /*resolveGlobal*/);
#else
	dll = crDLLOpen(lib, 1 /*resolveGlobal*/);
#endif
	return dll;
}


static CRDLL *
#ifdef DARWIN
__findSystemGL( const char *provided_system_path, const char *default_system_path, char *provided_lib_name )
#else
__findSystemGL( const char *provided_system_path )
#endif
{
#ifdef DARWIN
	const char *the_path = (provided_system_path && crStrlen(provided_system_path) > 0) ? provided_system_path : default_system_path;

	/* Fallback for loading frameworks */
	if( !provided_lib_name )
		return crDLLOpen( the_path, 1 );
	else
		return __findSystemLib( the_path, provided_lib_name );
#else
	return __findSystemLib( provided_system_path, SYSTEM_GL );
#endif
}

static SPUGenericFunction
findExtFunction( const crOpenGLInterface *interface, const char *funcName )
{
#ifdef WINDOWS
	if (interface->wglGetProcAddress)
		return (SPUGenericFunction) interface->wglGetProcAddress( funcName );
	else
		return (SPUGenericFunction) NULL;
#else
	/* XXX for some reason, the NVIDIA glXGetProcAddressARB() function
	 * returns pointers that cause Chromium to crash.  If we use the
	 * pointer returned by crDLLGetNoError() instead, we're OK.
	 */
	SPUGenericFunction f = crDLLGetNoError(glDll, funcName);
	if (f)
		return f;
#if !defined(DARWIN)
	else if (interface->glXGetProcAddressARB)
		return interface->glXGetProcAddressARB( (const GLubyte *) funcName );
#endif
	else
		return NULL;
#endif
}
"""


def IsExtensionFunc(func_name):
	"""Determine if the named function is a core function, or extension."""
	cat = apiutil.Category(func_name)
	if cat == "1.0" or cat == "1.1" or cat == "1.2" or cat == "1.3":
		return 0
	else:
		return 1

#
# Generate a no-op function.
#
def GenerateNop(func_name):
	return_type = apiutil.ReturnType(func_name);
	params = apiutil.Parameters(func_name)
	print 'static %s GLLOADER_APIENTRY Nop%s(%s)' % (return_type, func_name, apiutil.MakeDeclarationString(params))
	print '{'
	for (name, type, vecSize) in params:
		if name != "":
			print '\t(void) %s;' % name
	if apiutil.ReturnType(func_name) != 'void':
		print '\treturn 0;'
	print '}'
	print ''



#
# Make no-op funcs for all OpenGL extension functions
#
for func_name in keys:
	if IsExtensionFunc(func_name):
		GenerateNop(func_name)


#
# Generate the crLoadOpenGL() function
#
print """
void
crUnloadOpenGL( void )
{
	crDLLClose( glDll );
	glDll = NULL;

#ifdef DARWIN
	crDLLClose( cglDll );
	cglDll = NULL;

	crDLLClose( aglDll );
	aglDll = NULL;
#endif
}

/*
 * Initialize the 'interface' structure with the WGL or GLX window system
 * interface functions.
 * Then, fill in the table with (name, pointer) pairs for all the core
 * OpenGL entrypoint functions.  But only if table is not NULL
 * Return: number of entries placed in table[], or 0 if error.
 */
int
crLoadOpenGL( crOpenGLInterface *interface, SPUNamedFunctionTable table[] )
{
	static const char *coreFunctions[] = {
"""

for func_name in keys:
	if not IsExtensionFunc(func_name):
		print '\t\t"gl%s",' % func_name

print """
		NULL
	};
	SPUNamedFunctionTable *entry = table;
	int i;

	const char *env_syspath = crGetenv( "CR_SYSTEM_GL_PATH" );
#ifdef DARWIN
	const char *env_cgl_syspath = crGetenv( "CR_SYSTEM_CGL_PATH" );
	const char *env_agl_syspath = crGetenv( "CR_SYSTEM_AGL_PATH" );
#endif
	
	crDebug( "Looking for the system's OpenGL library..." );
#ifdef DARWIN
	glDll = __findSystemGL( env_syspath, SYSTEM_GL_LIB_DIR, SYSTEM_GL );
#else
	glDll = __findSystemGL( env_syspath );
#endif
	if (!glDll)
	{
		crError("Unable to find system OpenGL!");
		return 0;
	}
	
	crDebug( "Found it in %s.", !env_syspath ? "default path" : env_syspath );

#ifdef DARWIN
	crDebug( "Looking for the system's CGL library..." );
	cglDll = __findSystemGL( env_cgl_syspath, SYSTEM_CGL_DIR, SYSTEM_CGL );
	if (!cglDll)
	{
		crError("Unable to find system CGL!");
		return 0;
	}

	crDebug( "Found it in %s.", !env_cgl_syspath ? "default path" : env_cgl_syspath );

	crDebug( "Looking for the system's AGL library..." );
	aglDll = __findSystemGL( env_agl_syspath, SYSTEM_AGL_DIR, SYSTEM_AGL );
	if (!aglDll)
	{
		crError("Unable to find system AGL!");
		return 0;
	}

	crDebug( "Found it in %s.", !env_agl_syspath ? "default path" : env_agl_syspath );
#endif
"""

useful_wgl_functions = [
	"wglGetProcAddress",
	"wglMakeCurrent",
	"wglSwapBuffers",
	"wglCreateContext",
	"wglDeleteContext",
	"wglGetCurrentContext",
	"wglChoosePixelFormat",
	"wglDescribePixelFormat",
	"wglSetPixelFormat",
	"wglChoosePixelFormatEXT",
	"wglGetPixelFormatAttribivEXT",
	"wglGetPixelFormatAttribfvEXT",
	"glGetString"
]
useful_agl_functions = [
	"aglCreateContext",
	"aglDestroyContext",
	"aglSetCurrentContext",
	"aglSwapBuffers",
	"aglChoosePixelFormat",
	"aglDestroyPixelFormat",
	"aglDescribePixelFormat",
	"aglGetCurrentContext",
	"aglSetDrawable",
	"aglGetDrawable",
	"aglSetFullScreen",
	"aglUpdateContext",
	"aglUseFont",
	"aglSetInteger",
	"aglGetInteger",
	"aglGetError",
	"aglEnable",
	"aglDisable"
]
in_gl_functions = [
	"CGLGetCurrentContext",
	"CGLSetCurrentContext"
]
useful_cgl_functions = [
	"CGLChoosePixelFormat",
	"CGLDestroyPixelFormat",
	"CGLDescribePixelFormat",
	"CGLQueryRendererInfo",
	"CGLDestroyRendererInfo",
	"CGLDescribeRenderer",
	"CGLCreateContext",
	"CGLDestroyContext",
	"CGLCopyContext",
	"CGLCreatePBuffer",
	"CGLDestroyPBuffer",
	"CGLDescribePBuffer",
	"CGLTexImagePBuffer",
	"CGLSetOffScreen",
	"CGLGetOffScreen",
	"CGLSetFullScreen",
	"CGLSetPBuffer",
	"CGLGetPBuffer",
	"CGLClearDrawable",
	"CGLFlushDrawable",
	"CGLEnable",
	"CGLDisable",
	"CGLIsEnabled",
	"CGLSetParameter",
	"CGLGetParameter",
	"CGLSetVirtualScreen",
	"CGLGetVirtualScreen",
	"CGLSetOption",
	"CGLGetOption",
	"CGLGetVersion",
	"CGLErrorString",
	"CGLSetSurface",
	"CGLGetSurface",
	"CGLUpdateContext",
	"glGetString"
]
useful_glx_functions = [
	"glXGetConfig",
	"glXQueryExtension",
	"glXQueryVersion",
	"glXQueryExtensionsString",
	"glXChooseVisual",
	"glXCreateContext",
	"glXDestroyContext",
	"glXUseXFont",
	"glXIsDirect",
	"glXMakeCurrent",
	"glGetString",
	"glXSwapBuffers",
	"glXGetCurrentDisplay",
	"glXGetCurrentContext",
	"glXGetClientString",
	"glXWaitGL",
	"glXWaitX",
	"glXCopyContext"
]
possibly_useful_glx_functions = [
	"glXGetProcAddressARB",
	"glXJoinSwapGroupNV",
	"glXBindSwapBarrierNV",
	"glXQuerySwapGroupNV",
	"glXQueryMaxSwapGroupsNV",
	"glXQueryFrameCountNV",
	"glXResetFrameCountNV",
	"glXChooseFBConfig",
	"glXGetFBConfigs",
	"glXGetFBConfigAttrib",
	"glXGetVisualFromFBConfig",
	"glXCreateNewContext",
	"glXCreatePbuffer",
	"glXDestroyPbuffer",
	"glXQueryContext",
	"glXQueryDrawable",
	"glXMakeContextCurrent",
    "glXCreateWindow",
    "glXGetVisualFromFBConfig",
]

print '#ifdef WINDOWS'

for fun in useful_wgl_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( glDll, "%s" );' % (fun,fun,fun)

print '#elif defined(DARWIN)'
for fun in useful_agl_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( aglDll, "%s" );' % (fun,fun,fun)

for fun in useful_cgl_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( cglDll, "%s" );' % (fun, fun,fun)

for fun in in_gl_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( glDll, "%s" );' % (fun, fun,fun)

print '#else'
print '\t/* GLX */'

# XXX merge these loops?
for fun in useful_glx_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( glDll, "%s" );' % (fun, fun, fun)
for fun in possibly_useful_glx_functions:
	print '\tinterface->%s = (%sFunc_t) crDLLGetNoError( glDll, "%s" );' % (fun, fun, fun)
print '#endif'

print """
	if (!entry)
		return 1; /* token value */
		
	for (i = 0; coreFunctions[i]; i++) {
		const char *name = coreFunctions[i];
		if (fillin(entry, name + 2, crDLLGetNoError(glDll, name)))
			entry++;
        else
            crDebug("glLoader: NULL function %s", name);
	}

	/* end of table markers */
	entry->name = NULL;
	entry->fn = NULL;
	return entry - table;  /* number of entries filled */
}


/*
 * Fill in table[] with all the OpenGL extension functions that we're
 * interested in.
 */
int
crLoadOpenGLExtensions( const crOpenGLInterface *interface, SPUNamedFunctionTable table[] )
{
	struct extfunc {
		const char *funcName;
		const char *aliasName;
		SPUGenericFunction nopFunction;
	};
	static const struct extfunc functions[] = {
"""

for func_name in keys:
	if IsExtensionFunc(func_name):
		if apiutil.Category(func_name) == "Chromium":
			prefix = "cr"
		else:
			prefix = "gl"
		s = '\t\t{ "' + prefix + func_name + '", '
		a = apiutil.ReverseAlias(func_name)
		if a:
			s += '"' + prefix + a + '", '
		else:
			s += 'NULL, '
		s += '(SPUGenericFunction) Nop' + func_name + ' },'
		print s

print """
		{ NULL, NULL, NULL}
	};
	const struct extfunc *func;
	SPUNamedFunctionTable *entry = table;

#ifdef WINDOWS
	if (interface->wglGetProcAddress == NULL)
		crWarning("Unable to find wglGetProcAddress() in system GL library");
#elif !defined(DARWIN)
	if (interface->glXGetProcAddressARB == NULL)
		crWarning("Unable to find glXGetProcAddressARB() in system GL library");
#endif

	for (func = functions; func->funcName; func++) {
		SPUGenericFunction f = findExtFunction(interface, func->funcName);
		if (!f && func->aliasName) {
			f = findExtFunction(interface, func->aliasName);
		}
		if (!f) {
			f = func->nopFunction;
		}
		(void) fillin(entry, func->funcName + 2 , f);  /* +2 to skip "gl" */
		entry++;
	}

	/* end of list */
	entry->name = NULL;
	entry->fn = NULL;
	return entry - table;  /* number of entries filled */
}
"""


print """

#ifdef USE_OSMESA
int crLoadOSMesa( OSMesaContext (**createContext)( GLenum format, OSMesaContext sharelist ), 
		     GLboolean (**makeCurrent)( OSMesaContext ctx, GLubyte *buffer, 
						GLenum type, GLsizei width, GLsizei height ),
		     void (**destroyContext)( OSMesaContext ctx ))
{
	static CRDLL *osMesaDll = NULL;

	const char *env_syspath = crGetenv( "CR_SYSTEM_GL_PATH" );
	
	crDebug( "Looking for the system's OSMesa library..." );
	osMesaDll = __findSystemLib( env_syspath, "libOSMesa.so" );
	if (!osMesaDll)
	{
		crError("Unable to find system OSMesa!");
		return 0;
	}

	crDebug( "Found it in %s.", !env_syspath ? "default path" : env_syspath );

	*createContext =  (OSMesaContext (*) ( GLenum format, OSMesaContext sharelist ))
		crDLLGetNoError( osMesaDll, "OSMesaCreateContext" );

	*makeCurrent =  (GLboolean (*) ( OSMesaContext ctx, GLubyte *buffer, 
					  GLenum type, GLsizei width, GLsizei height ))
		crDLLGetNoError( osMesaDll, "OSMesaMakeCurrent" );

	*destroyContext =  (void (*) ( OSMesaContext ctx))
		crDLLGetNoError( osMesaDll, "OSMesaDestroyContext" );

	return 1;
}
#endif 

"""

