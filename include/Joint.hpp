#ifndef RML_JOINT_HPP
#define RML_JOINT_HPP

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <tinyxml2.h>

namespace RML {

    /// @brief The types of joints
    enum class JointType { UNKNOWN, REVOLUTE, CONTINUOUS, PRISMATIC, FLOATING, PLANAR, FIXED };

    /**
     * @brief Defines how a link moves relative to an attachment point
     * @details
     * @param Scalar The scalar type of the joint
     */
    template <typename Scalar>
    struct Joint {

        /// @brief The index of the joint in the model joint vector
        int joint_idx = -1;

        /// @brief The index of the joint in the models configuration vector
        int q_idx = -1;

        /// @brief The name of the joint
        std::string name = "";

        /// @brief The type of the joint
        JointType type = JointType::UNKNOWN;

        /// @brief The axis of motion for the joint
        Eigen::Matrix<Scalar, 3, 1> axis = Eigen::Matrix<Scalar, 3, 1>::Zero();

        /// @brief The transform to the parent link
        Eigen::Transform<Scalar, 3, Eigen::Affine> parent_transform =
            Eigen::Transform<Scalar, 3, Eigen::Affine>::Identity();

        /// @brief The transform to the child link
        Eigen::Transform<Scalar, 3, Eigen::Affine> child_transform =
            Eigen::Transform<Scalar, 3, Eigen::Affine>::Identity();

        /// @brief The parent link name
        std::string parent_link_name = "";

        /// @brief The child link name
        std::string child_link_name = "";

        /**
         * @brief Casts to NewScalar type
         */
        template <typename NewScalar>
        Joint<NewScalar> cast() const {
            Joint<NewScalar> new_joint = Joint<NewScalar>();
            new_joint.joint_idx        = joint_idx;
            new_joint.q_idx            = q_idx;
            new_joint.name             = name;
            new_joint.type             = type;
            new_joint.axis             = axis.template cast<NewScalar>();
            new_joint.parent_transform = parent_transform.template cast<NewScalar>();
            new_joint.child_transform  = child_transform.template cast<NewScalar>();
            new_joint.parent_link_name = parent_link_name;
            new_joint.child_link_name  = child_link_name;
            return new_joint;
        }
    };
}  // namespace RML

#endif
