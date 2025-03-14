// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#ifndef NOGGIT_LISTGETNODE_HPP
#define NOGGIT_LISTGETNODE_HPP

#include "noggit/ui/tools/NodeEditor/Nodes/LogicNodeBase.hpp"

using QtNodes::PortType;
using QtNodes::PortIndex;
using QtNodes::NodeData;
using QtNodes::NodeDataType;
using QtNodes::NodeDataModel;
using QtNodes::NodeValidationState;

class QComboBox;

namespace Noggit
{
    namespace Ui::Tools::NodeEditor::Nodes
    {
        class ListGetNode : public LogicNodeBase
        {
        Q_OBJECT

        public:
            ListGetNode();
            void compute() override;
            NodeValidationState validate() override;
            QJsonObject save() const override;
            void restore(QJsonObject const& json_obj) override;

        public Q_SLOTS:
            void inputConnectionCreated(const Connection& connection) override;
            void inputConnectionDeleted(const Connection& connection) override;

        private:
            QComboBox* _operation;
        };
    }
}

#endif //NOGGIT_LISTGETNODE_HPP
