#include <filesystem>
#include <optional>

#include <giomm.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <libadwaitamm.h>

#include "hirgon/hirgon.hpp"

int main(int argc, char* argv[]) {
  if (argc > 2) {
    fmt::print(stderr, "Usage: {} [PDF Path]", argv[0]);
  }
  auto path = (argc > 1) ? std::make_optional<std::filesystem::path>(argv[1]) : std::nullopt;

  auto app = Adw::Application::create("org.kurbo96.hirgon", Gio::Application::Flags::NON_UNIQUE);
  return app->make_window_and_run<illa::PDFViewer>(0, nullptr, *app, path);
}
