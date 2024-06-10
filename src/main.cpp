#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include <libadwaita-1/adwaita.h>

#include "hirgon/mupdf.hpp"

struct MyWindow : public Gtk::Window {
  MyWindow() {
    set_title("Basic application");
  }
};

void new_page(mupdf::PdfDocument& pdf, int pno, float width, float height) {
  mupdf::FzRect mediabox(0, 0, width, height);
  if (pno < -1) {
    return;
  }
  // create /Resources and /Contents objects
  mupdf::PdfObj resources = mupdf::pdf_add_new_dict(pdf, 1);
  mupdf::FzBuffer contents;
  mupdf::PdfObj page_obj = mupdf::pdf_add_page(pdf, mediabox, 0, resources, contents);
  mupdf::pdf_insert_page(pdf, pno, page_obj);
}

int main(int argc, char* argv[]) {
  adw_init();

  auto app = Gtk::Application::create("org.gtkmm.examples.base");

  return app->make_window_and_run<MyWindow>(argc, argv);
}
