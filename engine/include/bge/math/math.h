// BGE Revived - core math types.
// Column-major matrices, right-handed coordinate system, Y-up (glTF convention).
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bge {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;

inline constexpr float radians(float degrees) { return degrees * DEG2RAD; }
inline constexpr float degrees(float radians) { return radians * RAD2DEG; }
inline constexpr float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline constexpr float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline constexpr float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
template <typename T> inline constexpr T minv(T a, T b) { return a < b ? a : b; }
template <typename T> inline constexpr T maxv(T a, T b) { return a > b ? a : b; }

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

inline constexpr float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float length(const Vec2& v) { return std::sqrt(dot(v, v)); }
inline Vec2 normalize(const Vec2& v) { float l = length(v); return l > 1e-8f ? v / l : Vec2{}; }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}

    constexpr float& operator[](int i) { return (&x)[i]; }
    constexpr const float& operator[](int i) const { return (&x)[i]; }

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline constexpr float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float length_sq(const Vec3& v) { return dot(v, v); }
inline Vec3 normalize(const Vec3& v) { float l = length(v); return l > 1e-8f ? v / l : Vec3{}; }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 min(const Vec3& a, const Vec3& b) { return {minv(a.x, b.x), minv(a.y, b.y), minv(a.z, b.z)}; }
inline Vec3 max(const Vec3& a, const Vec3& b) { return {maxv(a.x, b.x), maxv(a.y, b.y), maxv(a.z, b.z)}; }
inline Vec3 abs(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    explicit constexpr Vec4(float s) : x(s), y(s), z(s), w(s) {}
    constexpr Vec3 xyz() const { return {x, y, z}; }
    constexpr Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr Vec4 operator-() const { return {-x, -y, -z, -w}; }
};

inline constexpr float dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Vec4 normalize(const Vec4& v) { float l = std::sqrt(dot(v, v)); return l > 1e-8f ? v * (1.0f / l) : Vec4{}; }

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quat identity() { return {0, 0, 0, 1}; }

    static Quat from_axis_angle(const Vec3& axis, float angle) {
        Vec3 a = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }
    // Intrinsic rotation order X, then Y, then Z (matches Euler XYZ).
    static Quat from_euler(const Vec3& e) {
        float cx = std::cos(e.x * 0.5f), sx = std::sin(e.x * 0.5f);
        float cy = std::cos(e.y * 0.5f), sy = std::sin(e.y * 0.5f);
        float cz = std::cos(e.z * 0.5f), sz = std::sin(e.z * 0.5f);
        return {sx * cy * cz + cx * sy * sz,
                cx * sy * cz - sx * cy * sz,
                cx * cy * sz + sx * sy * cz,
                cx * cy * cz - sx * sy * sz};
    }
    Vec3 to_euler() const {
        // XYZ intrinsic
        Vec3 e;
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        e.x = std::atan2(sinr_cosp, cosr_cosp);
        float sinp = 2.0f * (w * y - z * x);
        e.y = std::fabs(sinp) >= 1.0f ? std::copysign(HALF_PI, sinp) : std::asin(sinp);
        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        e.z = std::atan2(siny_cosp, cosy_cosp);
        return e;
    }

    constexpr Quat operator*(const Quat& o) const {
        return {w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w,
                w * o.w - x * o.x - y * o.y - z * o.z};
    }
    Quat& operator*=(const Quat& o) { *this = *this * o; return *this; }
    Vec3 rotate(const Vec3& v) const {
        Vec3 u{x, y, z};
        float s = w;
        return u * (2.0f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.0f * s);
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat normalized() const {
        float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-8f ? Quat{x / l, y / l, z / l, w / l} : identity();
    }
};

inline Quat slerp(const Quat& a, const Quat& b, float t) {
    Quat q1 = a, q2 = b;
    float d = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    if (d < 0.0f) { q2 = {-q2.x, -q2.y, -q2.z, -q2.w}; d = -d; }
    if (d > 0.9995f) {
        return Quat{lerpf(a.x, q2.x, t), lerpf(a.y, q2.y, t), lerpf(a.z, q2.z, t), lerpf(a.w, q2.w, t)}.normalized();
    }
    float theta0 = std::acos(clampf(d, -1.0f, 1.0f));
    float theta = theta0 * t;
    float sin_theta = std::sin(theta);
    float sin_theta0 = std::sin(theta0);
    float s0 = std::cos(theta) - d * sin_theta / sin_theta0;
    float s1 = sin_theta / sin_theta0;
    return {q1.x * s0 + q2.x * s1, q1.y * s0 + q2.y * s1, q1.z * s0 + q2.z * s1, q1.w * s0 + q2.w * s1};
}

struct Mat4 {
    // Column-major storage: m[column * 4 + row].
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    constexpr Mat4() = default;
    constexpr Mat4(float diagonal) {
        m[0] = m[5] = m[10] = m[15] = diagonal;
    }

    static constexpr Mat4 identity() { return Mat4(1.0f); }
    static constexpr Mat4 zero() { return Mat4(0.0f); }

    static constexpr Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static constexpr Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Mat4 rotation_x(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotation_y(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotation_z(float angle) {
        Mat4 r;
        float c = std::cos(angle), s = std::sin(angle);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }
    static Mat4 from_quat(const Quat& q) {
        Mat4 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz); r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz); r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy); r.m[9] = 2 * (yz - wx); r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }
    static Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s) {
        Mat4 r = from_quat(q);
        r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
        r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
        r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[12] = -dot(s, eye);
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z; r.m[13] = -dot(u, eye);
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = dot(f, eye);
        r.m[3] = 0; r.m[7] = 0; r.m[11] = 0; r.m[15] = 1;
        return r;
    }
    static Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
        Mat4 r = Mat4::zero();
        float f = 1.0f / std::tan(fov_y * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
        return r;
    }
    static Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (far_plane - near_plane);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
        return r;
    }

    constexpr Mat4 operator*(const Mat4& o) const {
        Mat4 r = Mat4::zero();
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }
    Mat4& operator*=(const Mat4& o) { *this = *this * o; return *this; }

    Vec4 operator*(const Vec4& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }
    Vec3 transform_point(const Vec3& p) const {
        Vec4 r = (*this) * Vec4(p, 1.0f);
        return {r.x, r.y, r.z};
    }
    Vec3 transform_vector(const Vec3& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z,
                m[1] * v.x + m[5] * v.y + m[9] * v.z,
                m[2] * v.x + m[6] * v.y + m[10] * v.z};
    }
    Vec3 translation() const { return {m[12], m[13], m[14]}; }
    Vec3 column(int i) const { return {m[i * 4], m[i * 4 + 1], m[i * 4 + 2]}; }

    Mat4 transposed() const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) r.m[c * 4 + row] = m[row * 4 + c];
        return r;
    }

    // General 4x4 inverse (cofactor expansion).
    Mat4 inverse() const {
        const float* a = m;
        float inv[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::fabs(det) < 1e-12f) return Mat4::identity();
        float inv_det = 1.0f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * inv_det;
        return r;
    }

    // Normal matrix (inverse-transpose of upper-left 3x3), returned as mat3 columns in a Mat4.
    Mat4 normal_matrix() const {
        Mat4 upper;
        upper.m[0] = m[0]; upper.m[1] = m[1]; upper.m[2] = m[2];
        upper.m[4] = m[4]; upper.m[5] = m[5]; upper.m[6] = m[6];
        upper.m[8] = m[8]; upper.m[9] = m[9]; upper.m[10] = m[10];
        return upper.inverse().transposed();
    }
};

inline Quat quat_from_rotation(const Mat4& m) {
    // m must contain a pure rotation in its upper-left 3x3 block.
    float m00 = m.m[0], m01 = m.m[4], m02 = m.m[8];
    float m10 = m.m[1], m11 = m.m[5], m12 = m.m[9];
    float m20 = m.m[2], m21 = m.m[6], m22 = m.m[10];
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

// Decompose a TRS matrix. Mirrored matrices keep the reflection in the X scale;
// importers should reverse triangle winding when the determinant is negative.
inline void decompose_trs(const Mat4& m, Vec3& translation, Quat& rotation, Vec3& scale) {
    translation = m.translation();
    Vec3 c0 = m.column(0);
    Vec3 c1 = m.column(1);
    Vec3 c2 = m.column(2);
    float sx = length(c0);
    float sy = length(c1);
    float sz = length(c2);
    if (dot(cross(c0, c1), c2) < 0.0f) sx = -sx;
    scale = {sx, sy, sz};

    Mat4 r = Mat4::identity();
    Vec3 r0 = sx != 0.0f ? c0 / sx : Vec3{1, 0, 0};
    Vec3 r1 = sy != 0.0f ? c1 / sy : Vec3{0, 1, 0};
    Vec3 r2 = sz != 0.0f ? c2 / sz : Vec3{0, 0, 1};
    r.m[0] = r0.x; r.m[1] = r0.y; r.m[2] = r0.z;
    r.m[4] = r1.x; r.m[5] = r1.y; r.m[6] = r1.z;
    r.m[8] = r2.x; r.m[9] = r2.y; r.m[10] = r2.z;
    rotation = quat_from_rotation(r);
}

struct AABB {
    Vec3 min{1e30f, 1e30f, 1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    void expand(const Vec3& p) { min = bge::min(min, p); max = bge::max(max, p); }
    void expand(const AABB& b) { min = bge::min(min, b.min); max = bge::max(max, b.max); }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    AABB transformed(const Mat4& m) const {
        AABB out;
        for (int i = 0; i < 8; ++i) {
            Vec3 p{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
            out.expand(m.transform_point(p));
        }
        return out;
    }
};

struct Plane {
    Vec3 normal{0, 1, 0};
    float d = 0.0f;
    float distance(const Vec3& p) const { return dot(normal, p) + d; }
};

struct Frustum {
    Plane planes[6];

    static Frustum from_matrix(const Mat4& m) {
        Frustum f;
        auto make_plane = [](float a, float b, float c, float d) {
            Plane p{Vec3{a, b, c}, d};
            float len = length(p.normal);
            if (len > 1e-8f) { p.normal /= len; p.d /= len; }
            return p;
        };
        // row3 +/- rowN of the column-major matrix
        f.planes[0] = make_plane(m.m[3] + m.m[0], m.m[7] + m.m[4], m.m[11] + m.m[8], m.m[15] + m.m[12]);   // left
        f.planes[1] = make_plane(m.m[3] - m.m[0], m.m[7] - m.m[4], m.m[11] - m.m[8], m.m[15] - m.m[12]);   // right
        f.planes[2] = make_plane(m.m[3] + m.m[1], m.m[7] + m.m[5], m.m[11] + m.m[9], m.m[15] + m.m[13]);   // bottom
        f.planes[3] = make_plane(m.m[3] - m.m[1], m.m[7] - m.m[5], m.m[11] - m.m[9], m.m[15] - m.m[13]);   // top
        f.planes[4] = make_plane(m.m[3] + m.m[2], m.m[7] + m.m[6], m.m[11] + m.m[10], m.m[15] + m.m[14]);  // near
        f.planes[5] = make_plane(m.m[3] - m.m[2], m.m[7] - m.m[6], m.m[11] - m.m[10], m.m[15] - m.m[14]);  // far
        return f;
    }

    bool intersects(const AABB& box) const {
        for (const Plane& p : planes) {
            Vec3 positive{(p.normal.x >= 0) ? box.max.x : box.min.x,
                          (p.normal.y >= 0) ? box.max.y : box.min.y,
                          (p.normal.z >= 0) ? box.max.z : box.min.z};
            if (p.distance(positive) < 0.0f) return false;
        }
        return true;
    }
};

} // namespace bge
