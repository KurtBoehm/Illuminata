#ifndef INCLUDE_ILLUMINATA_PDF_OPENGL_HPP
#define INCLUDE_ILLUMINATA_PDF_OPENGL_HPP

#include <array>
#include <cmath>
#include <optional>

#include "illuminata/fmt.hpp"
#include "illuminata/geometry.hpp"
#include "illuminata/mupdf.hpp"
#include "illuminata/opengl.hpp"

namespace illa {
inline constexpr std::array<GLfloat, 12> vertex_data{
  -1.F, +1.F, // vertex 0
  +1.F, -1.F, // vertex 1
  -1.F, -1.F, // vertex 2
  -1.F, +1.F, // vertex 3
  +1.F, +1.F, // vertex 4
  +1.F, -1.F, // vertex 5
};

inline constexpr char vertex_shader_code[] = "#version 320 es\n"
                                             "\n"
                                             "layout(location = 0) in vec2 position;\n"
                                             "\n"
                                             "void main() {\n"
                                             "  gl_Position = vec4(position, 0.0, 1.0);\n"
                                             "}";

inline constexpr char fragment_shader_code[] =
  "#version 320 es\n"
  "precision mediump float;\n"
  "\n"
  "out vec4 outColor;\n"
  "uniform bool invert;\n"
  "uniform int area[4];\n"
  "uniform sampler2D tex;\n"
  "\n"
  "void main() {\n"
  "  ivec2 coord = ivec2(gl_FragCoord);\n"
  "  coord = ivec2(coord.x - area[0], area[1] - coord.y - 1);\n"
  "  if (0 > coord.x || coord.x >= area[2] || 0 > coord.y || coord.y >= area[3]) {\n"
  "    outColor = vec4(0.0);\n"
  "  } else {\n"
  "    outColor = texelFetch(tex, coord, 0);\n"
  "    if (invert) {\n"
  "      const float h = 128.0 / 255.0;\n"
  "      float y  = 0.299 * outColor.r + 0.587 * outColor.g + 0.114 * outColor.b;\n"
  "      float cb = h - 0.168736 * outColor.r - 0.331264 * outColor.g + 0.5 * outColor.b;\n"
  "      float cr = h + 0.5 * outColor.r - 0.418688 * outColor.g - 0.081312 * outColor.b;\n"
  "      y = 1.0 - y;\n"
  "      float r = y + 1.402 * (cr - h);\n"
  "      float g = y - 0.344136 * (cb - h) - 0.714136 * (cr - h);\n"
  "      float b = y + 1.772 * (cb - h);\n"
  "      outColor = vec4(r, g, b, outColor.a);\n"
  "    }\n"
  "  }\n"
  "}";

struct OpenGlState {
  std::optional<gl::Program> prog{};
  std::optional<gl::VertexArray> vtxs{};
  std::optional<gl::Texture> tex{};
  GLint invert_uniform{};
  GLint area_uniform{};
  GLint tex_uniform{};

  void realize() {
    gl::VertexArray& vao = vtxs.emplace();

    {
      GLuint buffer{};
      glGenBuffers(1, &buffer);

      auto vao_ctx = vao.bind();
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data.data(), GL_STATIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    gl::Shader vertex{gl::ShaderKind::VERTEX_SHADER, vertex_shader_code};
    gl::Shader fragment{gl::ShaderKind::FRAGMENT_SHADER, fragment_shader_code};

    gl::Program& program = prog.emplace();
    program.attach(vertex);
    program.attach(fragment);
    program.link();
    invert_uniform = program.uniform_location("invert");
    area_uniform = program.uniform_location("area");
    tex_uniform = program.uniform_location("tex");
    program.detach(vertex);
    program.detach(fragment);

    tex.emplace(gl::TextureKind::TEXTURE_2D);
  }

  void unrealize() {
    vtxs.reset();
    prog.reset();
  }

  void draw(mupdf::FzPixmap& pix, const Dims<int> dims, const Vec2<float> off, bool invert) {
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    {
      auto prog_ctx = prog.value().use();
      auto& vao = vtxs.value();
      auto vao_ctx = vao.bind();
      auto buf_ctx = vao.bind_buffer(gl::BufferBindingTarget::ARRAY_BUFFER);

      {
        gl::TextureUnit tu{0};
        auto& tx = *tex;
        tu.bind(tx);
#if ILLUMINATA_PRINT
        fmt::print("load: {}×{}×{}\n", pix.w(), pix.h(), pix.s());
#endif
        tx.load(pix.samples(), pix.w(), pix.h(), gl::PixelFormat::RGB);
        tu.set_uniform(tex_uniform);
      }

      {
        glUniform1i(invert_uniform, static_cast<GLint>(invert));
        std::array<GLint, 4> arr{
          GLint(std::round(off.x)),
          dims.h - GLint(std::round(off.y)),
          pix.w(),
          pix.h(),
        };
        glUniform1iv(area_uniform, arr.size(), arr.data());
      }

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
      glDrawArrays(GL_TRIANGLES, 0, vertex_data.size());
      glDisableVertexAttribArray(0);
    }

    glFlush();
  }
};
} // namespace illa

#endif // INCLUDE_ILLUMINATA_PDF_OPENGL_HPP
