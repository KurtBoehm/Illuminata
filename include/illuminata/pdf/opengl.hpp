#ifndef INCLUDE_ILLUMINATA_PDF_OPENGL_HPP
#define INCLUDE_ILLUMINATA_PDF_OPENGL_HPP

#include <array>
#include <cmath>
#include <optional>

#include "illuminata/geometry.hpp"
#include "illuminata/mupdf.hpp"
#include "illuminata/opengl.hpp"

#if ILLUMINATA_PRINT
#include "illuminata/fmt.hpp"
#endif

namespace illa {
// A screen-filling quad used so that the fragment shader is called for every pixel.
inline constexpr std::array<GLfloat, 12> vertex_data{
  -1.F, +1.F, // vertex 0
  +1.F, -1.F, // vertex 1
  -1.F, -1.F, // vertex 2
  -1.F, +1.F, // vertex 3
  +1.F, +1.F, // vertex 4
  +1.F, -1.F, // vertex 5
};

// Passing through the vertex coordinates of a screen-filling quad.
inline constexpr char vertex_shader_code[] = "#version 320 es\n"
                                             "\n"
                                             "layout(location = 0) in vec2 position;\n"
                                             "\n"
                                             "void main() {\n"
                                             "  gl_Position = vec4(position, 0.0, 1.0);\n"
                                             "}";

// If the coordinate is in the visible area, fetch the correct texel and optionally inverts it,
// otherwise returns a fully transparent color.
inline constexpr char fragment_shader_code[] =
  "#version 320 es\n"
  "precision mediump float;\n"
  "\n"
  "out vec4 outColor;\n"
  // {offset.x, windowDims.y - offset.y}
  "uniform int offsets[2];\n"
  "uniform bool invert;\n"
  "uniform sampler2D tex;\n"
  "\n"
  "void main() {\n"
  "  ivec2 coord = ivec2(gl_FragCoord);\n"
  // The coordinate within tex, where the “offset” from above is relative to the upper left corner.
  // Since gl_FragCoord.y increases from bottom to top, coord.y is
  // (windowDims.y - 1 - coord.y) - offset.y
  "  coord = ivec2(coord.x - offsets[0], offsets[1] - coord.y - 1);\n"
  "  ivec2 texDims = textureSize(tex, 0);\n"
  "  if (0 > coord.x || coord.x >= texDims.x || 0 > coord.y || coord.y >= texDims.y) {\n"
  "    outColor = vec4(0.0);\n"
  "  } else {\n"
  "    outColor = texelFetch(tex, coord, 0);\n"
  //   For the inversion, convert the sRGB color to YCbCR, invert Y, and convert back.
  //   Intuitively, this preserves hue and saturation (reasonably well) while inverting brightness.
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
  // An optional so that it can be created in `realize` and destroyed in `unrealize`.
  std::optional<gl::Program> prog{};
  // An optional so that it can be created in `realize` and destroyed in `unrealize`.
  std::optional<gl::VertexArray> vtxs{};
  // An optional so that it can be created in `realize` and destroyed in `unrealize`.
  std::optional<gl::Texture> tex{};
  GLint invert_uniform{};
  GLint offs_uniform{};
  GLint tex_uniform{};

  // Called to initialize the GLArea.
  void realize() {
    gl::VertexArray& vao = vtxs.emplace();

    // Set the vertex array buffer to a screen-filling quad.
    {
      GLuint buffer{};
      glGenBuffers(1, &buffer);

      auto vao_ctx = vao.bind();
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data.data(), GL_STATIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    gl::Shader vertex{gl::ShaderKind::vertex_shader, vertex_shader_code};
    gl::Shader fragment{gl::ShaderKind::fragment_shader, fragment_shader_code};

    gl::Program& program = prog.emplace();
    program.attach(vertex);
    program.attach(fragment);
    program.link();
    invert_uniform = program.uniform_location("invert");
    offs_uniform = program.uniform_location("offsets");
    tex_uniform = program.uniform_location("tex");
    program.detach(vertex);
    program.detach(fragment);

    tex.emplace(gl::TextureKind::texture_2d);
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
      auto buf_ctx = vao.bind_buffer(gl::BufferBindingTarget::array_buffer);

      {
        gl::TextureUnit tu{0};
        auto& tx = *tex;
        tu.bind(tx);
#if ILLUMINATA_PRINT
        fmt::print("load: {}×{}×{}\n", pix.w(), pix.h(), pix.s());
#endif
        tx.load(pix.samples(), pix.w(), pix.h(), gl::PixelFormat::rgb);
        tu.set_uniform(tex_uniform);
      }

      {
        glUniform1i(invert_uniform, static_cast<GLint>(invert));
        std::array<GLint, 2> arr{GLint(std::round(off.x)), GLint(dims.h - std::lround(off.y))};
        glUniform1iv(offs_uniform, arr.size(), arr.data());
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
