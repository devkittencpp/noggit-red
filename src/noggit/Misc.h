// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once
#include <math/trig.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <typeinfo>
#include <variant>
#include <vector>
#include <algorithm>  // Required for std::max_element

namespace util
{
  class sExtendableArray;
}

// namespace for static helper functions.


//This can be moved somewhere lated, replaces Boost::variant::type() which returnes typeid of active indexed variant
//https://stackoverflow.com/questions/53696720/get-currently-held-typeid-of-stdvariant-like-boostvariant-type
template<class V>
std::type_info const& var_type(V const& v) {
    return std::visit([](auto&& x)->decltype(auto) { return typeid(x); }, v);
}

namespace misc
{
  
  void find_and_replace(std::string& source, const std::string& find, const std::string& replace);
  float frand();
  float randfloat(float lower, float upper);
  int randint(int lower, int upper);
  float dist(float x1, float z1, float x2, float z2);
  float dist(glm::vec3 const& p1, glm::vec3 const& p2);
  float getShortestDist(float x, float z, float squareX, float squareZ, float unitSize);
  float getShortestDist(glm::vec3 const& pos, glm::vec3 const& square_pos, float unitSize);
  bool square_is_in_circle(float x, float z, float radius, float square_x, float square_z, float square_size);
  bool rectOverlap(glm::vec3 const*, glm::vec3 const*);
  // used for angled tools, get the height a point (pos) should be given an origin, angle and orientation
  float angledHeight(glm::vec3 const& origin, glm::vec3 const& pos, math::radians const& angle, math::radians const& orientation);
  void extract_v3d_min_max(glm::vec3 const& point, glm::vec3& min, glm::vec3& max);
  std::vector<glm::vec3> intersection_points(glm::vec3 const& vmin, glm::vec3 const& vmax);  
  glm::vec3 transform_model_box_coords(glm::vec3 const& pos);
  // normalize the filename used in adts since TC extractors don't accept /
  std::string normalize_adt_filename(std::string filename);

  // see http://realtimecollisiondetection.net/blog/?p=89 for more info
  bool float_equals(float const& a, float const& b);

  bool vec3d_equals(glm::vec3 const& v1, glm::vec3 const& v2);
  bool deg_vec3d_equals(math::degrees::vec3 const& v1, math::degrees::vec3 const& v2);

  glm::vec4 projectPointToScreen(const glm::vec3& point, const glm::mat4& VPmatrix, float viewport_width, float viewport_height, bool& valid);

  std::array<glm::vec2, 2> getBoundingBoxScreenBounds(const std::array<glm::vec3, 2>& local_extents
                                              , const glm::mat4& VPmatrix
                                              , float viewport_width
                                              , float viewport_height
                                              , int& valid_points
                                              , glm::mat4x4 const& obj_matrix
                                              , float scale = 1.0f);

  bool pointInside(glm::vec3 point, std::array<glm::vec3, 2> const& extents);
  bool pointInside(glm::vec2 point, std::array<glm::vec2, 2> const& extents);

  glm::vec4 normalized_device_coords(int x, int y, int screen_width, int screen_height);

  void minmax(glm::vec3* a, glm::vec3* b);

  int rounded_int_div(int value, int div);
  int rounded_255_int_div(int value);

  // treat the value as an 8x8 array of bit
  void set_bit(std::uint64_t& value, int x, int y, bool on);
  void bit_or(std::uint64_t& value, int x, int y, bool on);

  struct random_color : glm::vec4
  {
    random_color()
      : glm::vec4( misc::randfloat(0.0f, 1.0f)
                        , misc::randfloat(0.0f, 1.0f)
                        , misc::randfloat(0.0f, 1.0f)
                        , 0.7f
                        )
    {}
  };

  template<typename Range>
    constexpr std::size_t max_element_index (Range const& range)
  {
    return std::distance (range.begin(), std::max_element (range.begin(), range.end()));
  }

  template<typename T, std::size_t Capacity>
    struct max_capacity_stack_vector
  {
    max_capacity_stack_vector (std::size_t size, T init = T()) : _size (size)
    {
      std::fill (begin(), end(), init);
    }

    T const* begin() const { return _data; } T* begin() { return _data; }
    T const* end() const { return _data + _size; } T* end() { return _data + _size; }
    T& operator[] (std::size_t i) { return _data[i]; }

  private:
    T _data[Capacity];
    std::size_t const _size;
  };
}

//! \todo collect all lose functions/classes/structs for now, sort them later

struct sChunkHeader
{
  int mMagic;
  int mSize;
};

void SetChunkHeader(util::sExtendableArray& pArray, int pPosition, int pMagix, int pSize = 0);

