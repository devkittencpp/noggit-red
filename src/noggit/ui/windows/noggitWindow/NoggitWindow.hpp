#ifndef NOGGIT_WINDOW_NOGGIT_HPP
#define NOGGIT_WINDOW_NOGGIT_HPP

#include <noggit/ui/windows/noggitWindow/components/BuildMapListComponent.hpp>
#include <noggit/ui/uid_fix_mode.hpp>

#include <math/trig.hpp>

#include <QtWidgets/QMainWindow>
#include <QWidget>
#include <QMetaObject>

#include <memory>
#include <string>
#include <unordered_set>

class MapView;
class StackedWidget;
class World;

class QListWidget;

namespace Noggit::Application
{
  struct NoggitApplicationConfiguration;
}

namespace Noggit::Ui::Component
{
  class BuildMapListComponent;
}

namespace Noggit::Project
{
  class NoggitProject;
}

namespace Noggit::Ui::Tools::MapCreationWizard::Ui
{
  class MapCreationWizard;
}

namespace Noggit::Ui
{
    class minimap_widget;
    class settings;
    class about;

    namespace Tools::MapCreationWizard::Ui
    {
      class MapCreationWizard;
    }
}

namespace Noggit::Ui::Windows
{
    class NoggitWindow : public QMainWindow
    {
      Q_OBJECT

      friend class Noggit::Ui::Component::BuildMapListComponent;

    public:
      NoggitWindow(std::shared_ptr<Noggit::Application::NoggitApplicationConfiguration> application,
          std::shared_ptr<Noggit::Project::NoggitProject> project);

      void promptExit(QCloseEvent* event);
      void promptUidFixFailure();

      // TODO, better location for those utils that need to be above mapview?
      void startWowClient();

      void patchWowClient();


      QMenuBar* _menuBar;

      QToolBar* _app_toolbar;

      // std::unique_ptr<World> _world;

      std::unordered_set<QWidget*> displayed_widgets;
      void buildMenu();
    signals:
      void exitPromptOpened();
      void mapSelected(int map_id);

    private:
    	std::unique_ptr<Component::BuildMapListComponent> _buildMapListComponent;
      std::shared_ptr<Application::NoggitApplicationConfiguration> _applicationConfiguration;
      std::shared_ptr<Project::NoggitProject> _project;


      void handleEventMapListContextMenuPinMap(int mapId, std::string MapName);
      void handleEventMapListContextMenuUnpinMap(int mapId);

      World* getWorld();

      void loadMap (int map_id);

      void check_uid_then_enter_map ( glm::vec3 pos
                                    , math::degrees camera_pitch
                                    , math::degrees camera_yaw
                                    , bool from_bookmark = false
                                    );

      void enterMapAt ( glm::vec3 pos
                      , math::degrees camera_pitch
                      , math::degrees camera_yaw
                      , uid_fix_mode uid_fix = uid_fix_mode::none
                      , bool from_bookmark = false
                      );

      minimap_widget* _minimap;
      settings* _settings;
      about* _about;
      QWidget* _null_widget;
      MapView* _map_view;
      StackedWidget* _stack_widget;

      Noggit::Ui::Tools::MapCreationWizard::Ui::MapCreationWizard* _map_creation_wizard;
      QMetaObject::Connection _map_wizard_connection;

      QListWidget* _continents_table;
      QString _filter_name;
      QTabWidget* _right_side;

      void applyFilterSearch(const QString& name, int type, int expansion, bool wmo_maps);

      bool map_loaded = false;
      bool exit_to_project_selection = false;

      virtual void closeEvent (QCloseEvent*) override;
    };
}
#endif // NOGGIT_WINDOW_NOGGIT_HPP
