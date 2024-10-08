#ifndef INCLUDE_ILLUMINATA_PDF_INFO_HPP
#define INCLUDE_ILLUMINATA_PDF_INFO_HPP

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm.h>
#include <giomm.h>
#include <glib.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>

#include "illuminata/mupdf.hpp"

#if ILLUMINATA_PRINT
#include "illuminata/fmt.hpp"
#endif

namespace illa {
// Information about a page in a PDF document relevant for rendering it.
struct PdfPageInfo {
  mupdf::FzPage page;
  mupdf::FzDisplayList display_list;
};

// Information about a PDF document and the page currently opened.
struct PdfInfo {
  std::filesystem::path path;
  mupdf::FzDocument doc;
  int page;
  std::optional<PdfPageInfo> page_info;

  explicit PdfInfo(std::filesystem::path pdf, int pno = 0)
      : path{std::move(pdf)}, doc{path.c_str()}, page{pno} {
#if ILLUMINATA_PRINT
    fmt::print("Open {:?}\n", path);
#endif
    update_page(pno);
  }

  void update_page(int pno) {
    page = pno;
    if (valid_page(pno)) {
#if ILLUMINATA_PRINT
      fmt::print("load page {}\n", pno);
#endif
      mupdf::FzPage p = doc.fz_load_page(pno);
      page_info = PdfPageInfo(p, p.fz_new_display_list_from_page());
    } else {
#if ILLUMINATA_PRINT
      fmt::print("reset page info\n");
#endif
      page_info.reset();
    }
  }

  void reload_doc() {
    doc = mupdf::FzDocument{path.c_str()};
    update_page(std::max(std::min(page, doc.fz_count_pages() - 1), 0));
  }

  [[nodiscard]] bool valid_page(int pno) const {
    return 0 <= pno && pno < doc.fz_count_pages();
  }
};
} // namespace illa

#endif // INCLUDE_ILLUMINATA_PDF_INFO_HPP
