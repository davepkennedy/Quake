#pragma once

// Compile a single shader stage; returns 0 and prints error on failure.
GLuint GL_CompileShader(GLenum type, const char *src);

// Compile both stages and link; deletes stage objects on success or failure.
// Returns 0 and prints error on failure.
GLuint GL_BuildProgram(const char *vert_src, const char *frag_src);

// Combines the CPU-side r_proj_matrix, r_world_matrix (view), and r_entity_matrix
// (identity unless a per-entity model transform is active -- see R_RotateForEntity)
// into a single column-major 4x4 matrix. Replaces the legacy fixed-function
// matrix stack -- no glGetFloatv/GL_PROJECTION_MATRIX/GL_MODELVIEW_MATRIX involved,
// so this works under a Core Profile context.
void GL_GetMVP(float *out16);

// out = a * b, column-major 4x4 (matches GL's convention: transforms compose as
// out*v == a*(b*v), i.e. b is applied first).
void GL_Mat4Mul(const float *a, const float *b, float *out);

// out = identity.
void GL_Mat4Identity(float *out16);
