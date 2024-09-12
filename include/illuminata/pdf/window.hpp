#ifndef INCLUDE_ILLUMINATA_PDF_WINDOW_HPP
#define INCLUDE_ILLUMINATA_PDF_WINDOW_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm.h>
#include <giomm.h>
#include <glib.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>
#include <vector>

#include "illuminata/fmt.hpp"
#include "illuminata/geometry.hpp"
#include "illuminata/mupdf.hpp"
#include "illuminata/pdf/info.hpp"
#include "illuminata/pdf/transform.hpp"

#if ILLUMINATA_OPENGL
#include "illuminata/pdf/opengl.hpp"
#else
#include <cairomm/cairomm.h>
#endif

namespace illa {
template<typename... T>
inline void log([[maybe_unused]] fmt::format_string<T...> fmt, [[maybe_unused]] T&&... args) {
#if ILLUMINATA_PRINT
  fmt::print(fmt, std::forward<T>(args)...);
#endif
}

struct PdfViewer : public Adw::ApplicationWindow {
  struct GeomInfo {
    Dims<int> dims_base;
    Dims<int> dims_scaled;
    int scale;
    float factor;
    mupdf::FzMatrix fzmat;
    Vec2<float> offset;
    mupdf::FzRect rclip;
    mupdf::FzIrect irect;
  };

  using Clock = std::chrono::steady_clock;
  using Dur = std::chrono::duration<double>;

  std::optional<PdfInfo> pdf{};
  bool invert{};

  std::conditional_t<ILLUMINATA_OPENGL, Gtk::GLArea, Gtk::DrawingArea> draw_area{};

  Transform transform{};

#if ILLUMINATA_OPENGL
  OpenGlState ogl{};
#endif

  explicit PdfViewer(Adw::Application& app, std::optional<std::filesystem::path> path = {}) {
    set_title("Illuminata");
    set_icon_name("org.kurbo96.Illuminata");
    set_default_size(800, 600);

    auto dark = app.get_style_manager()->property_dark();
    invert = dark.get_value();
    [[maybe_unused]] auto dark_conn =
      dark.signal_changed().connect([this, dark] { invert = dark.get_value(); });

#if ILLUMINATA_OPENGL
    [[maybe_unused]] auto realize_conn = draw_area.signal_realize().connect([&] {
      draw_area.make_current();
      if (draw_area.has_error()) {
        return;
      }
      ogl.realize();
    });

    [[maybe_unused]] auto unrealize_conn = draw_area.signal_unrealize().connect(
      [&] {
        draw_area.make_current();
        if (draw_area.has_error()) {
          return;
        }
        ogl.unrealize();
      },
      false);

    auto draw_op = [this](const Glib::RefPtr<Gdk::GLContext>& /*ctx*/) {
      if (!pdf.has_value() || !pdf->page_info.has_value()) {
        return false;
      }

      const auto t0 = Clock::now();
      auto geom = compute_geom(draw_area.get_width(), draw_area.get_height());
      const auto t1 = Clock::now();
      mupdf::FzPixmap pix = render(geom);
      const auto t2 = Clock::now();
      ogl.draw(pix, geom.dims_scaled, geom.offset, invert);
      const auto t3 = Clock::now();

      log("{} → {} → {} → {}×{} {}\n", geom.dims_base, geom.dims_scaled, geom.factor, pix.w(),
          pix.h(), pix.alpha());
      log("setup={}, pixmap={}, opengl={}\n", Dur{t1 - t0}, Dur{t2 - t1}, Dur{t3 - t2});

      return true;
    };
    [[maybe_unused]] auto render_conn = draw_area.signal_render().connect(draw_op, true);
#else
    auto draw_op = [&](const std::shared_ptr<Cairo::Context>& ctx, int width, int height) {
      if (!pdf.has_value() || !pdf->page_info.has_value()) {
        return;
      }

      const auto t0 = Clock::now();

      auto geom = compute_geom(width, height);
      ctx->scale(1.0 / geom.scale, 1.0 / geom.scale);
      const auto t1 = Clock::now();
      mupdf::FzPixmap pix = render(geom);
      const auto t2 = Clock::now();
      auto pixbuf = Gdk::Pixbuf::create_from_data(
        pix.samples(), Gdk::Colorspace::RGB, bool(pix.alpha()), 8, pix.w(), pix.h(), pix.stride());
      const auto t3 = Clock::now();
      Gdk::Cairo::set_source_pixbuf(ctx, pixbuf, geom.offset.x, geom.offset.y);
      const auto t4 = Clock::now();
      ctx->paint();
      const auto t5 = Clock::now();

      log("{} → {} → {} → {}×{} {}\n", geom.dims_base, geom.dims_scaled, geom.factor, pix.w(),
          pix.h(), pix.alpha());
      log("setup={}, pixmap={}, pixbuf={}, cairo={}, paint={}\n", Dur{t1 - t0}, Dur{t2 - t1},
          Dur{t3 - t2}, Dur{t4 - t3}, Dur{t5 - t4});
    };
    draw_area.set_draw_func(draw_op);
#endif

    [[maybe_unused]] auto scale_conn =
      draw_area.property_scale_factor().signal_changed().connect([&] { draw_area.queue_draw(); });

    Adw::HeaderBar bar{};

    auto tv = Adw::ToolbarView::create();
    tv->add_top_bar(bar);
    tv->set_content(draw_area);
    tv->set_top_bar_style(Adw::ToolbarView::Style::RAISED);
    [[maybe_unused]] auto conn_extend = property_fullscreened().signal_changed().connect(
      [this, tv] { tv->set_reveal_top_bars(!is_fullscreen()); });
    set_content(*tv);

    if (path.has_value()) {
      load_pdf(*path);
    }

    Gtk::PopoverMenu popover{};
    auto menu = Gio::Menu::create();
    menu->append("Navigation", "win.navigation");
    menu->append("About", "win.about");
    popover.set_menu_model(menu);

    auto kb_action = Gio::SimpleAction::create("navigation");
    kb_action->set_enabled();
    [[maybe_unused]] auto kb_conn =
      kb_action->signal_activate().connect([](const Glib::VariantBase& /*var*/) {
        using Kv = std::pair<const char*, const char*>;
        struct Group {
          const char* name;
          std::vector<Kv> kv{};
        };

        auto* win = Gtk::make_managed<Gtk::ShortcutsWindow>();

        Gtk::ShortcutsSection sec{};
        win->add_section(sec);

        for (const auto& [gname, gkv] : std::vector<Group>{
               {
                 "General",
                 {
                   {"r", "Reload"},
                   {"c", "Toggle Cursor"},
                   {"F11", "Toggle Fullscreen"},
                   {"Escape", "Unfullscreen"},
                   {"q", "Close"},
                 },
               },
               {
                 "Visual Style",
                 {
                   {"i", "Toggle Inverted Brightness"},
                   {"m", "Switch Color Scheme"},
                   {"<Shift>m", "Revert Color Scheme"},
                 },
               },
               {
                 "Page Navigation",
                 {
                   {"<Shift>k Left Up Page_Up", "Previous Page"},
                   {"<Shift>j Down Right Page_Down", "Next Page"},
                 },
               },
               {
                 "On-Page Navigation",
                 {
                   {"j", "Move Up"},
                   {"h", "Move Left"},
                   {"k", "Move Down"},
                   {"l", "Move Right"},
                   {"KP_Add plus", "Zoom In"},
                   {"KP_Subtract minus", "Zoom Out"},
                   {"KP_0 0", "Reset View"},
                 },
               },
             }) {
          Gtk::ShortcutsGroup g{};
          g.property_title().set_value(gname);
          sec.add_group(g);

          for (const auto& [k, v] : gkv) {
            Gtk::ShortcutsShortcut sc{};
            g.add_shortcut(sc);
            sc.property_accelerator().set_value(k);
            sc.property_title().set_value(v);
          }
        }

        win->present();
      });

    auto about_action = Gio::SimpleAction::create("about");
    about_action->set_enabled();
    [[maybe_unused]] auto about_conn =
      about_action->signal_activate().connect([this](const Glib::VariantBase& /*var*/) {
        auto* dialog = Gtk::make_managed<Adw::AboutDialog>();
        dialog->set_application_icon("org.kurbo96.Illuminata");
        dialog->set_application_name("Illuminata");
        dialog->set_developer_name("Kurt Böhm");
        dialog->set_version(ILLUMINATA_VERSION);
        dialog->set_website("https://github.com/KurtBoehm/illuminata");
        dialog->set_copyright("© 2024 Kurt Böhm");

        dialog->set_developers({"Kurt Böhm <kurbo96@gmail.com>"});
        dialog->set_designers({"Kurt Böhm <kurbo96@gmail.com>"});
        dialog->present(this);
      });

    auto group = Gio::SimpleActionGroup::create();
    group->add_action(kb_action);
    group->add_action(about_action);
    insert_action_group("win", group);

    Gtk::MenuButton menu_button{};
    menu_button.set_icon_name("open-menu-symbolic");
    menu_button.set_focusable(false);
    menu_button.set_can_focus(false);
    menu_button.set_menu_model(menu);
    bar.pack_end(menu_button);

    auto* open_button = Gtk::make_managed<Gtk::Button>("Open PDF");
    open_button->set_image_from_icon_name("document-open");
    open_button->set_focusable(false);
    [[maybe_unused]] auto open_conn = open_button->signal_clicked().connect([&]() {
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
    bar.pack_start(*open_button);

    auto evk = Gtk::EventControllerKey::create();
    [[maybe_unused]] auto evk_conn = evk->signal_key_pressed().connect(
      [&](guint keyval, [[maybe_unused]] guint keycode, [[maybe_unused]] Gdk::ModifierType state) {
        const bool is_shift = ((state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{});

        switch (keyval) {
        // General
        case GDK_KEY_r: {
          if (pdf.has_value()) {
            pdf->reload_doc();
            draw_area.queue_draw();
          }
          return true;
        }
        case GDK_KEY_c: {
          const auto cursor = get_cursor();
          const auto is_none = cursor != nullptr && cursor->get_name() == "none";
          set_cursor(is_none ? "default" : "none");
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
        case GDK_KEY_Escape: {
          if (is_fullscreen()) {
            unfullscreen();
          }
          return true;
        }
        case GDK_KEY_q: {
          close();
          return true;
        }
        // Visual Style
        case GDK_KEY_i: {
          invert = !invert;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_m: {
          auto style_manager = app.get_style_manager();
          style_manager->set_color_scheme(style_manager->get_dark() ? Adw::ColorScheme::FORCE_LIGHT
                                                                    : Adw::ColorScheme::FORCE_DARK);
          return true;
        }
        case GDK_KEY_M: {
          auto style_manager = app.get_style_manager();
          style_manager->set_color_scheme(Adw::ColorScheme::DEFAULT);
          return true;
        }
        // Page Navigation
        case GDK_KEY_J:
        case GDK_KEY_Right:
        case GDK_KEY_Down:
        case GDK_KEY_Page_Down: {
          navigate_pages(1);
          transform.reset();
          return true;
        }
        case GDK_KEY_K:
        case GDK_KEY_Left:
        case GDK_KEY_Up:
        case GDK_KEY_Page_Up: {
          navigate_pages(-1);
          transform.reset();
          return true;
        }
        // On-Page Navigation
        case GDK_KEY_j: {
          transform.off.y -= is_shift ? 10.F : 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_h: {
          transform.off.x -= is_shift ? 10.F : 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_k: {
          transform.off.y += is_shift ? 10.F : 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_l: {
          transform.off.x += is_shift ? 10.F : 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_Add:
        case GDK_KEY_plus: {
          transform.scale *= 1.1F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_Subtract:
        case GDK_KEY_minus: {
          transform.scale *= 0.9F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_0:
        case GDK_KEY_0: {
          transform.reset();
          draw_area.queue_draw();
          return true;
        }
        default: break;
        }
        return false;
      },
      true);
    add_controller(evk);

    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_MIDDLE);
    [[maybe_unused]] auto drag_update_conn =
      drag->signal_drag_update().connect([this](double start_x, double start_y) {
        transform.drag_off = Vec2{float(start_x), float(start_y)};
        draw_area.queue_draw();
      });
    [[maybe_unused]] auto drag_end_conn =
      drag->signal_drag_end().connect([this](double start_x, double start_y) {
        transform.off -= Vec2{float(start_x), float(start_y)} / doc_factor();
        transform.drag_off = {0.F};
        draw_area.queue_draw();
      });
    draw_area.add_controller(drag);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    [[maybe_unused]] auto scroll_conn = scroll->signal_scroll().connect(
      [this, scroll](double /*dx*/, double dy) {
        auto event = scroll->get_current_event();
        auto mod = event->get_modifier_state();
        const auto shift =
          (mod & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType::NO_MODIFIER_MASK;
        mod &= ~Gdk::ModifierType::SHIFT_MASK;
        switch (mod) {
        case Gdk::ModifierType::NO_MODIFIER_MASK: {
          transform.off.y += (shift ? 10.F : 1.F) * float(dy);
          draw_area.queue_draw();
          return true;
        }
        case Gdk::ModifierType::CONTROL_MASK: {
          transform.scale *= std::pow(1.F - (shift ? 0.5F : 0.1F), float(dy));
          draw_area.queue_draw();
          return true;
        }
        default: break;
        }
        return false;
      },
      true);
    draw_area.add_controller(scroll);
  }

  float doc_factor(Dims<float> dims, Rect<float> rect) const {
    return std::min(dims.w / rect.w(), dims.h / rect.h()) * transform.scale;
  }
  float doc_factor() const {
    if (!pdf.has_value() || !pdf->page_info.has_value()) {
      return 0.F;
    }

    const Dims dims{draw_area.get_width(), draw_area.get_height()};
    const Rect rect{pdf->page_info->page.fz_bound_page()};
    return doc_factor(Dims<float>(dims), rect);
  }

  void load_pdf(std::filesystem::path p) {
    set_title(fmt::format("Illuminata: {}", p.filename()));
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

  GeomInfo compute_geom(int width, int height) const {
    const Dims dims_base{width, height};
    const auto scale = draw_area.get_scale_factor();

    const Rect rect{pdf->page_info->page.fz_bound_page()};
    const auto f_base = doc_factor(Dims<float>(dims_base), rect);
    const auto f_scaled = f_base * float(scale);

    const auto mat = mupdf::FzMatrix{}.fz_pre_scale(f_scaled, f_scaled);
    const auto trans = transform.document_transform(dims_base, rect, f_base, f_scaled);
    mupdf::FzRect rclip = trans.rclip.fz_rect();

    return GeomInfo{
      .dims_base = dims_base,
      .dims_scaled = dims_base * scale,
      .scale = scale,
      .factor = f_scaled,
      .fzmat = mat,
      .offset = trans.offset,
      .rclip = rclip,
      .irect = rclip.fz_transform_rect(mat).fz_round_rect(),
    };
  }

  mupdf::FzPixmap render(GeomInfo& geom) {
    mupdf::FzPixmap pix{mupdf::FzColorspace::Fixed_RGB, geom.irect, mupdf::FzSeparations{}, 0};
    pix.fz_clear_pixmap_with_value(0xFF);

    mupdf::FzDevice dev{geom.fzmat, pix, geom.irect};
    mupdf::FzCookie cookie{};
    pdf->page_info->display_list.fz_run_display_list(dev, mupdf::FzMatrix{}, geom.rclip, cookie);
    dev.fz_close_device();

    return pix;
  }
};
} // namespace illa

#endif // INCLUDE_ILLUMINATA_PDF_WINDOW_HPP
