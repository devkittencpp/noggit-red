// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <math/bounding_box.hpp>
#include <math/ray.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/Log.h>
#include <noggit/Model.h>
#include <noggit/Particle.h>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/scoped_blp_texture_reference.hpp>
#include <noggit/TextureManager.h> // TextureManager, Texture

#include <cassert>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <string>
#include <cstring>  // Required for memcpy

Model::Model(const std::string& filename, Noggit::NoggitRenderContext context)
  : AsyncObject(filename)
  , _context(context)
  , _renderer(this)
{
  // memset(&header, 0, sizeof(ModelHeader));
}

void Model::finishLoading()
{
  BlizzardArchive::ClientFile f(_file_key.filepath(), Noggit::Application::NoggitApplication::instance()->clientData());

  if (f.isEof() || f.getSize() < sizeof(ModelHeader))
  {
    // LogError << "Error loading file \"" << _file_key.stringRepr() << "\". Aborting to load model." << std::endl;
    // finished = true;
    throw std::runtime_error("Error loading file \"" + _file_key.stringRepr() + "\". Aborting to load model.");
  }

  ModelHeader header;

  memcpy(&header, f.getBuffer(), sizeof(ModelHeader));



  uint32_t packed_version = 0;
  std::memcpy(&packed_version, header.version, sizeof(packed_version));

  bool valid_version = false;

  // Noggit::Application::NoggitApplication::instance()->clientData()->version()// either should work
  switch (Noggit::Project::CurrentProject::get()->projectVersion )
  {
  case Noggit::Project::ProjectVersion::WOTLK:
    if (packed_version == m2_version_wrath)
      valid_version = true;
    break;
  case Noggit::Project::ProjectVersion::SL:
    if (packed_version == m2_version_legion_bfa_sl)
      valid_version = true;
    break;
  default:
    assert(false);
  }

  if (!valid_version)
  [[unlikely]]
  {
    LogError << "Error loading file \"" << _file_key.stringRepr() << "\". Wrong M2 version " << std::to_string(packed_version) << std::endl;
    throw std::runtime_error("Error loading file \"" + _file_key.stringRepr() + "\". Wrong M2 version " + std::to_string(packed_version));
  }

  // blend mode override
  if (header.Flags & m2_flag_use_texture_combiner_combos)
  {
    // go to the end of the header (where the blend override data is)    
    uint32_t const* blend_override_info = reinterpret_cast<uint32_t const*>(f.getBuffer() + sizeof(ModelHeader));
    uint32_t n_blend_override = *blend_override_info++;
    uint32_t ofs_blend_override = *blend_override_info;

    blend_override = M2Array<uint16_t>(f, ofs_blend_override, n_blend_override);
  }

  animated = isAnimated(f, header);  // isAnimated will set animGeometry and animTextures

  trans = 1.0f;
  _current_anim_seq = 0;

  bounding_box_min = header.bounding_box_min;
  bounding_box_max = header.bounding_box_max;
  bounding_box_radius = header.bounding_box_radius;

  collision_box_min = header.collision_box_min;
  collision_box_max = header.collision_box_max;
  collision_box_radius = header.collision_box_radius;

  Flags = header.Flags;


  if (header.nGlobalSequences)
  {
    _global_sequences = M2Array<int>(f, header.ofsGlobalSequences, header.nGlobalSequences);
  }

  //! \todo  This takes a biiiiiit long. Have a look at this.
  initCommon(f, header);

  if (animated)
  {
    initAnimated(f, header);

    mesh_bounds_ratio = calcMeshBoundsRatio();
  }

  f.close();

  // add fake geometry for selection
  if (_renderer.renderPasses().empty() || particles_only() /* || this->file_key().filepath() == "world/generic/passivedoodads/particleemitters/ashenvalewisps.m2"*/)
  {
    _fake_geometry.emplace(this);
  }

  finished = true;
  _state_changed.notify_all();
}

void Model::waitForChildrenLoaded()
{
  for (auto& tex : _textures)
  {
    tex.get()->wait_until_loaded();
  }

  for (auto& pair : _replaceTextures)
  {
    pair.second.get()->wait_until_loaded();
  }
}

[[nodiscard]]
bool Model::is_hidden() const
{
  return _hidden;
}

void Model::toggle_visibility()
{
  _hidden = !_hidden;
}

void Model::show()
{
  _hidden = false;
}

void Model::hide()
{
  _hidden = true;
}

[[nodiscard]]
bool Model::use_fake_geometry() const
{
  return !!_fake_geometry;
}

[[nodiscard]]
bool Model::animated_mesh() const
{
  return (animGeometry || animBones);
}

[[nodiscard]]
bool Model::particles_only() const
{ // some particle emitters like wisps in ashenvale have a few vertices but no collision, using that to detect
  return !_particles.empty()
    && (_renderer.renderPasses().empty() || _vertices.empty() || !nBoundingTriangles);
}

[[nodiscard]]
bool Model::is_required_when_saving() const
{
  return true;
}

[[nodiscard]]
Noggit::Rendering::ModelRender* Model::renderer()
{
  return &_renderer;
}

uint32_t Model::get_anim_lenght(int16_t anim_id)
{
  return _animation_length[anim_id];
}

bool Model::isAnimated(const BlizzardArchive::ClientFile& f, ModelHeader& header)
{
  // see if we have any animated bones
  ModelBoneDef const* bo = reinterpret_cast<ModelBoneDef const*>(f.getBuffer() + header.ofsBones);

  animGeometry = false;
  animBones = false;
  _per_instance_animation = false;

  ModelVertex const* verts = reinterpret_cast<ModelVertex const*>(f.getBuffer() + header.ofsVertices);
  for (size_t i = 0; i<header.nVertices && !animGeometry; ++i) 
  {
    for (size_t b = 0; b<4; b++) 
    {
      if (verts[i].weights[b]>0) 
      {
        ModelBoneDef const& bb = bo[verts[i].bones[b]];
        bool billboard = (bb.flags & (0x78)); // billboard | billboard_lock_[xyz]

        if ((bb.flags & 0x200) || billboard) 
        {
          if (billboard) 
          {
            // if we have billboarding, the model will need per-instance animation
            _per_instance_animation = true;
          }
          animGeometry = true;
          break;
        }
      }
    }
  }

  if (animGeometry || header.nParticleEmitters || header.nRibbonEmitters || header.nLights)
  {
    animBones = true;
  }
  else
  {
    for (size_t i = 0; i<header.nBones; ++i)
    {
      ModelBoneDef const& bb = bo[i];
      if (bb.translation.type || bb.rotation.type || bb.scaling.type)
      {
        animBones = true;
        break;
      }
    }
  }

  animTextures = header.nTexAnims > 0;

  // animated colors
  if (header.nColors)
  {
    ModelColorDef const* cols = reinterpret_cast<ModelColorDef const*>(f.getBuffer() + header.ofsColors);
    for (size_t i = 0; i<header.nColors; ++i)
    {
      if (cols[i].color.type != 0 || cols[i].opacity.type != 0)
      {
        return true;
      }
    }
  }

  // animated opacity
  if (header.nTransparency)
  {
    ModelTransDef const* trs = reinterpret_cast<ModelTransDef const*>(f.getBuffer() + header.ofsTransparency);
    for (size_t i = 0; i<header.nTransparency; ++i)
    {
      if (trs[i].trans.type != 0)
      {
        return true;
      }
    }
  }

  // guess not...
  return animGeometry || animTextures || animBones;
}


glm::vec3 fixCoordSystem(glm::vec3 v)
{
  return glm::vec3(v.x, v.z, -v.y);
}

namespace
{
  glm::vec3 fixCoordSystem2(glm::vec3 v)
  {
    return glm::vec3(v.x, v.z, v.y);
  }

  glm::quat fixCoordSystemQuat(glm::quat v)
  {
    return glm::quat(-v.x, -v.z, v.y, v.w);
  }
}


void Model::initCommon(const BlizzardArchive::ClientFile& f, ModelHeader& header)
{
  // vertices, normals, texcoords
  _vertices = M2Array<ModelVertex>(f, header.ofsVertices, header.nVertices);

  vertices_bounds = { glm::vec3(std::numeric_limits<float>::max()),
                      glm::vec3(std::numeric_limits<float>::lowest()) };

  for (auto& v : _vertices)
  {

    if ((animGeometry || animBones))
    {
      // for animated models, calculate base vertex box of initial vertice positions to know the mesh size
      vertices_bounds[0].x = std::min(vertices_bounds[0].x, v.position.x);
      vertices_bounds[0].y = std::min(vertices_bounds[0].y, v.position.y);
      vertices_bounds[0].z = std::min(vertices_bounds[0].z, v.position.z);

      vertices_bounds[1].x = std::max(vertices_bounds[1].x, v.position.x);
      vertices_bounds[1].y = std::max(vertices_bounds[1].y, v.position.y);
      vertices_bounds[1].z = std::max(vertices_bounds[1].z, v.position.z);
    }

    v.position = fixCoordSystem(v.position);
    v.normal = fixCoordSystem(v.normal);
  }

  nBoundingTriangles = header.nBoundingTriangles;
  // Optional, load collision
  // collision_indices = M2Array<glm::uint16_t>(f, header.ofsBoundingTriangles, header.nBoundingTriangles);
  // collision_vertices = M2Array<glm::vec3>(f, header.ofsBoundingVertices, header.nBoundingVertices);
  // collision_normals = M2Array<glm::vec3>(f, header.ofsBoundingNormals, header.nBoundingNormals);

  // textures
  ModelTextureDef const* texdef = reinterpret_cast<ModelTextureDef const*>(f.getBuffer() + header.ofsTextures);
  _textureFilenames.resize(header.nTextures);
  _specialTextures.resize(header.nTextures);

  for (size_t i = 0; i < header.nTextures; ++i)
  {
    if (texdef[i].type == 0)
    {
      if (texdef[i].nameLen == 0)
      {
        LogDebug << "Texture " << i << " has a lenght of 0 for '" << _file_key.stringRepr() << std::endl;
        continue;
      }

      _specialTextures[i] = -1;
      const char* blp_ptr = f.getBuffer() + texdef[i].nameOfs;
      // some tools export the size without accounting for the \0
      bool invalid_size = *(blp_ptr + texdef[i].nameLen-1) != '\0';
      _textureFilenames[i] = std::string(blp_ptr, texdef[i].nameLen - (invalid_size ? 0 : 1));
    }
    else
    {
#ifndef NO_REPLACIBLE_TEXTURES_HACK
      _specialTextures[i] = -1;
      _textureFilenames[i] = "tileset/generic/black.blp";
#else
      //! \note special texture - only on characters and such... Noggit should not even render these.
      //! \todo Check if this is actually correct. Or just remove it.

      _specialTextures[i] = texdef[i].type;

      if (texdef[i].type == 3)
      {
        _textureFilenames[i] = "Item\\ObjectComponents\\Weapon\\ArmorReflect4.BLP";
        // a fix for weapons with type-3 textures.
        _replaceTextures.emplace (texdef[i].type, _textureFilenames[i]);
      }
#endif
    }
  }

  // init colors
  if (header.nColors)
  {
    _colors.reserve(header.nColors);
    ModelColorDef const* colorDefs = reinterpret_cast<ModelColorDef const*>(f.getBuffer() + header.ofsColors);
    for (size_t i = 0; i < header.nColors; ++i)
    {
      _colors.emplace_back (f, colorDefs[i], _global_sequences.data());
    }
  }

  // init transparency
  _transparency_lookup = M2Array<int16_t>(f, header.ofsTransparencyLookup, header.nTransparencyLookup);

  if (header.nTransparency)
  {
    _transparency.reserve(header.nTransparency);
    ModelTransDef const* trDefs = reinterpret_cast<ModelTransDef const*>(f.getBuffer() + header.ofsTransparency);
    for (size_t i = 0; i < header.nTransparency; ++i)
    {
      _transparency.emplace_back (f, trDefs[i], _global_sequences.data());
    }
  }


  // just use the first LOD/view

  if (header.nViews > 0) {
    // indices - allocate space, too
    std::string lodname = _file_key.filepath().substr(0, _file_key.filepath().length() - 3);
    lodname.append("00.skin");
    BlizzardArchive::ClientFile g(lodname, Noggit::Application::NoggitApplication::instance()->clientData());
    if (g.isEof()) {
      LogError << "loading skinfile " << lodname << std::endl;
      g.close();
      return;
    }

    auto view = reinterpret_cast<ModelView const*>(g.getBuffer());
    auto indexLookup = reinterpret_cast<uint16_t const*>(g.getBuffer() + view->ofs_index);
    auto triangles = reinterpret_cast<uint16_t const*>(g.getBuffer() + view->ofs_triangle);

    _indices.resize (view->n_triangle);

    for (size_t i (0); i < _indices.size(); ++i) {
      _indices[i] = indexLookup[triangles[i]];
    }

    // render ops
    auto model_geosets = reinterpret_cast<ModelGeoset const*>(g.getBuffer() + view->ofs_submesh);
    auto texture_unit = reinterpret_cast<ModelTexUnit const*>(g.getBuffer() + view->ofs_texture_unit);
    
    _texture_lookup = M2Array<uint16_t>(f, header.ofsTexLookup, header.nTexLookup);
    _texture_animation_lookups = M2Array<int16_t>(f, header.ofsTexAnimLookup, header.nTexAnimLookup);
    _texture_unit_lookup = M2Array<int16_t>(f, header.ofsTexUnitLookup, header.nTexUnitLookup);

    showGeosets.resize (view->n_submesh);
    for (size_t i = 0; i<view->n_submesh; ++i) 
    {
      showGeosets[i] = true;
    }

    _render_flags = M2Array<ModelRenderFlags>(f, header.ofsRenderFlags, header.nRenderFlags);

    _renderer.initRenderPasses(view, texture_unit, model_geosets);

    g.close();
  }  
}


FakeGeometry::FakeGeometry(Model* m)
{
  glm::vec3 min = m->bounding_box_min, max = m->bounding_box_max;

  vertices.emplace_back(min.x, max.y, min.z);
  vertices.emplace_back(min.x, max.y, max.z);
  vertices.emplace_back(max.x, max.y, max.z);
  vertices.emplace_back(max.x, max.y, min.z);

  vertices.emplace_back(min.x, min.y, min.z);
  vertices.emplace_back(min.x, min.y, max.z);
  vertices.emplace_back(max.x, min.y, max.z);
  vertices.emplace_back(max.x, min.y, min.z);

  indices =
  {
    0,1,2,  2,3,0,
    0,4,5,  5,1,0,
    0,3,7,  7,4,0,
    1,5,6,  6,2,1,
    2,6,7,  7,3,2,
    5,6,7,  7,4,5
  }; 
}


void Model::initAnimated(const BlizzardArchive::ClientFile& f, ModelHeader& header)
{
  std::vector<std::unique_ptr<BlizzardArchive::ClientFile>> animation_files;

  if (header.nAnimations > 0) 
  {
    std::vector<ModelAnimation> animations(header.nAnimations);

    memcpy(animations.data(), f.getBuffer() + header.ofsAnimations, header.nAnimations * sizeof(ModelAnimation));

    for (auto& anim : animations)
    {
      anim.length = std::max(anim.length, 1U);

      _animation_length[anim.animID] += anim.length;
      _animations_seq_per_id[anim.animID][anim.subAnimID] = anim;

      std::string lodname = _file_key.filepath().substr(0, _file_key.filepath().length() - 3);
      std::stringstream tempname;
      tempname << lodname << anim.animID << "-" << anim.subAnimID << ".anim";
      if (Noggit::Application::NoggitApplication::instance()->clientData()->exists(tempname.str()))
      {
        animation_files.push_back(std::make_unique<BlizzardArchive::ClientFile>(tempname.str(),
            Noggit::Application::NoggitApplication::instance()->clientData()));
      }
    }
  }

  if (animBones)
  {
    ModelBoneDef const* mb = reinterpret_cast<ModelBoneDef const*>(f.getBuffer() + header.ofsBones);
    for (size_t i = 0; i<header.nBones; ++i)
    {
      bones.emplace_back(f, mb[i], _global_sequences.data(), animation_files);
    }

    bone_matrices.resize(bones.size());
  }  

  if (animTextures) 
  {
    _texture_animations.reserve(header.nTexAnims);
    ModelTexAnimDef const* ta = reinterpret_cast<ModelTexAnimDef const*>(f.getBuffer() + header.ofsTexAnims);
    for (size_t i=0; i<header.nTexAnims; ++i) {
      _texture_animations.emplace_back (f, ta[i], _global_sequences.data());
    }
  }

  
  // particle systems
  if (header.nParticleEmitters)
  {
    _particles.reserve(header.nParticleEmitters);
    ModelParticleEmitterDef const* pdefs = reinterpret_cast<ModelParticleEmitterDef const*>(f.getBuffer() + header.ofsParticleEmitters);
    for (size_t i = 0; i<header.nParticleEmitters; ++i) 
    {
      try
      {
        _particles.emplace_back(this, f, pdefs[i], _global_sequences.data(), _context);
      }
      catch (std::logic_error error)
      {
        LogError << "Loading particles for '" << _file_key.stringRepr() << "' " << error.what() << std::endl;
      }      
    }
  }
  

  
  // ribbons
  if (header.nRibbonEmitters)
  {
    _ribbons.reserve(header.nRibbonEmitters);
    ModelRibbonEmitterDef const* rdefs = reinterpret_cast<ModelRibbonEmitterDef const*>(f.getBuffer() + header.ofsRibbonEmitters);
    for (size_t i = 0; i<header.nRibbonEmitters; ++i) {
      _ribbons.emplace_back(this, f, rdefs[i], _global_sequences.data(), _context);
    }
  }
  

  // init lights
  if (header.nLights)
  {
    _lights.reserve(header.nLights);
    ModelLightDef const* lDefs = reinterpret_cast<ModelLightDef const*>(f.getBuffer() + header.ofsLights);
    for (size_t i=0; i<header.nLights; ++i)
      _lights.emplace_back (f, lDefs[i], _global_sequences.data());
  }

  anim_calculated = false;
}

void Model::calcBones(glm::mat4x4 const& model_view
                     , int _anim
                     , int time
                     , int animation_time
                     )
{
  for (size_t i = 0; i< bones.size(); ++i)
  {
    bones[i].calc = false;
  }

  for (size_t i = 0; i< bones.size(); ++i)
  {
    bones[i].calcMatrix(model_view, bones.data(), _anim, time, animation_time);
  }
}

void Model::animate(glm::mat4x4 const& model_view, int anim_id, int anim_time)
{
  if (_animations_seq_per_id.empty() || _animations_seq_per_id[anim_id].empty())
  {
    return;
  }

  int tmax = _animation_length[anim_id];
  int t = anim_time % tmax;
  int current_sub_anim = 0;
  int time_for_anim = t;

  for (auto const& sub_animation : _animations_seq_per_id[anim_id])
  {
    if (static_cast<int>(sub_animation.second.length) > time_for_anim)
    {
      current_sub_anim = sub_animation.first;
      break;
    }

    time_for_anim -= sub_animation.second.length;
  }

  ModelAnimation const& a = _animations_seq_per_id[anim_id][current_sub_anim];

  _current_anim_seq = a.Index;//_animations_seq_lookup[anim_id][current_sub_anim];
  _anim_time = t;
  _global_animtime = anim_time;

  if (animBones) 
  {
    calcBones(model_view, _current_anim_seq, t, _global_animtime);
  }

  if (animGeometry || animBones)
  {
    std::size_t bone_counter = 0;
    for (auto& bone : bones)
    {
    	bone_matrices[bone_counter] = bone.mat;
      bone_counter++;
    }

    _renderer.updateBoneMatrices();


    // transform vertices
    // Animations are done in GPU now
    // getTransformVertices();
  }

  for (size_t i=0; i< _lights.size(); ++i)
  {
    if (_lights[i].parent >= 0) 
    {
        _lights[i].tpos = bones[_lights[i].parent].mat * glm::vec4(_lights[i].pos,0);
      _lights[i].tdir = bones[_lights[i].parent].mrot * glm::vec4(_lights[i].dir,0);
    }
  }

  /*
  for (auto& particle : _particles)
  {
    // random time distribution for teh win ..?
    int pt = (t + static_cast<int>(tmax*particle.tofs)) % tmax;
    particle.setup(_current_anim_seq, pt, _global_animtime);
  }

  for (size_t i = 0; i<header.nRibbonEmitters; ++i) 
  {
    _ribbons[i].setup(_current_anim_seq, t, _global_animtime);
  }

   */

  for (auto& tex_anim : _texture_animations)
  {
    tex_anim.calc(_current_anim_seq, t, _anim_time);
  }
}

std::vector<ModelVertex> Model::getTransformVertices() const
{
  if (!animGeometry)
    return {};

  // Don't need to store them anymore, we only need them for operations like intersections
  // _current_vertices = _vertices;
  std::vector<ModelVertex> _current_vertices = _vertices;

  for (auto& vertex : _current_vertices)
  {
    ::glm::vec3 v(0, 0, 0), n(0, 0, 0);

    for (size_t b(0); b < 4; ++b)
    {
      if (vertex.weights[b] <= 0)
        continue;

      ::glm::vec3 tv = bones[vertex.bones[b]].mat * glm::vec4(vertex.position, 1.0f);
      ::glm::vec3 tn = bones[vertex.bones[b]].mrot * glm::vec4(vertex.normal, 1.0f);

      v += tv * (static_cast<float> (vertex.weights[b]) / 255.0f);
      n += tn * (static_cast<float> (vertex.weights[b]) / 255.0f);
    }

    vertex.position = v;
    vertex.normal = glm::normalize(n);
  }
  /*
  OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> const binder (_vertices_buffer);
  gl.bufferData (GL_ARRAY_BUFFER, _current_vertices.size() * sizeof (ModelVertex), _current_vertices.data(), GL_STREAM_DRAW);

   */
  return _current_vertices;
}

std::optional<std::array<glm::vec3, 2>> Model::getCurrentSequenceBounds()
{

  for (auto const& animation : _animations_seq_per_id)
  {
    int anim_id = animation.first;
    for (auto const& sub_animation : animation.second)
    {
      int sub_anim_id = sub_animation.first;

      auto const& animation = sub_animation.second;

      if (_current_anim_seq == animation.Index)
      {

        return std::array<glm::vec3, 2>{ animation.boxA, animation.boxB };
      }
    }
  }

  return std::nullopt;
}

std::array<glm::vec3, 2> Model::getAnimatedBoundingBox() const
{
  if (mesh_bounds_ratio >= 1.0f)
    return { bounding_box_min, bounding_box_max };;

  auto const transform_verts = getTransformVertices();

  if (!animGeometry || transform_verts.empty())
    return { bounding_box_min, bounding_box_max };

  glm::vec3 min_bound(std::numeric_limits<float>::max());
  glm::vec3 max_bound(std::numeric_limits<float>::lowest());

  for (const auto& vertex : transform_verts) {
    min_bound.x = std::min(min_bound.x, vertex.position.x);
    min_bound.y = std::min(min_bound.y, vertex.position.y);
    min_bound.z = std::min(min_bound.z, vertex.position.z);

    max_bound.x = std::max(max_bound.x, vertex.position.x);
    max_bound.y = std::max(max_bound.y, vertex.position.y);
    max_bound.z = std::max(max_bound.z, vertex.position.z);
  }
  return { min_bound, max_bound };
}

float Model::calcMeshBoundsRatio() const
{
  if (!animated_mesh())
    return 1.0f;

  float global_bounds_volume = math::aabb(bounding_box_min, bounding_box_max).volume();

  if (global_bounds_volume == 0.0f)
    return 1.0f;

  float vertex_box_volume = math::aabb(vertices_bounds[0], vertices_bounds[1]).volume();

  // 0.0-1.0
  return (vertex_box_volume / global_bounds_volume);
}

void TextureAnim::calc(int anim, int time, int animtime)
{
    mat = glm::mat4x4(1);
  if (trans.uses(anim)) 
  {
      mat = glm::translate(mat, trans.getValue(anim, time, animtime));
  }
  if (rot.uses(anim)) 
  {
      mat *= glm::toMat4(rot.getValue(anim, time, animtime));
  }
  if (scale.uses(anim)) 
  {
      mat = glm::scale(mat, scale.getValue(anim, time, animtime));
  }
}

ModelColor::ModelColor(const BlizzardArchive::ClientFile& f, const ModelColorDef &mcd, int *global)
  : color (mcd.color, f, global)
  , opacity(mcd.opacity, f, global)
{}

ModelTransparency::ModelTransparency(const BlizzardArchive::ClientFile& f, const ModelTransDef &mcd, int *global)
  : trans (mcd.trans, f, global)
{}

ModelLight::ModelLight(const BlizzardArchive::ClientFile& f, const ModelLightDef &mld, int *global)
  : type (mld.type)
  , parent (mld.bone)
  , pos (fixCoordSystem(mld.pos))
  , tpos (fixCoordSystem(mld.pos))
  , dir (::glm::vec3(0,1,0))
  , tdir (::glm::vec3(0,1,0)) // obviously wrong
  , diffColor (mld.color, f, global)
  , ambColor (mld.ambColor, f, global)
  , diffIntensity (mld.intensity, f, global)
  , ambIntensity (mld.ambIntensity, f, global)
{}

void ModelLight::setup(int time, OpenGL::light, int animtime)
{
	auto ambient = ambColor.getValue(0, time, animtime) * ambIntensity.getValue(0, time, animtime);
    auto diffuse = diffColor.getValue(0, time, animtime) * diffIntensity.getValue(0, time, animtime);

  glm::vec4 ambcol(ambient.x, ambient.y,ambient.z, 1.0f);
  glm::vec4 diffcol(diffuse.x, diffuse.y, diffuse.z, 1.0f);
  glm::vec4 p;

  enum ModelLightTypes {
    MODELLIGHT_DIRECTIONAL = 0,
    MODELLIGHT_POINT
  };

  if (type == MODELLIGHT_DIRECTIONAL) {
    // directional
    p = glm::vec4(tdir.x, tdir.y, tdir.z, 0.0f);
  }
  else if (type == MODELLIGHT_POINT) {
    // point
    p = glm::vec4(tpos.x, tpos.y,tpos.z, 1.0f);
  }
  else {
    p = glm::vec4(tpos.x, tpos.y, tpos.z, 1.0f);
    LogError << "Light type " << type << " is unknown." << std::endl;
  }
 
  // todo: use models' light
}

TextureAnim::TextureAnim (const BlizzardArchive::ClientFile& f, const ModelTexAnimDef &mta, int *global)
  : trans (mta.trans, f, global)
  , rot (mta.rot, f, global)
  , scale (mta.scale, f, global)
  , mat (glm::mat4x4())
{}

Bone::Bone( const BlizzardArchive::ClientFile& f,
            const ModelBoneDef &b,
            int *global,
            const std::vector<std::unique_ptr<BlizzardArchive::ClientFile>>& animation_files)
  : trans (b.translation, f, global, animation_files)
  , rot (b.rotation, f, global, animation_files)
  , scale (b.scaling, f, global, animation_files)
  , pivot (fixCoordSystem (b.pivot))
  , parent (b.parent)
{
  memcpy(&flags, &b.flags, sizeof(uint32_t));

  trans.apply(fixCoordSystem);
  rot.apply(fixCoordSystemQuat);
  scale.apply(fixCoordSystem2);
}

void Bone::calcMatrix(glm::mat4x4 const& model_view
                     , Bone *allbones
                     , int anim
                     , int time
                     , int animtime
                     )
{

  if (calc) return;

  glm::mat4x4 m = glm::mat4x4(1);
  glm::mat4x4 mr = glm::mat4x4(1);

  if ( flags.transformed
    || flags.billboard 
    || flags.cylindrical_billboard_lock_x 
    || flags.cylindrical_billboard_lock_y 
    || flags.cylindrical_billboard_lock_z
      )
  {
  	m = glm::translate(m, pivot);


    if (trans.uses(anim))
    {
      m = glm::translate(m, trans.getValue (anim, time, animtime));
    }

    if (rot.uses(anim))
    {
      glm::quat ref = glm::quat_cast(glm::mat4x4(1));
      glm::quat q = rot.getValue(anim, time, animtime);
      glm::vec3 rot_euler = glm::eulerAngles(q);

      glm::vec3 test_rot_vec = glm::vec3(rot_euler[2], 
        -(rot_euler[1] + glm::radians(180.f)),
        -(rot_euler[0] + glm::radians(180.f)));

      mr = glm::eulerAngleXYZ(test_rot_vec.x, test_rot_vec.y, test_rot_vec.z);

      m = m * mr;
    }

    if (scale.uses(anim))
    {
      m = glm::scale(m, scale.getValue (anim, time, animtime));
    }

    if (flags.billboard)
    {
        glm::vec3 vRight = model_view[0];
        glm::vec3 vUp = model_view[1]; 
    	vRight =  glm::vec3(vRight.x * -1, vRight.y * -1, vRight.z * -1);
        m[0][2] = vRight.x;
        m[1][2] = vRight.y;
        m[2][2] = vRight.z;
        m[0][1] = vUp.x;
        m[1][1] = vUp.y;
        m[2][1] = vUp.z;
    }

    m = glm::translate(m, -pivot);
  }

  if (parent >= 0)
  {
    allbones[parent].calcMatrix (model_view, allbones, anim, time, animtime);
    mat = allbones[parent].mat * m;
  }
  else
  {
    mat = m;
  }
  
  // transform matrix for normal vectors ... ??
  if (rot.uses(anim))
  {
    if (parent >= 0)
    {
        mrot = allbones[parent].mrot * mr;
    }
    else
    {
      mrot = mr;
    }
  }
  else
  {
    mrot = glm::mat4x4(1);
  }

  calc = true;
}


std::vector<std::pair<float, std::tuple<int, int, int>>> Model::intersect (glm::mat4x4 const& model_view, math::ray const& ray, int animtime, bool calc_anims)
{
  std::vector<std::pair<float, std::tuple<int, int, int>>> results;

  if (!finishedLoading() || loading_failed())
  {
    return results;
  }

  if (animated && calc_anims && (!anim_calculated || _per_instance_animation))
  {
    animate (model_view, 0, animtime);
    anim_calculated = true;
  }

  if (use_fake_geometry())
  {
    auto& fake_geom = _fake_geometry.value();

    for (size_t i = 0; i < fake_geom.indices.size(); i += 3)
    {
      if (auto distance
        = ray.intersect_triangle(fake_geom.vertices[fake_geom.indices[i + 0]],
          fake_geom.vertices[fake_geom.indices[i + 1]],
          fake_geom.vertices[fake_geom.indices[i + 2]])
        )
      {
        results.emplace_back (*distance, std::make_tuple(static_cast<int>(i), static_cast<int>(i+1), 1+2));
        return results;
      }
    }

    return results;
  }

  auto const transformed_verts = getTransformVertices();
  bool const has_transformed_verts = !transformed_verts.empty();
  for (auto const& pass : _renderer.renderPasses())
  {
    for (int i (pass.index_start); i < pass.index_start + pass.index_count; i += 3)
    {
      std::optional<float> distance;
      if (!has_transformed_verts)
      {
        distance = ray.intersect_triangle(_vertices[_indices[static_cast<std::size_t>(i + 0)]].position,
                                          _vertices[_indices[static_cast<std::size_t>(i + 1)]].position,
                                          _vertices[_indices[static_cast<std::size_t>(i + 2)]].position);
      }
      else
      {
        distance = ray.intersect_triangle(transformed_verts[_indices[static_cast<std::size_t>(i + 0)]].position,
                                          transformed_verts[_indices[static_cast<std::size_t>(i + 1)]].position,
                                          transformed_verts[_indices[static_cast<std::size_t>(i + 2)]].position);
      }

      glm::vec3 debug_vert1 = _vertices[_indices[static_cast<std::size_t>(i + 0)]].position; ////
      glm::vec3 debug_vert2 = _vertices[_indices[static_cast<std::size_t>(i + 1)]].position; ////
      glm::vec3 debug_vert3 = _vertices[_indices[static_cast<std::size_t>(i + 2)]].position; ////

      if (distance)
      {
        results.emplace_back (*distance, std::make_tuple(i, i + 1, 1 + 2));
      }
    }
  }

  return results;
}

void Model::lightsOn(OpenGL::light lbase)
{
  // setup lights
  for (unsigned int i=0, l=lbase; i< _lights.size(); ++i) _lights[i].setup(_anim_time, l++, _global_animtime);
}

void Model::lightsOff(OpenGL::light lbase)
{
  for (unsigned int i = 0, l = lbase; i< _lights.size(); ++i) gl.disable(l++);
}


void Model::updateEmitters(float dt)
{
  return;

  if (finished)
  {
    for (auto& particle : _particles)
    {
      particle.update (dt);
    }
  }
}

