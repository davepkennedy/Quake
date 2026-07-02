#include "quakedef.h"

static void MatMul4x4(const float *a, const float *b, float *out)
{
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
        {
            float s = 0.f;
            for (int k = 0; k < 4; k++)
                s += a[k*4 + row] * b[col*4 + k];
            out[col*4 + row] = s;
        }
}

void GL_GetMVP(float *out16)
{
    float proj[16], mv[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX,  mv);
    MatMul4x4(proj, mv, out16);
}

GLuint GL_CompileShader(GLenum type, const char *src)
{
    GLuint s = qglCreateShader(type);
    qglShaderSource(s, 1, &src, nullptr);
    qglCompileShader(s);

    GLint ok = 0;
    qglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        qglGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        char *log = new char[len + 1]();
        qglGetShaderInfoLog(s, len, nullptr, log);
        Con_Printf("Shader compile error:\n%s\n", log);
        delete[] log;
        qglDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint GL_BuildProgram(const char *vert_src, const char *frag_src)
{
    GLuint vert = GL_CompileShader(GL_VERTEX_SHADER,   vert_src);
    GLuint frag = GL_CompileShader(GL_FRAGMENT_SHADER, frag_src);

    if (!vert || !frag)
    {
        if (vert) qglDeleteShader(vert);
        if (frag) qglDeleteShader(frag);
        return 0;
    }

    GLuint prog = qglCreateProgram();
    qglAttachShader(prog, vert);
    qglAttachShader(prog, frag);
    qglLinkProgram(prog);

    qglDeleteShader(vert);
    qglDeleteShader(frag);

    GLint ok = 0;
    qglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        qglGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char *log = new char[len + 1]();
        qglGetProgramInfoLog(prog, len, nullptr, log);
        Con_Printf("Shader link error:\n%s\n", log);
        delete[] log;
        qglDeleteProgram(prog);
        return 0;
    }
    return prog;
}
