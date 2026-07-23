#pragma once

// Move-only RAII wrappers around OpenGL objects. Each deletes its handle in
// Release() (idempotent) and in its destructor (a safety net only -- real
// cleanup happens via an explicit Xxx_Shutdown() call from Host_Shutdown,
// before VID_Shutdown() destroys the GL context; a destructor that runs at
// static-storage-duration teardown would fire after that, with no current
// context). Copy is disabled so a handle can never be owned by two objects
// at once. An implicit conversion to GLuint lets every existing per-frame
// call site (qglBindVertexArray(x), qglUseProgram(x), ...) keep compiling
// unchanged -- only declaration and construction sites need to change.

class GLVertexArray
{
public:
	GLVertexArray () = default;
	~GLVertexArray () { Release (); }

	GLVertexArray (const GLVertexArray &) = delete;
	GLVertexArray &operator= (const GLVertexArray &) = delete;

	GLVertexArray (GLVertexArray &&other) noexcept : handle (other.handle) { other.handle = 0; }
	GLVertexArray &operator= (GLVertexArray &&other) noexcept
	{
		if (this != &other)
		{
			Release ();
			handle = other.handle;
			other.handle = 0;
		}
		return *this;
	}

	static GLVertexArray Create ()
	{
		GLVertexArray v;
		qglGenVertexArrays (1, &v.handle);
		return v;
	}

	void Release ()
	{
		if (handle)
		{
			qglDeleteVertexArrays (1, &handle);
			handle = 0;
		}
	}

	operator GLuint () const { return handle; }

private:
	GLuint handle = 0;
};

class GLBuffer
{
public:
	GLBuffer () = default;
	~GLBuffer () { Release (); }

	GLBuffer (const GLBuffer &) = delete;
	GLBuffer &operator= (const GLBuffer &) = delete;

	GLBuffer (GLBuffer &&other) noexcept : handle (other.handle) { other.handle = 0; }
	GLBuffer &operator= (GLBuffer &&other) noexcept
	{
		if (this != &other)
		{
			Release ();
			handle = other.handle;
			other.handle = 0;
		}
		return *this;
	}

	static GLBuffer Create ()
	{
		GLBuffer b;
		qglGenBuffers (1, &b.handle);
		return b;
	}

	void Release ()
	{
		if (handle)
		{
			qglDeleteBuffers (1, &handle);
			handle = 0;
		}
	}

	operator GLuint () const { return handle; }

private:
	GLuint handle = 0;
};

class GLSampler
{
public:
	GLSampler () = default;
	~GLSampler () { Release (); }

	GLSampler (const GLSampler &) = delete;
	GLSampler &operator= (const GLSampler &) = delete;

	GLSampler (GLSampler &&other) noexcept : handle (other.handle) { other.handle = 0; }
	GLSampler &operator= (GLSampler &&other) noexcept
	{
		if (this != &other)
		{
			Release ();
			handle = other.handle;
			other.handle = 0;
		}
		return *this;
	}

	static GLSampler Create ()
	{
		GLSampler s;
		qglGenSamplers (1, &s.handle);
		return s;
	}

	void Release ()
	{
		if (handle)
		{
			qglDeleteSamplers (1, &handle);
			handle = 0;
		}
	}

	operator GLuint () const { return handle; }

private:
	GLuint handle = 0;
};

class GLProgram
{
public:
	GLProgram () = default;
	explicit GLProgram (GLuint alreadyBuilt) : handle (alreadyBuilt) {}
	~GLProgram () { Release (); }

	GLProgram (const GLProgram &) = delete;
	GLProgram &operator= (const GLProgram &) = delete;

	GLProgram (GLProgram &&other) noexcept : handle (other.handle) { other.handle = 0; }
	GLProgram &operator= (GLProgram &&other) noexcept
	{
		if (this != &other)
		{
			Release ();
			handle = other.handle;
			other.handle = 0;
		}
		return *this;
	}

	void Release ()
	{
		if (handle)
		{
			qglDeleteProgram (handle);
			handle = 0;
		}
	}

	operator GLuint () const { return handle; }

private:
	GLuint handle = 0;
};

// Compile a single shader stage; returns 0 and prints error on failure.
GLuint GL_CompileShader(GLenum type, const char *src);

// Compile both stages and link; deletes stage objects on success or failure.
// Returns an empty (handle 0) GLProgram and prints error on failure.
GLProgram GL_BuildProgram(const char *vert_src, const char *frag_src);

// Combines the CPU-side r_proj_matrix, r_world_matrix (view), and r_entity_matrix
// (identity unless a per-entity model transform is active -- see R_RotateForEntity)
// into a single column-major 4x4 matrix, ready for glUniformMatrix4fv. Replaces the
// legacy fixed-function matrix stack -- no glGetFloatv/GL_PROJECTION_MATRIX/
// GL_MODELVIEW_MATRIX involved, so this works under a Core Profile context.
void GL_GetMVP(float *out16);
