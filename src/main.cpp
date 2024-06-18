#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <gdk/gdkkeysyms.h>
#include <gdkmm.h>
#include <giomm.h>
#include <glib.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>

#include "hirgon/hirgon.hpp"

#if HIRGON_OPENGL
#include <array>
#else
#include <cairomm/cairomm.h>
#endif
#if HIRGON_PRINT
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
#if HIRGON_PRINT
    fmt::print("Open {:?}\n", path);
#endif
    update_page(pno);
  }

  void update_page(int pno) {
    page = pno;
    if (valid_page(pno)) {
#if HIRGON_PRINT
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

#if HIRGON_OPENGL
inline constexpr std::array<GLfloat, 12> vertex_data{
  -1.F, +1.F, // vertex 0
  +1.F, -1.F, // vertex 1
  -1.F, -1.F, // vertex 2
  -1.F, +1.F, // vertex 3
  +1.F, +1.F, // vertex 4
  +1.F, -1.F, // vertex 5
};

inline constexpr char vertex_shader_code[] = "#version 320\n"
                                             "\n"
                                             "layout(location = 0) in vec2 position;\n"
                                             "\n"
                                             "void main() {\n"
                                             "  gl_Position = vec4(position, 0.0, 1.0);\n"
                                             "}";

inline constexpr char fragment_shader_code[] =
  "#version 320\n"
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
#endif

struct PDFViewer : public Adw::ApplicationWindow {
  std::optional<PdfInfo> pdf{};
  bool invert{};

  std::conditional_t<HIRGON_OPENGL, Gtk::GLArea, Gtk::DrawingArea> draw_area{};
  Gtk::Button open_button{"Open PDF"};

  float scale_{1.F};
  Vec2<float> off_{0.F, 0.F};
  Vec2<float> drag_off_{0.F, 0.F};

#if HIRGON_OPENGL
  std::optional<gl::Program> prog{};
  std::optional<gl::VertexArray> vtxs{};
  std::optional<gl::Texture> tex{};
  GLint invert_uniform_{};
  GLint area_uniform_{};
  GLint tex_uniform_{};
#endif

  explicit PDFViewer(Adw::Application& app, std::optional<std::filesystem::path> path = {}) {
    set_title("Hirgon");
    set_default_size(800, 600);

    auto dark = app.get_style_manager()->property_dark();
    invert = dark.get_value();
    [[maybe_unused]] auto dark_conn =
      dark.signal_changed().connect([this, dark] { invert = dark.get_value(); });

#if HIRGON_OPENGL
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
#if HIRGON_PRINT
      using Clock = std::chrono::steady_clock;
      using Dur = std::chrono::duration<double>;
      const auto t0 = Clock::now();
#endif

      const auto scale_factor = draw_area.get_scale_factor();
#if HIRGON_OPENGL
      const auto width = draw_area.get_width();
      const auto height = draw_area.get_height();
#else
      ctx->scale(1.0 / scale_factor, 1.0 / scale_factor);
#endif
      const Dims dims_base{width, height};
      const Dims dims = dims_base * scale_factor;

      if (!pdf.has_value() || !pdf->page_info.has_value()) {
#if HIRGON_OPENGL
        return false;
#else
        return;
#endif
      }

      const auto& pinfo = pdf->page_info;
      const Rect rect{pinfo->page.fz_bound_page()};
      const float f = doc_factor(Dims<float>(dims), rect);
      const auto mat = mupdf::FzMatrix{}.fz_pre_scale(f, f);

      // In document coordinates
      const auto area_dims = dims / f;
      const auto area_center = area_dims.center();
      const auto center = rect.center() + off_ - drag_off_ * scale_factor / f;
      const auto view_area = Rect{area_dims} - area_center + center;
      const auto inter = view_area.intersect(rect);
      const auto view_area_min = inter - center + area_center;
      const auto off = view_area_min.offset() * f;

      auto rclip = inter.fz_rect();
      auto irect = rclip.fz_transform_rect(mat).fz_round_rect();

#if HIRGON_PRINT
      const auto t1 = Clock::now();
#endif

      mupdf::FzPixmap pix{mupdf::FzColorspace::Fixed_RGB, irect, mupdf::FzSeparations{}, 0};
      pix.fz_clear_pixmap_with_value(0xFF);
      {
        mupdf::FzDevice dev{mat, pix, irect};
        mupdf::FzCookie cookie{};
        pinfo->display_list.fz_run_display_list(dev, mupdf::FzMatrix{}, rclip, cookie);
        dev.fz_close_device();
      }

#if HIRGON_PRINT
      const auto t2 = Clock::now();
#endif

#if HIRGON_OPENGL
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
#if HIRGON_PRINT
          fmt::print("load: {}×{}×{}\n", pix.w(), pix.h(), pix.s());
#endif
          tx.load(pix.samples(), pix.w(), pix.h(), gl::PixelFormat::RGB);
          tu.set_uniform(tex_uniform_);
        }

        {
          glUniform1i(invert_uniform_, static_cast<GLint>(invert));
          std::array<GLint, 4> arr{
            GLint(std::round(off.x)),
            dims.h - GLint(std::round(off.y)),
            pix.w(),
            pix.h(),
          };
          glUniform1iv(area_uniform_, arr.size(), arr.data());
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, vertex_data.size());
        glDisableVertexAttribArray(0);
      }
      glFlush();

#if HIRGON_PRINT
      const auto t3 = Clock::now();
      fmt::print("{} → {} → {} → {}×{} {}\n", dims_base, dims, f, pix.w(), pix.h(), pix.alpha());
      fmt::print("setup={}, pixmap={}, opengl={}\n", Dur{t1 - t0}, Dur{t2 - t1}, Dur{t3 - t2});
#endif

      return true;
    };
    [[maybe_unused]] auto render_conn = draw_area.signal_render().connect(draw_op, true);
#else
      auto pixbuf = Gdk::Pixbuf::create_from_data(
        pix.samples(), Gdk::Colorspace::RGB, bool(pix.alpha()), 8, pix.w(), pix.h(), pix.stride());

#if HIRGON_PRINT
      const auto t3 = Clock::now();
#endif

      Gdk::Cairo::set_source_pixbuf(ctx, pixbuf, off.x, off.y);

#if HIRGON_PRINT
      const auto t4 = Clock::now();
#endif

      ctx->paint();

#if HIRGON_PRINT
      const auto t5 = Clock::now();
      fmt::print("{} → {} → {} → {}×{} {}\n", dims_base, dims, f, pix.w(), pix.h(), pix.alpha());
      fmt::print("setup={}, pixmap={}, pixbuf={}, cairo={}, paint={}\n", Dur{t1 - t0}, Dur{t2 - t1},
                 Dur{t3 - t2}, Dur{t4 - t3}, Dur{t5 - t4});
#endif
    };
    draw_area.set_draw_func(draw_op);
#endif

    [[maybe_unused]] auto scale_conn =
      draw_area.property_scale_factor().signal_changed().connect([&] { draw_area.queue_draw(); });

    Adw::HeaderBar bar{};

    auto tv = Adw::ToolbarView::create();
    tv->add_top_bar(bar);
    tv->set_content(draw_area);
    tv->set_top_bar_style(Adw::ToolbarStyle::RAISED);
    [[maybe_unused]] auto conn_extend = property_fullscreened().signal_changed().connect(
      [this, tv] { tv->set_reveal_top_bars(!is_fullscreen()); });
    set_content(*tv);

    if (path.has_value()) {
      load_pdf(*path);
    }

    Gtk::PopoverMenu popover{};

    auto menu = Gio::Menu::create();
    auto item0 = Gio::MenuItem::create("a", Glib::ustring{});
    item0->set_action("win.item0");
    menu->append_item(item0);
    menu->append("B");
    popover.set_menu_model(menu);

    auto action0 = Gio::SimpleAction::create_bool("item0", true);
    action0->set_enabled();
    [[maybe_unused]] auto action0_conn =
      action0->signal_activate().connect([action0](const Glib::VariantBase& /*var*/) {
        bool state{};
        action0->get_state(state);
        action0->change_state(!state);
      });
    auto group = Gio::SimpleActionGroup::create();
    group->add_action(action0);
    this->insert_action_group("win", group);

    Gtk::MenuButton menu_button{};
    menu_button.set_icon_name("open-menu-symbolic");
    menu_button.set_menu_model(menu);
    bar.pack_end(menu_button);

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
    bar.pack_start(open_button);

    auto evk = Gtk::EventControllerKey::create();
    [[maybe_unused]] auto evk_conn = evk->signal_key_pressed().connect(
      [&](guint keyval, [[maybe_unused]] guint keycode, [[maybe_unused]] Gdk::ModifierType state) {
        switch (keyval) {
        case GDK_KEY_w: {
          off_.y -= 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_a: {
          off_.x -= 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_s: {
          off_.y += 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_d: {
          off_.x += 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_m: {
          auto style_manager = app.get_style_manager();
          style_manager->set_color_scheme(style_manager->get_dark() ? Adw::ColorScheme::FORCE_LIGHT
                                                                    : Adw::ColorScheme::FORCE_DARK);
          return true;
        }
        case GDK_KEY_Right:
        case GDK_KEY_Down:
        case GDK_KEY_Page_Down: {
          navigate_pages(1);
          reset_transform();
          return true;
        }
        case GDK_KEY_Left:
        case GDK_KEY_Up:
        case GDK_KEY_Page_Up: {
          navigate_pages(-1);
          reset_transform();
          return true;
        }
        case GDK_KEY_KP_Add:
        case GDK_KEY_plus: {
          scale_ *= 1.1F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_Subtract:
        case GDK_KEY_minus: {
          scale_ *= 0.9F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_0:
        case GDK_KEY_0: {
          reset_transform();
          draw_area.queue_draw();
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

    auto drag = Gtk::GestureDrag::create();
    [[maybe_unused]] auto drag_update_conn =
      drag->signal_drag_update().connect([this](double start_x, double start_y) {
        drag_off_ = Vec2{float(start_x), float(start_y)};
        draw_area.queue_draw();
      });
    [[maybe_unused]] auto drag_end_conn =
      drag->signal_drag_end().connect([this](double start_x, double start_y) {
        off_ = off_ - Vec2{float(start_x), float(start_y)} / doc_factor();
        drag_off_ = {0.F};
        draw_area.queue_draw();
      });
    draw_area.add_controller(drag);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    [[maybe_unused]] auto scroll_conn = scroll->signal_scroll().connect(
      [this](double /*dx*/, double dy) {
        scale_ *= 1.F - 0.1F * float(dy);
        draw_area.queue_draw();
        return true;
      },
      true);
    draw_area.add_controller(scroll);
  }

  float doc_factor(Dims<float> dims, Rect<float> rect) const {
    return std::min(dims.w / rect.w(), dims.h / rect.h()) * scale_;
  }
  float doc_factor() const {
    if (!pdf.has_value() || !pdf->page_info.has_value()) {
      return 0.F;
    }

    const Dims dims_base{draw_area.get_width(), draw_area.get_height()};
    const Dims dims = dims_base;
    const auto& pinfo = pdf->page_info;
    const Rect rect{pinfo->page.fz_bound_page()};

    return doc_factor(Dims<float>(dims), rect);
  }

  void reset_transform() {
    scale_ = 1.F;
    off_ = {0.F, 0.F};
    drag_off_ = {0.F, 0.F};
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

  auto app = Adw::Application::create("org.kurbo96.hirgon", Gio::Application::Flags::DEFAULT_FLAGS);

  auto path = (argc > 1) ? std::make_optional<std::filesystem::path>(argv[1]) : std::nullopt;
  return app->make_window_and_run<PDFViewer>(0, nullptr, *app, path);
}
