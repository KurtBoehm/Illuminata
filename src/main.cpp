#include <filesystem>
#include <optional>
#include <type_traits>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm.h>
#include <giomm.h>
#include <glib.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>

#include "hirgon/hirgon.hpp"

int main(int argc, char* argv[]) {
  if (argc > 2) {
    fmt::print(stderr, "Usage: {} [PDF Path]", argv[0]);
  }

  auto app = Adw::Application::create("org.kurbo96.hirgon", Gio::Application::Flags::DEFAULT_FLAGS);

  auto path = (argc > 1) ? std::make_optional<std::filesystem::path>(argv[1]) : std::nullopt;

  app->signal_activate().connect([&]() {
    // The created window is managed. Thus, the C++ wrapper is deleted
    // by Gtk::Object::destroy_notify_() when the C window is destroyed.
    // https://gitlab.gnome.org/GNOME/gtkmm/-/issues/114
    auto* window = Gtk::make_managed<illa::PDFViewer>(*app, path);
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
