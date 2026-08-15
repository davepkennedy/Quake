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
        Sys_Error("GL_LoadExtensions: required function {} not found", name);
    }
    return p;
}

void GL_LoadExtensions(void)
{
    qglActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(LoadProc("glActiveTexture", true));

    qglGenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(LoadProc("glGenBuffers", true));
    qglDeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(LoadProc("glDeleteBuffers", true));
    qglBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(LoadProc("glBindBuffer", true));
    qglBufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(LoadProc("glBufferData", true));
    qglBufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(LoadProc("glBufferSubData", true));

    qglVertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(LoadProc("glVertexAttribPointer", true));
    qglEnableVertexAttribArray = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(LoadProc("glEnableVertexAttribArray", true));
    qglDisableVertexAttribArray = reinterpret_cast<PFNGLDISABLEVERTEXATTRIBARRAYPROC>(LoadProc("glDisableVertexAttribArray", true));

    qglCreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(LoadProc("glCreateShader", true));
    qglShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(LoadProc("glShaderSource", true));
    qglCompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(LoadProc("glCompileShader", true));
    qglGetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(LoadProc("glGetShaderiv", true));
    qglGetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(LoadProc("glGetShaderInfoLog", true));
    qglDeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(LoadProc("glDeleteShader", true));
    qglCreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(LoadProc("glCreateProgram", true));
    qglAttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(LoadProc("glAttachShader", true));
    qglLinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(LoadProc("glLinkProgram", true));
    qglGetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(LoadProc("glGetProgramiv", true));
    qglGetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(LoadProc("glGetProgramInfoLog", true));
    qglUseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(LoadProc("glUseProgram", true));
    qglDeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(LoadProc("glDeleteProgram", true));

    qglGetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(LoadProc("glGetUniformLocation", true));
    qglUniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(LoadProc("glUniform1i", true));
    qglUniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(LoadProc("glUniform1f", true));
    qglUniform2fv = reinterpret_cast<PFNGLUNIFORM2FVPROC>(LoadProc("glUniform2fv", true));
    qglUniform3fv = reinterpret_cast<PFNGLUNIFORM3FVPROC>(LoadProc("glUniform3fv", true));
    qglUniform4fv = reinterpret_cast<PFNGLUNIFORM4FVPROC>(LoadProc("glUniform4fv", true));
    qglUniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(LoadProc("glUniformMatrix4fv", true));

    qglGenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(LoadProc("glGenVertexArrays", true));
    qglDeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(LoadProc("glDeleteVertexArrays", true));
    qglBindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(LoadProc("glBindVertexArray", true));

    qglGenerateMipmap = reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(LoadProc("glGenerateMipmap", false));

    qglGenSamplers = reinterpret_cast<PFNGLGENSAMPLERSPROC>(LoadProc("glGenSamplers", true));
    qglDeleteSamplers = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(LoadProc("glDeleteSamplers", true));
    qglBindSampler = reinterpret_cast<PFNGLBINDSAMPLERPROC>(LoadProc("glBindSampler", true));
    qglSamplerParameteri = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(LoadProc("glSamplerParameteri", true));

    if (gl_extensions.find("GL_EXT_texture_filter_anisotropic") != std::string::npos)
    {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_max_anisotropy);
        Con_Printf("Anisotropic filtering: %.0fx\n", gl_max_anisotropy);
    }

    Con_Printf("GL extensions loaded\n");
}
