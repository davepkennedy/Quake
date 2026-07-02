#pragma once
// Modern OpenGL function pointers and constants (GL 1.3 – 4.5).
// All functions are loaded at startup by GL_LoadExtensions() (gl_ext.cpp).
// Uses qgl prefix to distinguish loaded pointers from any future SDK prototypes.

#include <stddef.h>   // ptrdiff_t

// Types missing from the Windows SDK GL 1.1 header
typedef char            GLchar;
typedef ptrdiff_t       GLsizeiptr;
typedef ptrdiff_t       GLintptr;

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------

// GL 1.3 – multitexture
#ifndef GL_TEXTURE0
#define GL_TEXTURE0   0x84C0
#define GL_TEXTURE1   0x84C1
#define GL_TEXTURE2   0x84C2
#endif

// GL_EXT_texture_filter_anisotropic (core in GL 4.6, widely available as EXT)
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// GL 1.5 – buffer objects
#define GL_ARRAY_BUFFER           0x8892
#define GL_ELEMENT_ARRAY_BUFFER   0x8893
#define GL_STATIC_DRAW            0x88B4
#define GL_DYNAMIC_DRAW           0x88E8
#define GL_STREAM_DRAW            0x88E0

// GL 2.0 – shaders
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_INFO_LOG_LENGTH        0x8B84

// -------------------------------------------------------------------------
// Function pointer typedefs
// -------------------------------------------------------------------------

// GL 1.3 multitexture
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);

// GL 1.5 buffer objects
typedef void    (APIENTRY *PFNGLGENBUFFERSPROC)   (GLsizei n, GLuint *buffers);
typedef void    (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void    (APIENTRY *PFNGLBINDBUFFERPROC)   (GLenum target, GLuint buffer);
typedef void    (APIENTRY *PFNGLBUFFERDATAPROC)   (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void    (APIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);

// GL 2.0 vertex attributes
typedef void    (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)    (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void    (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void    (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);

// GL 2.0 shaders
typedef GLuint  (APIENTRY *PFNGLCREATESHADERPROC)     (GLenum type);
typedef void    (APIENTRY *PFNGLSHADERSOURCEPROC)     (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void    (APIENTRY *PFNGLCOMPILESHADERPROC)    (GLuint shader);
typedef void    (APIENTRY *PFNGLGETSHADERIVPROC)      (GLuint shader, GLenum pname, GLint *params);
typedef void    (APIENTRY *PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void    (APIENTRY *PFNGLDELETESHADERPROC)     (GLuint shader);
typedef GLuint  (APIENTRY *PFNGLCREATEPROGRAMPROC)    (void);
typedef void    (APIENTRY *PFNGLATTACHSHADERPROC)     (GLuint program, GLuint shader);
typedef void    (APIENTRY *PFNGLLINKPROGRAMPROC)      (GLuint program);
typedef void    (APIENTRY *PFNGLGETPROGRAMIVPROC)     (GLuint program, GLenum pname, GLint *params);
typedef void    (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void    (APIENTRY *PFNGLUSEPROGRAMPROC)       (GLuint program);
typedef void    (APIENTRY *PFNGLDELETEPROGRAMPROC)    (GLuint program);

// GL 2.0 uniforms
typedef GLint   (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void    (APIENTRY *PFNGLUNIFORM1IPROC)         (GLint location, GLint v0);
typedef void    (APIENTRY *PFNGLUNIFORM1FPROC)         (GLint location, GLfloat v0);
typedef void    (APIENTRY *PFNGLUNIFORM2FVPROC)        (GLint location, GLsizei count, const GLfloat *value);
typedef void    (APIENTRY *PFNGLUNIFORM3FVPROC)        (GLint location, GLsizei count, const GLfloat *value);
typedef void    (APIENTRY *PFNGLUNIFORM4FVPROC)        (GLint location, GLsizei count, const GLfloat *value);
typedef void    (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)  (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

// GL 3.0 vertex array objects
typedef void    (APIENTRY *PFNGLGENVERTEXARRAYSPROC)   (GLsizei n, GLuint *arrays);
typedef void    (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void    (APIENTRY *PFNGLBINDVERTEXARRAYPROC)   (GLuint array);

// GL 3.0 – generate mipmaps
typedef void    (APIENTRY *PFNGLGENERATEMIPMAPPROC)    (GLenum target);

// GL 3.3 – sampler objects
typedef void    (APIENTRY *PFNGLGENSAMPLERSPROC)        (GLsizei count, GLuint *samplers);
typedef void    (APIENTRY *PFNGLDELETESAMPLERSPROC)     (GLsizei count, const GLuint *samplers);
typedef void    (APIENTRY *PFNGLBINDSAMPLERPROC)        (GLuint unit, GLuint sampler);
typedef void    (APIENTRY *PFNGLSAMPLERPARAMETERIPROC)  (GLuint sampler, GLenum pname, GLint param);

// -------------------------------------------------------------------------
// Global function pointer declarations  (defined in gl_ext.cpp)
// -------------------------------------------------------------------------

extern PFNGLACTIVETEXTUREPROC           qglActiveTexture;

extern PFNGLGENBUFFERSPROC              qglGenBuffers;
extern PFNGLDELETEBUFFERSPROC           qglDeleteBuffers;
extern PFNGLBINDBUFFERPROC              qglBindBuffer;
extern PFNGLBUFFERDATAPROC              qglBufferData;
extern PFNGLBUFFERSUBDATAPROC           qglBufferSubData;

extern PFNGLVERTEXATTRIBPOINTERPROC     qglVertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC qglDisableVertexAttribArray;

extern PFNGLCREATESHADERPROC            qglCreateShader;
extern PFNGLSHADERSOURCEPROC            qglShaderSource;
extern PFNGLCOMPILESHADERPROC           qglCompileShader;
extern PFNGLGETSHADERIVPROC             qglGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC        qglGetShaderInfoLog;
extern PFNGLDELETESHADERPROC            qglDeleteShader;
extern PFNGLCREATEPROGRAMPROC           qglCreateProgram;
extern PFNGLATTACHSHADERPROC            qglAttachShader;
extern PFNGLLINKPROGRAMPROC             qglLinkProgram;
extern PFNGLGETPROGRAMIVPROC            qglGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC       qglGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC              qglUseProgram;
extern PFNGLDELETEPROGRAMPROC           qglDeleteProgram;

extern PFNGLGETUNIFORMLOCATIONPROC      qglGetUniformLocation;
extern PFNGLUNIFORM1IPROC               qglUniform1i;
extern PFNGLUNIFORM1FPROC               qglUniform1f;
extern PFNGLUNIFORM2FVPROC              qglUniform2fv;
extern PFNGLUNIFORM3FVPROC              qglUniform3fv;
extern PFNGLUNIFORM4FVPROC              qglUniform4fv;
extern PFNGLUNIFORMMATRIX4FVPROC        qglUniformMatrix4fv;

extern PFNGLGENVERTEXARRAYSPROC         qglGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC      qglDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC         qglBindVertexArray;

extern PFNGLGENERATEMIPMAPPROC          qglGenerateMipmap;

extern PFNGLGENSAMPLERSPROC             qglGenSamplers;
extern PFNGLDELETESAMPLERSPROC          qglDeleteSamplers;
extern PFNGLBINDSAMPLERPROC             qglBindSampler;
extern PFNGLSAMPLERPARAMETERIPROC       qglSamplerParameteri;

// Max anisotropy queried at init; 1.0 means extension unavailable
extern float gl_max_anisotropy;

// Call once after the GL context is created (inside GL_Init)
void GL_LoadExtensions(void);
