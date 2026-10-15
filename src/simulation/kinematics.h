#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace ra2::motion {
// Integer geometry shared by movement, aiming and projectiles. One cell is
// 1,024,000 units; one lepton is exactly 4,000 units.
inline constexpr std::int64_t scale = 1024;
inline constexpr std::int64_t lepton = 4000;
struct Vector {
  std::int64_t x = 0, y = 0, z = 0;
  auto operator<=>(const Vector&) const = default;
};
Vector operator+(Vector a, Vector b);
Vector operator-(Vector a, Vector b);
std::int64_t sqrt(std::int64_t value);
std::int64_t length(Vector v);
int angleDelta(int from, int to);
int direction(std::int64_t x, std::int64_t y);
Vector unitVector(int angle); // x/y scaled by 65536
Vector rotateFlh(int facing, const std::array<int, 3>& flh);
Vector surfaceFlh(int heading, int facing, const std::array<int,3>& flh, int slopeX, int slopeY);

struct DriveSegment {
  static constexpr int samples = 32;
  // A straight approach followed by a quadratic corner (32 arc samples).
  std::array<Vector, samples + 2> points{};
  std::array<std::int64_t, samples + 2> distances{};
  std::array<int, samples + 2> headings{};
  int startHeading = 0, endHeading = 0;
  void build(Vector from, Vector to);
  void buildCorner(Vector from, Vector corner, Vector after);
  std::int64_t length() const { return distances.back(); }
  Vector at(std::int64_t distance) const;
  int heading(std::int64_t distance) const;
};

struct Launch { Vector velocity; int flight256 = 0; };
// Solve the low ballistic arc in 1/256-frame increments. Velocity is applied
// before gravity, matching the discrete flight integrator below.
std::optional<Launch> ballisticLaunch(Vector delta, int rangeLeptons, int gravityLeptons);
}
