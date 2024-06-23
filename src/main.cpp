#include <giomm.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>

#include "hirgon/hirgon.hpp"

int main() {
  auto app = Adw::Application::create("org.kurbo96.hirgon", Gio::Application::Flags::DEFAULT_FLAGS);

  app->signal_activate().connect([&]() {
    // The created window is managed. Thus, the C++ wrapper is deleted
    // by Gtk::Object::destroy_notify_() when the C window is destroyed.
    // https://gitlab.gnome.org/GNOME/gtkmm/-/issues/114
    auto* window = Gtk::make_managed<illa::PDFViewer>(*app);
    app->add_window(*window);
    window->present();
  });

  app->signal_window_removed().connect([&](Gtk::Window* window) {
    if (window != nullptr) {
      window->destroy();
    }
  });

  return app->run(0, nullptr);
}
