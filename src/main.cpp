#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "hirgon/hirgon.hpp"

#define OPENGL true
#define PRINT true

#if OPENGL
#include <array>
#endif
#if PRINT
#include <chrono>
#endif

struct PdfPageInfo {
  mupdf::FzPage page;
  mupdf::FzDisplayList display_list;
};

struct PdfInfo {
  std::filesystem::path path;
  mupdf::FzDocument doc;
  int page;
  std::optional<PdfPageInfo> page_info;

  explicit PdfInfo(std::filesystem::path pdf, int pno = 0)
      : path{std::move(pdf)}, doc{path.c_str()}, page{pno} {
#if PRINT
    fmt::print("Open {:?}\n", path);
#endif
    update_page(pno);
  }

  void update_page(int pno) {
    page = pno;
    if (valid_page(pno)) {
#if PRINT
      fmt::print("load page {}\n", pno);
#endif
      mupdf::FzPage p = doc.fz_load_page(pno);
      page_info = PdfPageInfo(p, p.fz_new_display_list_from_page());
    } else {
      page_info.reset();
    }
  }

  void reload_doc() {
    doc = mupdf::FzDocument{path.c_str()};
    update_page(std::min(page, doc.fz_count_pages() - 1));
  }

  [[nodiscard]] bool valid_page(int pno) const {
    return 0 <= pno && pno < doc.fz_count_pages();
  }
};

#if OPENGL
inline constexpr std::array<GLfloat, 24> vertex_data{
  -1.F, 1.F,  0.F, 1.F, // vertex 0
  1.F,  -1.F, 0.F, 1.F, // vertex 1
  -1.F, -1.,  0.F, 1.F, // vertex 2
  -1.F, 1.F,  0.F, 1.F, // vertex 3
  1.F,  1.F,  0.F, 1.F, // vertex 4
  1.F,  -1.F, 0.F, 1.F, // vertex 5
};

inline constexpr char vertex_shader_code[] = "#version 320\n"
                                             "\n"
                                             "layout(location = 0) in vec4 position;\n"
                                             "out vec2 tex_coord;\n"
                                             "\n"
                                             "void main() {\n"
                                             "  gl_Position = position;\n"
                                             "  tex_coord = 0.5 * position.xy + 0.5;\n"
                                             "}";

inline constexpr char fragment_shader_code[] =
  "#version 320\n"
  "precision mediump float;\n"
  "\n"
  "in vec2 tex_coord;\n"
  "out vec4 outColor;\n"
  "uniform bool invert;\n"
  "uniform int area[4];\n"
  "uniform sampler2D tex;\n"
  "\n"
  "void main() {\n"
  "  ivec2 coord = ivec2(gl_FragCoord) - ivec2(area[0], area[1]);\n"
  "  if (0 > coord.x || coord.x >= area[2] || 0 > coord.y || coord.y >= area[3]) {\n"
  "    outColor = vec4(0.0);\n"
  "  } else {\n"
  "    coord = ivec2(coord.x, area[3] - coord.y - 1);\n"
  "    outColor = texelFetch(tex, coord, 0);\n"
#if false
  "    outColor = texture(tex, tex_coord);\n"
#endif
  "    if (invert) {\n"
  "      float y  = 0.299 * outColor.r + 0.587 * outColor.g + 0.114 * outColor.b;\n"
  "      float cb = 0.5 - 0.168736 * outColor.r - 0.331264 * outColor.g + 0.5 * outColor.b;\n"
  "      float cr = 0.5 + 0.5 * outColor.r - 0.418688 * outColor.g - 0.081312 * outColor.b;\n"
  "      y = 1.0 - y;\n"
  "      float r = y + 1.402 * (cr - 0.5);\n"
  "      float g = y - 0.344136 * (cb - 0.5) - 0.714136 * (cr - 0.5);\n"
  "      float b = y + 1.772 * (cb - 0.5);\n"
  "      outColor = vec4(r, g, b, outColor.a);\n"
  "    }\n"
  "  }"
  "}";
#endif

struct PDFViewer : public Gtk::ApplicationWindow {
  std::optional<PdfInfo> pdf{};
  bool invert{};

  std::conditional_t<OPENGL, Gtk::GLArea, Gtk::DrawingArea> draw_area{};
  Gtk::Button open_button{"Open PDF"};
#if OPENGL
  std::optional<gl::Program> prog{};
  std::optional<gl::VertexArray> vtxs{};
  std::optional<gl::Texture> tex{};
  GLint invert_uniform_{};
  GLint area_uniform_{};
  GLint tex_uniform_{};
#endif

  explicit PDFViewer(std::optional<std::filesystem::path> path = {}) {
    set_title("Hirgon");

    auto prefer_dark_theme = get_settings()->property_gtk_application_prefer_dark_theme();
    invert = prefer_dark_theme.get_value();
    [[maybe_unused]] auto dark_conn = prefer_dark_theme.signal_changed().connect(
      [this, prefer_dark_theme] { invert = prefer_dark_theme.get_value(); });

#if OPENGL
    [[maybe_unused]] auto realize_conn = draw_area.signal_realize().connect([&] {
      draw_area.make_current();
      if (draw_area.has_error()) {
        return;
      }

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
      invert_uniform_ = program.uniform_location("invert");
      area_uniform_ = program.uniform_location("area");
      tex_uniform_ = program.uniform_location("tex");
      program.detach(vertex);
      program.detach(fragment);

      tex.emplace(gl::TextureKind::TEXTURE_2D);
    });

    [[maybe_unused]] auto unrealize_conn = draw_area.signal_unrealize().connect(
      [&] {
        draw_area.make_current();
        if (draw_area.has_error()) {
          return;
        }
        vtxs.reset();
        prog.reset();
      },
      false);

    auto draw_op = [this](const Glib::RefPtr<Gdk::GLContext>& /*ctx*/) {
#else
    auto draw_op = [&](const std::shared_ptr<Cairo::Context>& ctx, int width, int height) {
#endif
#if PRINT
      using Clock = std::chrono::steady_clock;
      using Dur = std::chrono::duration<double>;
      const auto t0 = Clock::now();
#endif

      const auto scale_factor = draw_area.get_scale_factor();
#if OPENGL
      const auto width = draw_area.get_width();
      const auto height = draw_area.get_height();
#else
      ctx->scale(1.0 / scale_factor, 1.0 / scale_factor);
#endif
      const auto w = width * scale_factor;
      const auto h = height * scale_factor;

      if (!pdf.has_value() || !pdf->page_info.has_value()) {
#if OPENGL
        return false;
#else
        return
#endif
      }

      const auto& pinfo = pdf->page_info;
      const auto rect = pinfo->page.fz_bound_page();
      const auto f = std::min(float(w) / std::max(0.F, rect.x1 - rect.x0),
                              float(h) / std::max(0.F, rect.y1 - rect.y0));

#if PRINT
      const auto t1 = Clock::now();
#endif

      mupdf::FzPixmap pix{
        pinfo->display_list,
        mupdf::FzMatrix{}.fz_pre_scale(f, f),
        mupdf::FzColorspace::Fixed_RGB,
        0,
      };

#if PRINT
      const auto t2 = Clock::now();
#endif

#if OPENGL
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
#if PRINT
          fmt::print("load: {}×{}×{}\n", pix.w(), pix.h(), pix.s());
#endif
          tx.load(pix.samples(), pix.w(), pix.h(), gl::PixelFormat::RGB);
          tu.set_uniform(tex_uniform_);
        }

        {
          std::array<GLint, 4> arr{
            GLint(w - pix.w()) / 2,
            GLint(h - pix.h()) / 2,
            pix.w(),
            pix.h(),
          };
          glUniform1i(invert_uniform_, static_cast<GLint>(invert));
          glUniform1iv(area_uniform_, 4, arr.data());
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, vertex_data.size());
        glDisableVertexAttribArray(0);
      }
      glFlush();

#if PRINT
      const auto t3 = Clock::now();
      fmt::print("{}×{} → {}×{} → {} → {}×{} {}\n", width, height, w, h, f, pix.w(), pix.h(),
                 pix.alpha());
      fmt::print("setup={}, pixmap={}, opengl={}\n", Dur{t1 - t0}, Dur{t2 - t1}, Dur{t3 - t2});
#endif

      return true;
    };
    [[maybe_unused]] auto render_conn = draw_area.signal_render().connect(draw_op, true);
#else
      auto pixbuf = Gdk::Pixbuf::create_from_data(
        pix.samples(), Gdk::Colorspace::RGB, bool(pix.alpha()), 8, pix.w(), pix.h(), pix.stride());

#if PRINT
      const auto t3 = Clock::now();
#endif

      Gdk::Cairo::set_source_pixbuf(ctx, pixbuf, float(w - pix.w()) / 2.F,
                                    float(h - pix.h()) / 2.F);

#if PRINT
      const auto t4 = Clock::now();
#endif

      ctx->paint();

#if PRINT
      const auto t5 = Clock::now();
      fmt::print("{}×{} → {}×{} → {} → {}×{} {}\n", width, height, w, h, f, pix.fz_pixmap_width(),
                 pix.fz_pixmap_height(), pix.alpha());
      fmt::print("setup={}, pixmap={}, pixbuf={}, cairo={}, paint={}\n", Dur{t1 - t0}, Dur{t2 - t1},
                 Dur{t3 - t2}, Dur{t4 - t3}, Dur{t5 - t4});
#endif
    };
    draw_area.set_draw_func(draw_op);
#endif

    [[maybe_unused]] auto scale_conn =
      draw_area.property_scale_factor().signal_changed().connect([&] { draw_area.queue_draw(); });
    set_child(draw_area);

    if (path.has_value()) {
      load_pdf(*path);
    }

    Gtk::Image open_icon{};
    open_icon.set_from_icon_name("document-open");

    open_button.set_child(open_icon);
    open_button.set_focusable(false);
    [[maybe_unused]] auto open_conn = open_button.signal_clicked().connect([&]() {
      auto filter_pdf = Gtk::FileFilter::create();
      filter_pdf->set_name("PDF files");
      filter_pdf->add_mime_type("application/pdf");

      auto filters = Gio::ListStore<Gtk::FileFilter>::create();
      filters->append(filter_pdf);

      auto dialog = Gtk::FileDialog::create();
      dialog->set_title("Open PDF");
      dialog->set_filters(filters);
      dialog->set_modal(true);

      dialog->open(*this, [&, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
          auto file = dialog->open_finish(result);
          if (file == nullptr) {
            return;
          }

          std::filesystem::path path{file->get_path()};
          if (!std::filesystem::exists(path)) {
            fmt::print(stderr, "Path {:?} does not exist!\n", path);
          }

          load_pdf(path);
        } catch (const Gtk::DialogError& ex) {
          fmt::print(stderr, "FileDialog failed: {}\n", ex);
        }
      });
    });

    Gtk::HeaderBar bar{};
    bar.pack_end(open_button);
    set_titlebar(bar);

    auto evk = Gtk::EventControllerKey::create();
    [[maybe_unused]] auto evk_conn = evk->signal_key_pressed().connect(
      [&](guint keyval, [[maybe_unused]] guint keycode, [[maybe_unused]] Gdk::ModifierType state) {
        switch (keyval) {
        case GDK_KEY_Right:
        case GDK_KEY_Down:
        case GDK_KEY_Page_Down: {
          navigate_pages(1);
          return true;
        }
        case GDK_KEY_Left:
        case GDK_KEY_Up:
        case GDK_KEY_Page_Up: {
          navigate_pages(-1);
          return true;
        }
        case GDK_KEY_Escape: {
          if (is_fullscreen()) {
            unfullscreen();
          } else {
            close();
          }
          return true;
        }
        case GDK_KEY_F11: {
          if (is_fullscreen()) {
            unfullscreen();
          } else {
            fullscreen();
          }
          return true;
        }
        case GDK_KEY_c: {
          const auto cursor = get_cursor();
          const auto is_none = cursor != nullptr && cursor->get_name() == "none";
          set_cursor(is_none ? "default" : "none");
          return true;
        }
        case GDK_KEY_i: {
          invert = !invert;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_r: {
          if (pdf.has_value()) {
            pdf->reload_doc();
            draw_area.queue_draw();
          }
          return true;
        }
        case GDK_KEY_q: {
          close();
          return true;
        }
        default: break;
        }
        return false;
      },
      true);
    add_controller(evk);
  }

  void load_pdf(std::filesystem::path p) {
    pdf.emplace(std::move(p));
    draw_area.queue_draw();
  }

  void navigate_pages(int direction) {
    if (pdf.has_value()) {
      const auto new_page = pdf->page + direction;
      if (pdf->valid_page(new_page)) {
        pdf->update_page(new_page);
        draw_area.queue_draw();
      }
    }
  }
};

int main(int argc, char* argv[]) {
  if (argc > 2) {
    fmt::print(stderr, "Usage: {} [PDF Path]", argv[0]);
  }

  adw_init();

  auto app = Gtk::Application::create("org.kurbo96.hirgon");

  auto path = (argc > 1) ? std::make_optional<std::filesystem::path>(argv[1]) : std::nullopt;
  return app->make_window_and_run<PDFViewer>(0, nullptr, path);
}
