#include <algorithm>
#include <filesystem>
#include <memory>
#include <mupdf/classes.h>
#include <optional>
#include <utility>

#include <cairomm/context.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm/enums.h>
#include <glib.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/window.h>
#include <libadwaita-1/adwaita.h>

#include "gdkmm/general.h"
#include "gdkmm/pixbuf.h"
#include "giomm/asyncresult.h"
#include "giomm/listmodel.h"
#include "giomm/liststore.h"
#include "glibmm/refptr.h"
#include "gtkmm/error.h"
#include "gtkmm/filedialog.h"
#include "gtkmm/filefilter.h"
#include "hirgon/mupdf.hpp"

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
    fmt::print("Open {:?}\n", path);
    update_page(pno);
  }

  void update_page(int pno) {
    page = pno;
    if (valid_page(pno)) {
      fmt::print("load page {}\n", pno);
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

struct PDFViewer : public Gtk::ApplicationWindow {
  std::optional<PdfInfo> pdf{};

  Gtk::DrawingArea drawing_area{};
  Gtk::Button open_button{"Open PDF"};

  explicit PDFViewer(std::optional<std::filesystem::path> path = {}) {
    set_title("Hirgon");

    drawing_area.set_draw_func([&](std::shared_ptr<Cairo::Context> ctx, int width, int height) {
      const auto scale_factor = get_scale_factor();
      ctx->scale(1.0 / scale_factor, 1.0 / scale_factor);
      const auto w = width * scale_factor;
      const auto h = height * scale_factor;

      if (!pdf.has_value() || !pdf->page_info.has_value()) {
        return;
      }

      const auto& pinfo = pdf->page_info;
      const auto rect = pinfo->page.fz_bound_page();
      const auto f = std::min(float(w) / std::max(0.F, rect.x1 - rect.x0),
                              float(h) / std::max(0.F, rect.y1 - rect.y0));

      mupdf::FzPixmap pix{
        pinfo->display_list,
        mupdf::FzMatrix{}.fz_pre_scale(f, f),
        mupdf::FzColorspace::Fixed_RGB,
        0,
      };
      fmt::print("{}×{} → {}×{} → {} → {}×{} {}\n", width, height, w, h, f, pix.fz_pixmap_width(),
                 pix.fz_pixmap_height(), pix.alpha());

      auto pixbuf =
        Gdk::Pixbuf::create_from_data(pix.samples(), Gdk::Colorspace::RGB, bool(pix.alpha()), 8,
                                      pix.fz_pixmap_width(), pix.fz_pixmap_height(), pix.stride());
      Gdk::Cairo::set_source_pixbuf(ctx, pixbuf, float(w - pix.w()) / 2.F,
                                    float(h - pix.h()) / 2.F);
      ctx->paint();
    });
    set_child(drawing_area);

    if (path.has_value()) {
      load_pdf(*path);
    }

    Gtk::Image open_icon{};
    open_icon.set_from_icon_name("document-open");

    open_button.set_child(open_icon);
    open_button.set_focusable(false);
    [[maybe_unused]] auto open_conn = open_button.signal_clicked().connect([&]() {
      fmt::print("click!\n");

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
            fmt::print("Path {:?} does not exist!\n", path);
          }

          load_pdf(path);
        } catch (const Gtk::DialogError& ex) {
          fmt::print("FileDialog failed: {}\n", ex);
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
        case GDK_KEY_r: {
          if (pdf.has_value()) {
            pdf->reload_doc();
            drawing_area.queue_draw();
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
    drawing_area.queue_draw();
  }

  void navigate_pages(int direction) {
    if (pdf.has_value()) {
      const auto new_page = pdf->page + direction;
      if (pdf->valid_page(new_page)) {
        pdf->update_page(new_page);
        drawing_area.queue_draw();
      }
    }
  }
};

void getter(mupdf::PdfDocument& pdf, int pno, float width, float height, PdfPageInfo info) {
  mupdf::FzPixmap pix{
    info.display_list,
    mupdf::FzMatrix{}.fz_pre_scale(1, 1),
    mupdf::FzColorspace::Fixed_RGB,
    0,
  };
  pix.samples();
}

int main(int argc, char* argv[]) {
  adw_init();

  auto app = Gtk::Application::create("org.kurbo96.hirgon");

  return app->make_window_and_run<PDFViewer>(
    argc, argv, std::filesystem::path{"/home/gildor/projects/sci-cpp-exercises/build/sheet04.pdf"});
}
