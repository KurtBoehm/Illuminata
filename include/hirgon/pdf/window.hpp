#ifndef INCLUDE_HIRGON_PDF_WINDOW_HPP
#define INCLUDE_HIRGON_PDF_WINDOW_HPP

#include <algorithm>
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

#include "hirgon/fmt.hpp"
#include "hirgon/geometry.hpp"
#include "hirgon/mupdf.hpp"
#include "hirgon/pdf/info.hpp"
#include "hirgon/pdf/opengl.hpp"
#include "hirgon/pdf/transform.hpp"

#if !HIRGON_OPENGL
#include <cairomm/cairomm.h>
#endif
#if HIRGON_PRINT
#include <chrono>
#endif

namespace illa {
struct PDFViewer : public Adw::ApplicationWindow {
  std::optional<PdfInfo> pdf{};
  bool invert{};

  std::conditional_t<HIRGON_OPENGL, Gtk::GLArea, Gtk::DrawingArea> draw_area{};
  Gtk::Button open_button{"Open PDF"};

  Transform trans{};

#if HIRGON_OPENGL
  OpenGlState ogl{};
#endif

  explicit PDFViewer(Adw::Application& app, std::optional<std::filesystem::path> path = {}) {
    set_title("Hirgon");
    set_icon_name("org.kurbo96.Hirgon");
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
      const auto [inter, off] = trans.document_transform(dims, rect, scale_factor, f);

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
      ogl.draw(pix, dims, off, invert);

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
                   {"Escape", "Unfullscreen or Close"},
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
                   {"<Shift>k Page_Up", "Previous Page"},
                   {"<Shift>j Page_Down", "Next Page"},
                 },
               },
               {
                 "On-Page Navigation",
                 {
                   {"j Up", "Move Up"},
                   {"h Left", "Move Left"},
                   {"k Down", "Move Down"},
                   {"l Right", "Move Right"},
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
        dialog->set_application_icon("org.kurbo96.Hirgon");
        dialog->set_application_name("Hirgon");
        dialog->set_developer_name("Kurt Böhm");
        dialog->set_version(HIRGON_VERSION);
        dialog->set_website("https://github.com/KurtBoehm/hirgon");
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

    open_button.set_image_from_icon_name("document-open");
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
          } else {
            close();
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
        case GDK_KEY_Page_Down: {
          navigate_pages(1);
          trans.reset();
          return true;
        }
        case GDK_KEY_K:
        case GDK_KEY_Page_Up: {
          navigate_pages(-1);
          trans.reset();
          return true;
        }
        // On-Page Navigation
        case GDK_KEY_j:
        case GDK_KEY_Up: {
          trans.off.y -= 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_h:
        case GDK_KEY_Left: {
          trans.off.x -= 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_k:
        case GDK_KEY_Down: {
          trans.off.y += 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_l:
        case GDK_KEY_Right: {
          trans.off.x += 1.F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_Add:
        case GDK_KEY_plus: {
          trans.scale *= 1.1F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_Subtract:
        case GDK_KEY_minus: {
          trans.scale *= 0.9F;
          draw_area.queue_draw();
          return true;
        }
        case GDK_KEY_KP_0:
        case GDK_KEY_0: {
          trans.reset();
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
        trans.drag_off = Vec2{float(start_x), float(start_y)};
        draw_area.queue_draw();
      });
    [[maybe_unused]] auto drag_end_conn =
      drag->signal_drag_end().connect([this](double start_x, double start_y) {
        trans.off -= Vec2{float(start_x), float(start_y)} / doc_factor();
        trans.drag_off = {0.F};
        draw_area.queue_draw();
      });
    draw_area.add_controller(drag);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    [[maybe_unused]] auto scroll_conn = scroll->signal_scroll().connect(
      [this, scroll](double /*dx*/, double dy) {
        auto event = scroll->get_current_event();
        switch (event->get_modifier_state()) {
        case Gdk::ModifierType::NO_MODIFIER_MASK: {
          trans.off.y += float(dy);
          draw_area.queue_draw();
          return true;
        }
        case Gdk::ModifierType::CONTROL_MASK: {
          trans.scale *= 1.F - 0.1F * float(dy);
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
    return std::min(dims.w / rect.w(), dims.h / rect.h()) * trans.scale;
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
} // namespace illa

#endif // INCLUDE_HIRGON_PDF_WINDOW_HPP
