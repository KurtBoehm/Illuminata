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
  vertex_shader = GL_VERTEX_SHADER,
  fragment_shader = GL_FRAGMENT_SHADER,
};
inline auto format_as(ShaderKind f) {
  return fmt::underlying(f);
}

enum struct BufferBindingTarget {
  array_buffer = GL_ARRAY_BUFFER,
  atomic_counter_buffer = GL_ATOMIC_COUNTER_BUFFER,
  copy_read_buffer = GL_COPY_READ_BUFFER,
  copy_write_buffer = GL_COPY_WRITE_BUFFER,
  dispatch_indirect_buffer = GL_DISPATCH_INDIRECT_BUFFER,
  draw_indirect_buffer = GL_DRAW_INDIRECT_BUFFER,
  element_array_buffer = GL_ELEMENT_ARRAY_BUFFER,
  pixel_pack_buffer = GL_PIXEL_PACK_BUFFER,
  pixel_unpack_buffer = GL_PIXEL_UNPACK_BUFFER,
  query_buffer = GL_QUERY_BUFFER,
  shader_storage_buffer = GL_SHADER_STORAGE_BUFFER,
  texture_buffer = GL_TEXTURE_BUFFER,
  transform_feedback_buffer = GL_TRANSFORM_FEEDBACK_BUFFER,
  uniform_buffer = GL_UNIFORM_BUFFER,
};

enum struct TextureKind {
  texture_1d = GL_TEXTURE_1D,
  texture_2d = GL_TEXTURE_2D,
  texture_3d = GL_TEXTURE_3D,
  texture_rectangle = GL_TEXTURE_RECTANGLE,
  texture_buffer = GL_TEXTURE_BUFFER,
  texture_cube_map = GL_TEXTURE_CUBE_MAP,
  texture_1d_array = GL_TEXTURE_1D_ARRAY,
  texture_2d_array = GL_TEXTURE_2D_ARRAY,
  texture_cube_map_array = GL_TEXTURE_CUBE_MAP_ARRAY,
  texture_2d_multisample = GL_TEXTURE_2D_MULTISAMPLE,
  texture_2d_multisample_array = GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
};

enum struct PixelFormat {
  red = GL_RED,
  rg = GL_RG,
  rgb = GL_RGB,
  rgba = GL_RGBA,
};

enum struct PixelKind {
  u8 = GL_UNSIGNED_BYTE,
  i8 = GL_BYTE,
  u16 = GL_UNSIGNED_SHORT,
  i16 = GL_SHORT,
  u32 = GL_UNSIGNED_INT,
  i32 = GL_INT,
  f16 = GL_HALF_FLOAT,
  f32 = GL_FLOAT,
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
                    (kind_ == ShaderKind::vertex_shader) ? "vertex" : "fragment", msg)};
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
