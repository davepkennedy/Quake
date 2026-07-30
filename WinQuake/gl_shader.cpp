#include "quakedef.h"
#include <glm/gtc/type_ptr.hpp>

void GL_GetMVP(float *out16)
{
    glm::mat4 mvp = r_proj_matrix * r_world_matrix * r_entity_matrix;
    memcpy(out16, glm::value_ptr(mvp), 16 * sizeof(float));
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

GLProgram GL_BuildProgram(const char *vert_src, const char *frag_src)
{
    GLuint vert = GL_CompileShader(GL_VERTEX_SHADER, vert_src);
    GLuint frag = GL_CompileShader(GL_FRAGMENT_SHADER, frag_src);

    if (!vert || !frag)
    {
        if (vert)
        {
            qglDeleteShader(vert);
        }
        if (frag)
        {
            qglDeleteShader(frag);
        }
        return GLProgram();
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
        return GLProgram();
    }
    return GLProgram(prog);
}
