#ifndef INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP
#define INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP

#include "illuminata/geometry.hpp"

namespace illa {
struct DocTransform {
  Rect<float> rclip;
  Vec2<float> offset;
};

struct Transform {
  float scale{1.F};
  // Offset (document coordinates).
  Vec2<float> off{0.F, 0.F};
  // Offset due to dragging (unscaled screen coordinates).
  Vec2<float> drag_off{0.F, 0.F};

  void reset() {
    scale = 1.F;
    off = {0.F, 0.F};
    drag_off = {0.F, 0.F};
  }

  // `dims_base`: View dimensions (unscaled view coordinates).
  // `rect`: PDF page bounds (document coordinates).
  // `f_base`: Scaling factor from document to unscaled view coordinates.
  // `f_scaled`: Scaling factor from document to scaled view coordinates.
  [[nodiscard]] DocTransform document_transform(const Dims<int> dims_base, const Rect<float> rect,
                                                float f_base, float f_scaled) const {
    // View dimensions (document coordinates).
    const Dims area_dims = dims_base / f_base;
    // Center of the view (starting at the origin, document coordinates).
    const Vec2 area_center = area_dims.center();
    // Center of the PDF page after applying the offset (document coordinates).
    const Vec2 center = rect.center() + off - drag_off / f_base;
    // The vector from area_center to center (document coordinates).
    const Vec2 center_off = center - area_center;
    // View area centered at the offset page center (document coordinates).
    const Rect view_area = Rect{area_dims} + center_off;
    // The part of the PDF page that is visible in the view (document coordinates).
    const Rect inter = view_area.intersect(rect);
    // The offset of the visible area from the origin (scaled view coordinates).
    const Vec2 view_area_off = (inter - center_off).offset() * f_scaled;
    return DocTransform{.rclip = inter, .offset = view_area_off};
  }
};
} // namespace illa

#endif // INCLUDE_ILLUMINATA_PDF_TRANSFORM_HPP
