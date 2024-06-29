#ifndef INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP
#define INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP

#include "illuminata/geometry.hpp"

namespace illa {
struct Transform {
  float scale{1.F};
  Vec2<float> off{0.F, 0.F};
  Vec2<float> drag_off{0.F, 0.F};

  struct DocTransform {
    Rect<float> rclip;
    Vec2<float> offset;
  };

  void reset() {
    scale = 1.F;
    off = {0.F, 0.F};
    drag_off = {0.F, 0.F};
  }

  [[nodiscard]] DocTransform document_transform(const Dims<int> dims, const Rect<float> rect,
                                                int scale_factor, float f) const {
    // In document coordinates
    const auto area_dims = dims / f;
    const auto area_center = area_dims.center();
    const auto center = rect.center() + off - drag_off * scale_factor / f;
    const auto view_area = Rect{area_dims} - area_center + center;
    const auto inter = view_area.intersect(rect);
    const auto view_area_min = inter - center + area_center;
    return DocTransform{.rclip = inter, .offset = view_area_min.offset() * f};
  }
};
} // namespace illa

#endif // INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP
