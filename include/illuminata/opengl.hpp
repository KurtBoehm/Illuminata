#ifndef INCLUDE_ILLUMINATA_OPENGL_HPP
#define INCLUDE_ILLUMINATA_OPENGL_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>

// IWYU pragma: begin_exports
#include <epoxy/gl.h>
#include <epoxy/gl_generated.h>
// IWYU pragma: end_exports

#include "fmt.hpp"

namespace gl {
enum struct ShaderKind {
  VERTEX_SHADER = GL_VERTEX_SHADER,
  FRAGMENT_SHADER = GL_FRAGMENT_SHADER,
};
inline auto format_as(ShaderKind f) {
  return fmt::underlying(f);
}

enum struct BufferBindingTarget {
  ARRAY_BUFFER = GL_ARRAY_BUFFER,
  ATOMIC_COUNTER_BUFFER = GL_ATOMIC_COUNTER_BUFFER,
  COPY_READ_BUFFER = GL_COPY_READ_BUFFER,
  COPY_WRITE_BUFFER = GL_COPY_WRITE_BUFFER,
  DISPATCH_INDIRECT_BUFFER = GL_DISPATCH_INDIRECT_BUFFER,
  DRAW_INDIRECT_BUFFER = GL_DRAW_INDIRECT_BUFFER,
  ELEMENT_ARRAY_BUFFER = GL_ELEMENT_ARRAY_BUFFER,
  PIXEL_PACK_BUFFER = GL_PIXEL_PACK_BUFFER,
  PIXEL_UNPACK_BUFFER = GL_PIXEL_UNPACK_BUFFER,
  QUERY_BUFFER = GL_QUERY_BUFFER,
  SHADER_STORAGE_BUFFER = GL_SHADER_STORAGE_BUFFER,
  TEXTURE_BUFFER = GL_TEXTURE_BUFFER,
  TRANSFORM_FEEDBACK_BUFFER = GL_TRANSFORM_FEEDBACK_BUFFER,
  UNIFORM_BUFFER = GL_UNIFORM_BUFFER,
};

enum struct TextureKind {
  TEXTURE_1D = GL_TEXTURE_1D,
  TEXTURE_2D = GL_TEXTURE_2D,
  TEXTURE_3D = GL_TEXTURE_3D,
  TEXTURE_RECTANGLE = GL_TEXTURE_RECTANGLE,
  TEXTURE_BUFFER = GL_TEXTURE_BUFFER,
  TEXTURE_CUBE_MAP = GL_TEXTURE_CUBE_MAP,
  TEXTURE_1D_ARRAY = GL_TEXTURE_1D_ARRAY,
  TEXTURE_2D_ARRAY = GL_TEXTURE_2D_ARRAY,
  TEXTURE_CUBE_MAP_ARRAY = GL_TEXTURE_CUBE_MAP_ARRAY,
  TEXTURE_2D_MULTISAMPLE = GL_TEXTURE_2D_MULTISAMPLE,
  TEXTURE_2D_MULTISAMPLE_ARRAY = GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
};

enum struct PixelFormat {
  RED = GL_RED,
  RG = GL_RG,
  RGB = GL_RGB,
  RGBA = GL_RGBA,
};

enum struct PixelKind {
  U8 = GL_UNSIGNED_BYTE,
  I8 = GL_BYTE,
  U16 = GL_UNSIGNED_SHORT,
  I16 = GL_SHORT,
  U32 = GL_UNSIGNED_INT,
  I32 = GL_INT,
  F16 = GL_HALF_FLOAT,
  F32 = GL_FLOAT,
};

struct Shader {
  explicit Shader(ShaderKind kind) : id_{glCreateShader(static_cast<int>(kind))}, kind_{kind} {
    if (id_ == 0) {
      throw std::runtime_error{"An error occured while creating the shader object."};
    }
  }
  Shader(ShaderKind kind, std::string_view src) : Shader{kind} {
    source(src);
    compile();
  }

  Shader(const Shader&) = delete;
  Shader(Shader&& other) noexcept : id_(other.id_), kind_(other.kind_) {
    other.id_ = 0;
  }
  Shader& operator=(const Shader&) = delete;
  Shader& operator=(Shader&& other) = delete;
  ~Shader() {
    if (id_ != 0) {
      glDeleteShader(id_);
    }
  }

  void source(std::string_view src) {
    const char* data = src.data();
    auto len = static_cast<GLint>(src.size());
    glShaderSource(id_, 1, &data, &len);
  }
  void compile() {
    glCompileShader(id_);

    int status{};
    glGetShaderiv(id_, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
      GLint log_len{};
      glGetShaderiv(id_, GL_INFO_LOG_LENGTH, &log_len);

      auto buffer = std::make_unique<char>(log_len + 1);
      glGetShaderInfoLog(id_, log_len, nullptr, buffer.get());

      std::string_view msg{std::string_view{buffer.get(), static_cast<std::size_t>(log_len)}};
      fmt::print("msg: {}", msg);
      throw std::runtime_error{
        fmt::format("Compile failure in {} shader:\n{}",
                    (kind_ == ShaderKind::VERTEX_SHADER) ? "vertex" : "fragment", msg)};
    }
  }

  [[nodiscard]] GLuint id() const {
    return id_;
  }

private:
  GLuint id_;
  ShaderKind kind_;
};

struct TextureBindCtx {
  explicit TextureBindCtx(GLuint id, TextureKind kind) : id_{id}, kind_{kind} {
    glBindTexture(static_cast<GLenum>(kind_), id_);
  }
  TextureBindCtx(const TextureBindCtx&) = delete;
  TextureBindCtx(TextureBindCtx&& other) noexcept : id_{other.id_}, kind_{other.kind_} {
    other.id_ = 0;
  }
  TextureBindCtx& operator=(const TextureBindCtx&) = delete;
  TextureBindCtx& operator=(TextureBindCtx&&) = delete;
  ~TextureBindCtx() {
    if (id_ != 0) {
      glBindTexture(static_cast<GLenum>(kind_), 0);
    }
  }

private:
  GLuint id_{};
  TextureKind kind_;
};

struct Texture {
  explicit Texture(TextureKind kind) : kind_{kind} {
    glGenTextures(1, &id_);
    assert(id_ != 0);
  }
  Texture(const Texture&) = delete;
  Texture(Texture&& other) noexcept : id_{other.id_}, kind_{other.kind_} {
    other.id_ = 0;
  }
  Texture& operator=(const Texture&) = delete;
  Texture& operator=(Texture&&) = delete;
  ~Texture() {
    if (id_ != 0) {
      glDeleteTextures(1, &id_);
    }
  }

  TextureBindCtx bind() {
    return TextureBindCtx{id_, kind_};
  }

  void load(const std::uint8_t* data, GLsizei w, GLsizei h, PixelFormat format) {
    assert(kind_ == TextureKind::TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(static_cast<GLenum>(kind_), 0, static_cast<GLint>(format), w, h, 0,
                 static_cast<GLenum>(format), GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  [[nodiscard]] GLuint id() const {
    return id_;
  }
  [[nodiscard]] TextureKind kind() const {
    return kind_;
  }

private:
  GLuint id_{};
  TextureKind kind_;
};

struct TextureUnit {
  explicit TextureUnit(GLint idx) : idx_{idx} {
    glActiveTexture(GL_TEXTURE0 + idx_);
  }

  void bind(const Texture& tex) {
    glBindTexture(static_cast<GLenum>(tex.kind()), tex.id());
  }

  void set_uniform(GLint uni) {
    glUniform1i(uni, idx_);
  }

private:
  GLint idx_;
};

struct VertexArrayBind {
  explicit VertexArrayBind(GLuint id) : id_(id) {
    glBindVertexArray(id_);
  }
  VertexArrayBind(const VertexArrayBind&) = delete;
  VertexArrayBind(VertexArrayBind&&) = delete;
  VertexArrayBind& operator=(const VertexArrayBind&) = delete;
  VertexArrayBind& operator=(VertexArrayBind&&) = delete;
  ~VertexArrayBind() {
    glBindVertexArray(0);
  }

private:
  GLuint id_;
};

struct VertexBufferBind {
  explicit VertexBufferBind(GLuint id, BufferBindingTarget target) : id_(id), target_(target) {
    glBindBuffer(static_cast<GLenum>(target_), id_);
  }
  VertexBufferBind(const VertexBufferBind&) = delete;
  VertexBufferBind(VertexBufferBind&&) = delete;
  VertexBufferBind& operator=(const VertexBufferBind&) = delete;
  VertexBufferBind& operator=(VertexBufferBind&&) = delete;
  ~VertexBufferBind() {
    glBindBuffer(static_cast<GLenum>(target_), 0);
  }

private:
  GLuint id_;
  BufferBindingTarget target_;
};

struct VertexArray {
  VertexArray() {
    glGenVertexArrays(1, &id_);
    assert(id_ != 0);
  }
  VertexArray(const VertexArray&) = delete;
  VertexArray(VertexArray&& other) noexcept : id_{other.id_} {
    other.id_ = 0;
  }
  VertexArray& operator=(const VertexArray&) = delete;
  VertexArray& operator=(VertexArray&&) = delete;
  ~VertexArray() {
    if (id_ != 0) {
      glDeleteBuffers(1, &id_);
    }
  }

  VertexArrayBind bind() {
    return VertexArrayBind{id_};
  }
  VertexBufferBind bind_buffer(BufferBindingTarget target) {
    return VertexBufferBind{id_, target};
  }

  [[nodiscard]] GLuint id() const {
    return id_;
  }

private:
  GLuint id_{};
};

struct ProgramUse {
  explicit ProgramUse(GLuint id) : id_(id) {
    glUseProgram(id_);
  }
  ProgramUse(const ProgramUse&) = delete;
  ProgramUse(ProgramUse&&) = delete;
  ProgramUse& operator=(const ProgramUse&) = delete;
  ProgramUse& operator=(ProgramUse&&) = delete;
  ~ProgramUse() {
    glUseProgram(0);
  }

private:
  GLuint id_;
};

struct Program {
  Program() : id_{glCreateProgram()} {
    if (id_ == 0) {
      throw std::runtime_error{"An error occured while creating the program object."};
    }
  }
  Program(const Program&) = delete;
  Program(Program&& other) noexcept : id_{other.id_} {
    other.id_ = 0;
  }
  Program& operator=(const Program&) = delete;
  Program& operator=(Program&&) = delete;
  ~Program() {
    if (id_ != 0) {
      glDeleteProgram(id_);
    }
  }

  void attach(const gl::Shader& shader) {
    glAttachShader(id_, shader.id());
  }

  void link() {
    glLinkProgram(id_);

    int status{};
    glGetProgramiv(id_, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
      GLint log_len{};
      glGetProgramiv(id_, GL_INFO_LOG_LENGTH, &log_len);

      auto buffer = std::make_unique<char>(log_len + 1);
      glGetProgramInfoLog(id_, log_len, nullptr, buffer.get());

      std::string_view msg{std::string_view{buffer.get(), static_cast<std::size_t>(log_len)}};
      throw std::runtime_error{fmt::format("Linking failure:\n{}", msg)};
    }
  }

  GLint uniform_location(const char* name) const {
    return glGetUniformLocation(id_, name);
  }

  void detach(const gl::Shader& shader) {
    glDetachShader(id_, shader.id());
  }

  ProgramUse use() {
    return ProgramUse{id_};
  }

  [[nodiscard]] GLuint id() const {
    return id_;
  }

private:
  GLuint id_;
};
} // namespace gl

#endif // INCLUDE_ILLUMINATA_OPENGL_HPP
