#pragma once

// Compile a single shader stage; returns 0 and prints error on failure.
GLuint GL_CompileShader(GLenum type, const char *src);

// Compile both stages and link; deletes stage objects on success or failure.
// Returns 0 and prints error on failure.
GLuint GL_BuildProgram(const char *vert_src, const char *frag_src);
