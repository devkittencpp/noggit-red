// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#ifndef NOGGIT_MATHUNARYNODE_HPP
#define NOGGIT_MATHUNARYNODE_HPP

#include "noggit/ui/tools/NodeEditor/Nodes/BaseNode.hpp"

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
        class MathUnaryNode : public BaseNode
        {
        Q_OBJECT

        public:
            MathUnaryNode();
            NodeValidationState validate() override;
            void compute() override;
            QJsonObject save() const override;
            void restore(QJsonObject const& json_obj) override;

        private:
          QComboBox* _operation;
        };
    }
}

#endif //NOGGIT_MATHUNARYNODE_HPP
