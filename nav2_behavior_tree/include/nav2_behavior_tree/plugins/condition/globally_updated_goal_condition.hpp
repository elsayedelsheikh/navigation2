// Copyright (c) 2021 Joshua Wallace
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NAV2_BEHAVIOR_TREE__PLUGINS__CONDITION__GLOBALLY_UPDATED_GOAL_CONDITION_HPP_
#define  NAV2_BEHAVIOR_TREE__PLUGINS__CONDITION__GLOBALLY_UPDATED_GOAL_CONDITION_HPP_

#include <string>
#include <vector>

#include "nav2_ros_common/lifecycle_node.hpp"
#include "behaviortree_cpp/condition_node.h"
#include "behaviortree_cpp/json_export.h"
#include "nav_msgs/msg/goals.hpp"
#include "nav2_behavior_tree/bt_utils.hpp"
#include "nav2_behavior_tree/json_utils.hpp"


namespace nav2_behavior_tree
{
/**
 * @brief A BT::ConditionNode that returns SUCCESS when goal is
 * updated on the blackboard and FAILURE otherwise
 */
class GloballyUpdatedGoalCondition : public BT::ConditionNode
{
public:
  /**
   * @brief A constructor for nav2_behavior_tree::GloballyUpdatedGoalCondition
   * @param condition_name Name for the XML tag for this node
   * @param conf BT node configuration
   */
  GloballyUpdatedGoalCondition(
    const std::string & condition_name,
    const BT::NodeConfiguration & conf);

  GloballyUpdatedGoalCondition() = delete;

  /**
   * @brief The main override required by a BT action
   * @return BT::NodeStatus Status of tick execution
   */
  BT::NodeStatus tick() override;


  /**
   * @brief Creates list of BT ports
   * @return BT::PortsList Containing node-specific ports
   */
  static BT::PortsList providedPorts()
  {
    // Register JSON definitions for the types used in the ports
    BT::RegisterJsonDefinition<geometry_msgs::msg::PoseStamped>();
    BT::RegisterJsonDefinition<nav_msgs::msg::Goals>();

    return {
      BT::InputPort<nav_msgs::msg::Goals>(
        "goals", "Vector of navigation goals"),
      BT::InputPort<geometry_msgs::msg::PoseStamped>(
        "goal", "Navigation goal"),
    };
  }

private:
  bool first_time;
  nav2::LifecycleNode::SharedPtr node_;
  geometry_msgs::msg::PoseStamped goal_;
  nav_msgs::msg::Goals goals_;
};

}  // namespace nav2_behavior_tree


#endif  // NAV2_BEHAVIOR_TREE__PLUGINS__CONDITION__GLOBALLY_UPDATED_GOAL_CONDITION_HPP_
