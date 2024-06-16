#ifndef INCLUDE_HIRGON_GEOMETRY_HPP
#define INCLUDE_HIRGON_GEOMETRY_HPP

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <type_traits>

#include "hirgon/fmt.hpp"
#include "hirgon/mupdf.hpp"

template<typename T>
struct Vec2 {
  T x, y;

  Vec2(T v) : x{v}, y{v} {} // NOLINT(hicpp-explicit-conversions)
  Vec2(T x, T y) : x{x}, y{y} {}

  template<typename TF>
  friend Vec2<std::common_type_t<T, TF>> operator*(Vec2 v, TF f) {
    return {v.x * f, v.y * f};
  }
  template<typename TF>
  friend Vec2<std::common_type_t<T, TF>> operator/(Vec2 v, TF f) {
    return {v.x / f, v.y / f};
  }
  template<typename TF>
  friend Vec2<std::common_type_t<T, TF>> operator+(Vec2 v1, Vec2<TF> v2) {
    return {v1.x + v2.x, v1.y + v2.y};
  }
  template<typename TF>
  friend Vec2<std::common_type_t<T, TF>> operator-(Vec2 v1, Vec2<TF> v2) {
    return {v1.x - v2.x, v1.y - v2.y};
  }

  [[nodiscard]] Vec2 max(Vec2 v) const {
    return {std::max(x, v.x), std::max(y, v.y)};
  }
};
template<typename T>
struct fmt::formatter<Vec2<T>> : nested_formatter<T> {
  auto format(Vec2<T> v, format_context& ctx) const {
    return this->write_padded(ctx, [this, v](auto out) {
      return fmt::format_to(out, "({},{})", this->nested(v.x), this->nested(v.y));
    });
  }
};

template<typename T>
struct Dims {
  T w, h;

  template<typename TF>
  friend Dims<std::common_type_t<T, TF>> operator*(Dims d, TF f) {
    return {d.w * f, d.h * f};
  }
  template<typename TF>
  friend Dims<std::common_type_t<T, TF>> operator/(Dims d, TF f) {
    return {d.w / f, d.h / f};
  }

  [[nodiscard]] Vec2<T> center() const {
    return {w / T{2}, h / T{2}};
  }

  template<typename TF>
  explicit operator Dims<TF>() const {
    return {TF(w), TF(h)};
  }
};
template<typename T>
struct fmt::formatter<Dims<T>> : nested_formatter<T> {
  auto format(Dims<T> d, format_context& ctx) const {
    return this->write_padded(ctx, [this, d](auto out) {
      return fmt::format_to(out, "{}×{}", this->nested(d.w), this->nested(d.h));
    });
  }
};

template<typename T>
struct Rect {
  T x_begin, x_end, y_begin, y_end;

  Rect(T x0, T x1, T y0, T y1)
      : x_begin{std::min(x0, x1)}, x_end{std::max(x0, x1)}, y_begin{std::min(y0, y1)},
        y_end{std::max(y0, y1)} {}
  explicit Rect(mupdf::FzRect r) : Rect{r.x0, r.x1, r.y0, r.y1} {}
  Rect(Dims<T> r) : Rect{0, r.w, 0, r.h} {} // NOLINT(hicpp-explicit-conversions)

  [[nodiscard]] Vec2<T> offset() const {
    return {x_begin, y_begin};
  }
  [[nodiscard]] T w() const {
    return x_end - x_begin;
  }
  [[nodiscard]] T h() const {
    return y_end - y_begin;
  }

  [[nodiscard]] Vec2<T> center() const {
    return {(x_begin + x_end) / T{2}, (y_begin + y_end) / T{2}};
  }

  [[nodiscard]] Rect intersect(Rect other) const {
    T x0 = std::max(x_begin, other.x_begin);
    T x1 = std::max(std::min(x_end, other.x_end), x0);
    T y0 = std::max(y_begin, other.y_begin);
    T y1 = std::max(std::min(y_end, other.y_end), y0);
    return {x0, x1, y0, y1};
  }

  [[nodiscard]] mupdf::FzRect fz_rect() const {
    return {x_begin, y_begin, x_end, y_end};
  }

  template<typename TF>
  friend Rect<std::common_type_t<T, TF>> operator+(Rect r, Vec2<TF> v) {
    return {r.x_begin + v.x, r.x_end + v.x, r.y_begin + v.y, r.y_end + v.y};
  }
  template<typename TF>
  friend Rect<std::common_type_t<T, TF>> operator-(Rect r, Vec2<TF> v) {
    return {r.x_begin - v.x, r.x_end - v.x, r.y_begin - v.y, r.y_end - v.y};
  }
  template<typename TF>
  friend Rect<std::common_type_t<T, TF>> operator*(Rect d, TF f) {
    return {d.x_begin * f, d.x_end * f, d.y_begin * f, d.y_end * f};
  }
  template<typename TF>
  friend Rect<std::common_type_t<T, TF>> operator/(Rect d, TF f) {
    return {d.x_begin / f, d.x_end / f, d.y_begin / f, d.y_end / f};
  }
};
Rect(mupdf::FzRect) -> Rect<float>;
template<typename T>
struct fmt::formatter<Rect<T>> : nested_formatter<T> {
  auto format(Rect<T> d, format_context& ctx) const {
    return this->write_padded(ctx, [this, d](auto out) {
      return fmt::format_to(out, "({},{};{},{})", this->nested(d.x_begin), this->nested(d.x_end),
                            this->nested(d.y_begin), this->nested(d.y_end));
    });
  }
};

#endif // INCLUDE_HIRGON_GEOMETRY_HPP
