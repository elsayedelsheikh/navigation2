// Copyright (c) 2018 Intel Corporation
// Copyright (c) 2022 Joshua Wallace
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

#ifndef NAV2_BEHAVIORS__PLUGINS__DRIVE_ON_HEADING_HPP_
#define NAV2_BEHAVIORS__PLUGINS__DRIVE_ON_HEADING_HPP_

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <limits>

#include "nav2_behaviors/timed_behavior.hpp"
#include "nav2_msgs/action/drive_on_heading.hpp"
#include "nav2_msgs/action/back_up.hpp"
#include "nav2_ros_common/node_utils.hpp"
#include "geometry_msgs/msg/twist_stamped.hpp"

namespace nav2_behaviors
{

/**
 * @class nav2_behaviors::DriveOnHeading
 * @brief An action server Behavior for spinning in
 */
template<typename ActionT = nav2_msgs::action::DriveOnHeading>
class DriveOnHeading : public TimedBehavior<ActionT>
{
  using CostmapInfoType = nav2_core::CostmapInfoType;

public:
  /**
   * @brief A constructor for nav2_behaviors::DriveOnHeading
   */
  DriveOnHeading()
  : TimedBehavior<ActionT>(),
    feedback_(std::make_shared<typename ActionT::Feedback>()),
    command_x_(0.0),
    command_speed_(0.0),
    command_disable_collision_checks_(false),
    simulate_ahead_time_(0.0)
  {
  }

  ~DriveOnHeading() = default;

  /**
   * @brief Initialization to run behavior
   * @param command Goal to execute
   * @return Status of behavior
   */
  ResultStatus onRun(const std::shared_ptr<const typename ActionT::Goal> command) override
  {
    std::string error_msg;
    if (command->target.y != 0.0 || command->target.z != 0.0) {
      error_msg = "DrivingOnHeading in Y and Z not supported, will only move in X.";
      RCLCPP_INFO(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::INVALID_INPUT, error_msg};
    }

    // Ensure that both the speed and direction have the same sign
    if (!((command->target.x > 0.0) == (command->speed > 0.0)) ) {
      error_msg = "Speed and command sign did not match";
      RCLCPP_ERROR(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::INVALID_INPUT, error_msg};
    }

    command_x_ = command->target.x;
    command_speed_ = command->speed;
    command_time_allowance_ = command->time_allowance;
    command_disable_collision_checks_ = command->disable_collision_checks;

    end_time_ = this->clock_->now() + command_time_allowance_;

    if (!nav2_util::getCurrentPose(
        initial_pose_, *this->tf_, this->local_frame_, this->robot_base_frame_,
        this->transform_tolerance_))
    {
      error_msg = "Initial robot pose is not available.";
      RCLCPP_ERROR(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::TF_ERROR, error_msg};
    }

    return ResultStatus{Status::SUCCEEDED, ActionT::Result::NONE, ""};
  }

  /**
   * @brief Loop function to run behavior
   * @return Status of behavior
   */
  ResultStatus onCycleUpdate() override
  {
    rclcpp::Duration time_remaining = end_time_ - this->clock_->now();
    if (time_remaining.seconds() < 0.0 && command_time_allowance_.seconds() > 0.0) {
      this->stopRobot();
      std::string error_msg =
        "Exceeded time allowance before reaching the DriveOnHeading goal - Exiting DriveOnHeading";
      RCLCPP_WARN(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::TIMEOUT, error_msg};
    }

    geometry_msgs::msg::PoseStamped current_pose;
    if (!nav2_util::getCurrentPose(
        current_pose, *this->tf_, this->local_frame_, this->robot_base_frame_,
        this->transform_tolerance_))
    {
      std::string error_msg = "Current robot pose is not available.";
      RCLCPP_ERROR(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::TF_ERROR, error_msg};
    }

    double diff_x = initial_pose_.pose.position.x - current_pose.pose.position.x;
    double diff_y = initial_pose_.pose.position.y - current_pose.pose.position.y;
    double distance = hypot(diff_x, diff_y);

    feedback_->distance_traveled = distance;
    this->action_server_->publish_feedback(feedback_);

    if (distance >= std::fabs(command_x_)) {
      this->stopRobot();
      return ResultStatus{Status::SUCCEEDED, ActionT::Result::NONE, ""};
    }

    auto cmd_vel = std::make_unique<geometry_msgs::msg::TwistStamped>();
    cmd_vel->header.stamp = this->clock_->now();
    cmd_vel->header.frame_id = this->robot_base_frame_;
    cmd_vel->twist.linear.y = 0.0;
    cmd_vel->twist.angular.z = 0.0;

    double current_speed = last_vel_ == std::numeric_limits<double>::max() ? 0.0 : last_vel_;
    bool forward = command_speed_ > 0.0;
    double min_feasible_speed, max_feasible_speed;
    if (forward) {
      min_feasible_speed = current_speed + deceleration_limit_ / this->cycle_frequency_;
      max_feasible_speed = current_speed + acceleration_limit_ / this->cycle_frequency_;
    } else {
      min_feasible_speed = current_speed - acceleration_limit_ / this->cycle_frequency_;
      max_feasible_speed = current_speed - deceleration_limit_ / this->cycle_frequency_;
    }
    cmd_vel->twist.linear.x = std::clamp(command_speed_, min_feasible_speed, max_feasible_speed);

    // Check if we need to slow down to avoid overshooting
    auto remaining_distance = std::fabs(command_x_) - distance;
    double max_vel_to_stop = std::sqrt(-2.0 * deceleration_limit_ * remaining_distance);
    if (max_vel_to_stop < std::abs(cmd_vel->twist.linear.x)) {
      cmd_vel->twist.linear.x = forward ? max_vel_to_stop : -max_vel_to_stop;
    }

    // Ensure we don't go below minimum speed
    if (std::fabs(cmd_vel->twist.linear.x) < minimum_speed_) {
      cmd_vel->twist.linear.x = forward ? minimum_speed_ : -minimum_speed_;
    }

    geometry_msgs::msg::Pose pose2d = current_pose.pose;

    if (!isCollisionFree(distance, cmd_vel->twist, pose2d)) {
      this->stopRobot();
      std::string error_msg = "Collision Ahead - Exiting DriveOnHeading";
      RCLCPP_WARN(this->logger_, error_msg.c_str());
      return ResultStatus{Status::FAILED, ActionT::Result::COLLISION_AHEAD, error_msg};
    }

    last_vel_ = cmd_vel->twist.linear.x;
    this->vel_pub_->publish(std::move(cmd_vel));

    return ResultStatus{Status::RUNNING, ActionT::Result::NONE, ""};
  }

  /**
   * @brief Method to determine the required costmap info
   * @return costmap resources needed
   */
  CostmapInfoType getResourceInfo() override {return CostmapInfoType::LOCAL;}

  void onCleanup() override {last_vel_ = std::numeric_limits<double>::max();}

  void onActionCompletion(std::shared_ptr<typename ActionT::Result>/*result*/)
  override
  {
    last_vel_ = std::numeric_limits<double>::max();
  }

protected:
  /**
   * @brief Check if pose is collision free
   * @param distance Distance to check forward
   * @param cmd_vel current commanded velocity
   * @param pose Current pose
   * @return is collision free or not
   */
  bool isCollisionFree(
    const double & distance,
    const geometry_msgs::msg::Twist & cmd_vel,
    geometry_msgs::msg::Pose & pose)
  {
    if (command_disable_collision_checks_) {
      return true;
    }

    // Simulate ahead by simulate_ahead_time_ in this->cycle_frequency_ increments
    int cycle_count = 0;
    double sim_position_change;
    const double diff_dist = abs(command_x_) - distance;
    const int max_cycle_count = static_cast<int>(this->cycle_frequency_ * simulate_ahead_time_);
    geometry_msgs::msg::Pose init_pose = pose;
    double init_theta = tf2::getYaw(init_pose.orientation);
    bool fetch_data = true;

    while (cycle_count < max_cycle_count) {
      sim_position_change = cmd_vel.linear.x * (cycle_count / this->cycle_frequency_);
      pose.position.x = init_pose.position.x + sim_position_change * cos(init_theta);
      pose.position.y = init_pose.position.y + sim_position_change * sin(init_theta);
      cycle_count++;

      if (diff_dist - abs(sim_position_change) <= 0.) {
        break;
      }

      if (!this->local_collision_checker_->isCollisionFree(pose, fetch_data)) {
        return false;
      }
      fetch_data = false;
    }
    return true;
  }

  /**
   * @brief Configuration of behavior action
   */
  void onConfigure() override
  {
    auto node = this->node_.lock();
    if (!node) {
      throw std::runtime_error{"Failed to lock node"};
    }

    nav2::declare_parameter_if_not_declared(
      node,
      "simulate_ahead_time", rclcpp::ParameterValue(2.0));
    node->get_parameter("simulate_ahead_time", simulate_ahead_time_);

    nav2::declare_parameter_if_not_declared(
      node, this->behavior_name_ + ".acceleration_limit",
      rclcpp::ParameterValue(2.5));
    nav2::declare_parameter_if_not_declared(
      node, this->behavior_name_ + ".deceleration_limit",
      rclcpp::ParameterValue(-2.5));
    nav2::declare_parameter_if_not_declared(
      node, this->behavior_name_ + ".minimum_speed",
      rclcpp::ParameterValue(0.10));
    node->get_parameter(this->behavior_name_ + ".acceleration_limit", acceleration_limit_);
    node->get_parameter(this->behavior_name_ + ".deceleration_limit", deceleration_limit_);
    node->get_parameter(this->behavior_name_ + ".minimum_speed", minimum_speed_);
    if (acceleration_limit_ <= 0.0 || deceleration_limit_ >= 0.0) {
      RCLCPP_ERROR(this->logger_,
        "DriveOnHeading: acceleration_limit and deceleration_limit must be "
        "positive and negative respectively");
      acceleration_limit_ = std::abs(acceleration_limit_);
      deceleration_limit_ = -std::abs(deceleration_limit_);
    }
  }

  typename ActionT::Feedback::SharedPtr feedback_;

  geometry_msgs::msg::PoseStamped initial_pose_;
  double command_x_;
  double command_speed_;
  bool command_disable_collision_checks_;
  rclcpp::Duration command_time_allowance_{0, 0};
  rclcpp::Time end_time_;
  double simulate_ahead_time_;
  double acceleration_limit_;
  double deceleration_limit_;
  double minimum_speed_;
  double last_vel_ = std::numeric_limits<double>::max();
};

}  // namespace nav2_behaviors

#endif  // NAV2_BEHAVIORS__PLUGINS__DRIVE_ON_HEADING_HPP_
