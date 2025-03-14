// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once
#include <noggit/tool_enums.hpp>

#include <glm/vec3.hpp>

#include <QJsonObject>
#include <QtWidgets/QWidget>

namespace Noggit::Ui::Tools::UiCommon
{
  class ExtendedSlider;
}

class World;

class QButtonGroup;
class QCheckBox;
class QLabel;
class QDial;
class QDoubleSpinBox;
class QGroupBox;
class QSlider;

namespace Noggit
{
  namespace Ui
  {
    class flatten_blur_tool : public QWidget
    {
    public:
      flatten_blur_tool(QWidget* parent = nullptr);

      void flatten (World* world, glm::vec3 const& cursor_pos, float dt);
      void blur (World* world, glm::vec3 const& cursor_pos, float dt);

      void nextFlattenType();
      void nextFlattenMode();
      void toggleFlattenAngle();
      void toggleFlattenLock();
      void lockPos (glm::vec3 const& cursor_pos);

      void changeRadius(float change);
      void changeSpeed(float change);
      void changeOrientation(float change);
      void changeAngle(float change);
      void changeHeight(float change);

      void setRadius(float radius);
      void setSpeed(float speed);
      void setOrientation(float orientation);

      float brushRadius() const;
      float angle() const;
      float orientation() const;
      bool angled_mode() const;
      bool use_ref_pos() const;
      glm::vec3 ref_pos() const;

      Noggit::Ui::Tools::UiCommon::ExtendedSlider* getRadiusSlider();;
      Noggit::Ui::Tools::UiCommon::ExtendedSlider* getSpeedSlider();;

      QSize sizeHint() const override;
      flatten_mode _flatten_mode;

      QJsonObject toJSON();
      void fromJSON(QJsonObject const& json);

    private:
      float _angle;
      float _orientation;

      glm::vec3 _lock_pos;

      int _flatten_type;

    private:
      QButtonGroup* _type_button_box;
      Noggit::Ui::Tools::UiCommon::ExtendedSlider* _radius_slider;
      Noggit::Ui::Tools::UiCommon::ExtendedSlider* _speed_slider;

      QGroupBox* _angle_group;
      QSlider* _angle_slider;
      QDial* _orientation_dial;
      QLabel* _orientation_info;
      QLabel* _angle_info;

      QGroupBox* _lock_group;
      QDoubleSpinBox* _lock_x;
      QDoubleSpinBox* _lock_z;
      QDoubleSpinBox* _lock_h;

      QCheckBox* _lock_up_checkbox;
      QCheckBox* _lock_down_checkbox;
      QCheckBox* _snap_m2_objects_chkbox;
      QCheckBox* _snap_wmo_objects_chkbox;
    };
  }
}
