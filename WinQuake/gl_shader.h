#pragma once

// Compile a single shader stage; returns 0 and prints error on failure.
GLuint GL_CompileShader(GLenum type, const char *src);

// Compile both stages and link; deletes stage objects on success or failure.
// Returns 0 and prints error on failure.
GLuint GL_BuildProgram(const char *vert_src, const char *frag_src);

// Combines the current fixed-function GL_PROJECTION_MATRIX and GL_MODELVIEW_MATRIX
// into a single column-major 4x4 matrix, for shaders replacing immediate-mode draws
// that still rely on glLoadMatrixf/glRotatef/glTranslatef for entity transforms.
void GL_GetMVP(float *out16);
