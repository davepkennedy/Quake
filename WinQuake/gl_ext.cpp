#include "quakedef.h"

// -------------------------------------------------------------------------
// Global function pointer definitions
// -------------------------------------------------------------------------

PFNGLACTIVETEXTUREPROC qglActiveTexture = nullptr;

PFNGLGENBUFFERSPROC qglGenBuffers = nullptr;
PFNGLDELETEBUFFERSPROC qglDeleteBuffers = nullptr;
PFNGLBINDBUFFERPROC qglBindBuffer = nullptr;
PFNGLBUFFERDATAPROC qglBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC qglBufferSubData = nullptr;

PFNGLVERTEXATTRIBPOINTERPROC qglVertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC qglDisableVertexAttribArray = nullptr;

PFNGLCREATESHADERPROC qglCreateShader = nullptr;
PFNGLSHADERSOURCEPROC qglShaderSource = nullptr;
PFNGLCOMPILESHADERPROC qglCompileShader = nullptr;
PFNGLGETSHADERIVPROC qglGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC qglGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC qglDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC qglCreateProgram = nullptr;
PFNGLATTACHSHADERPROC qglAttachShader = nullptr;
PFNGLLINKPROGRAMPROC qglLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC qglGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC qglGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC qglUseProgram = nullptr;
PFNGLDELETEPROGRAMPROC qglDeleteProgram = nullptr;

PFNGLGETUNIFORMLOCATIONPROC qglGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC qglUniform1i = nullptr;
PFNGLUNIFORM1FPROC qglUniform1f = nullptr;
PFNGLUNIFORM2FVPROC qglUniform2fv = nullptr;
PFNGLUNIFORM3FVPROC qglUniform3fv = nullptr;
PFNGLUNIFORM4FVPROC qglUniform4fv = nullptr;
PFNGLUNIFORMMATRIX4FVPROC qglUniformMatrix4fv = nullptr;

PFNGLGENVERTEXARRAYSPROC qglGenVertexArrays = nullptr;
PFNGLDELETEVERTEXARRAYSPROC qglDeleteVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC qglBindVertexArray = nullptr;

PFNGLGENERATEMIPMAPPROC qglGenerateMipmap = nullptr;

PFNGLGENSAMPLERSPROC qglGenSamplers = nullptr;
PFNGLDELETESAMPLERSPROC qglDeleteSamplers = nullptr;
PFNGLBINDSAMPLERPROC qglBindSampler = nullptr;
PFNGLSAMPLERPARAMETERIPROC qglSamplerParameteri = nullptr;

float gl_max_anisotropy = 1.0f;

// -------------------------------------------------------------------------

static void *LoadProc(const char *name, bool required)
{
    void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
    if (!p && required)
    {
        Sys_Error("GL_LoadExtensions: required function %s not found", name);
    }
    return p;
}

void GL_LoadExtensions(void)
{
    qglActiveTexture = (PFNGLACTIVETEXTUREPROC)LoadProc("glActiveTexture", true);

    qglGenBuffers = (PFNGLGENBUFFERSPROC)LoadProc("glGenBuffers", true);
    qglDeleteBuffers = (PFNGLDELETEBUFFERSPROC)LoadProc("glDeleteBuffers", true);
    qglBindBuffer = (PFNGLBINDBUFFERPROC)LoadProc("glBindBuffer", true);
    qglBufferData = (PFNGLBUFFERDATAPROC)LoadProc("glBufferData", true);
    qglBufferSubData = (PFNGLBUFFERSUBDATAPROC)LoadProc("glBufferSubData", true);

    qglVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)LoadProc("glVertexAttribPointer", true);
    qglEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)LoadProc("glEnableVertexAttribArray", true);
    qglDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)LoadProc("glDisableVertexAttribArray", true);

    qglCreateShader = (PFNGLCREATESHADERPROC)LoadProc("glCreateShader", true);
    qglShaderSource = (PFNGLSHADERSOURCEPROC)LoadProc("glShaderSource", true);
    qglCompileShader = (PFNGLCOMPILESHADERPROC)LoadProc("glCompileShader", true);
    qglGetShaderiv = (PFNGLGETSHADERIVPROC)LoadProc("glGetShaderiv", true);
    qglGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)LoadProc("glGetShaderInfoLog", true);
    qglDeleteShader = (PFNGLDELETESHADERPROC)LoadProc("glDeleteShader", true);
    qglCreateProgram = (PFNGLCREATEPROGRAMPROC)LoadProc("glCreateProgram", true);
    qglAttachShader = (PFNGLATTACHSHADERPROC)LoadProc("glAttachShader", true);
    qglLinkProgram = (PFNGLLINKPROGRAMPROC)LoadProc("glLinkProgram", true);
    qglGetProgramiv = (PFNGLGETPROGRAMIVPROC)LoadProc("glGetProgramiv", true);
    qglGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)LoadProc("glGetProgramInfoLog", true);
    qglUseProgram = (PFNGLUSEPROGRAMPROC)LoadProc("glUseProgram", true);
    qglDeleteProgram = (PFNGLDELETEPROGRAMPROC)LoadProc("glDeleteProgram", true);

    qglGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)LoadProc("glGetUniformLocation", true);
    qglUniform1i = (PFNGLUNIFORM1IPROC)LoadProc("glUniform1i", true);
    qglUniform1f = (PFNGLUNIFORM1FPROC)LoadProc("glUniform1f", true);
    qglUniform2fv = (PFNGLUNIFORM2FVPROC)LoadProc("glUniform2fv", true);
    qglUniform3fv = (PFNGLUNIFORM3FVPROC)LoadProc("glUniform3fv", true);
    qglUniform4fv = (PFNGLUNIFORM4FVPROC)LoadProc("glUniform4fv", true);
    qglUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)LoadProc("glUniformMatrix4fv", true);

    qglGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)LoadProc("glGenVertexArrays", true);
    qglDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)LoadProc("glDeleteVertexArrays", true);
    qglBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)LoadProc("glBindVertexArray", true);

    qglGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)LoadProc("glGenerateMipmap", false);

    qglGenSamplers = (PFNGLGENSAMPLERSPROC)LoadProc("glGenSamplers", true);
    qglDeleteSamplers = (PFNGLDELETESAMPLERSPROC)LoadProc("glDeleteSamplers", true);
    qglBindSampler = (PFNGLBINDSAMPLERPROC)LoadProc("glBindSampler", true);
    qglSamplerParameteri = (PFNGLSAMPLERPARAMETERIPROC)LoadProc("glSamplerParameteri", true);

    if (gl_extensions.find("GL_EXT_texture_filter_anisotropic") != std::string::npos)
    {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_max_anisotropy);
        Con_Printf("Anisotropic filtering: %.0fx\n", gl_max_anisotropy);
    }

    Con_Printf("GL extensions loaded\n");
}
