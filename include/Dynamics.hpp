#ifndef RML_DYNAMICS_HPP
#define RML_DYNAMICS_HPP

#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <autodiff/forward/real.hpp>
#include <autodiff/forward/real/eigen.hpp>

#include "Kinematics.hpp"
#include "Model.hpp"

namespace RML {

    /**
     * @brief Compute the mass matrix of the robot model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     */
    template <typename Scalar, int nq>
    Eigen::Matrix<Scalar, nq, nq> mass_matrix(Model<Scalar, nq>& model, const Eigen::Matrix<Scalar, nq, 1>& q) {
        // Reset the mass matrix and potential energy
        model.data.M.setZero();
        model.data.V = 0;
        Eigen::Matrix<Scalar, 6, 6> Mi;
        // Get the base link from the model
        auto base_link = model.links[model.base_link_idx];
        for (int i = 0; i < model.n_links; i++) {
            Mi.setZero();
            // Compute the rotation between the ith link and the base
            Eigen::Matrix<Scalar, 3, 3> R0i = rotation(model, q, base_link.name, model.links[i].name);
            // Insert the mass of the link into the top 3 diagonals
            Mi.block(0, 0, 3, 3) = model.links[i].mass * Eigen::Matrix<Scalar, 3, 3>::Identity();
            // Insert the inertia of the link into the bottom 3 diagonals
            Mi.block(3, 3, 3, 3) = R0i * model.links[i].inertia * R0i.transpose();
            // Compute the geometric jacobian of the links center of mass with respect to the base
            Eigen::Matrix<Scalar, 6, nq> Jci = geometric_jacobian_com(model, q, model.links[i].name);
            // Compute the contribution to the mass matrix of the link
            model.data.M += Jci.transpose() * Mi * Jci;
            // Compute the contribution to the potential energy of the link
            Eigen::Transform<Scalar, 3, Eigen::Affine> Hbi_c =
                forward_kinematics_com<Scalar, nq>(model, q, model.base_link_idx, model.links[i].link_idx);
            Eigen::Matrix<Scalar, 3, 1> rMIi_c = Hbi_c.translation();
            model.data.V += -model.links[i].mass * model.gravity.transpose() * rMIi_c;
        }
        return model.data.M;
    }

    /**
     * @brief Compute the kinetic_energy of the robot model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param dq The joint velocity of the robot.
     */
    template <typename Scalar, int nq>
    void kinetic_energy(Model<Scalar, nq>& model,
                        const Eigen::Matrix<Scalar, nq, 1>& q,
                        const Eigen::Matrix<Scalar, nq, 1>& dq) {

        // Compute the mass matrix
        mass_matrix<Scalar, nq>(model, q);
        // Compute the kinetic energy
        model.data.T = 0.5 * dq.transpose() * model.data.M * dq;
    }

    /**
     * @brief Compute the potential_energy of the robot model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     */
    template <typename Scalar, int nq>
    void potential_energy(Model<Scalar, nq>& model, const Eigen::Matrix<Scalar, nq, 1>& q) {
        // Reset the potential energy
        model.data.V = 0;
        // Compute the potential energy
        for (int i = 0; i < model.n_links; i++) {
            // Compute the contribution to the potential energy of the link
            Eigen::Transform<Scalar, 3, Eigen::Affine> Hbi_c =
                forward_kinematics_com<Scalar, nq>(model, q, model.base_link_idx, model.links[i].link_idx);
            Eigen::Matrix<Scalar, 3, 1> rMIi_c = Hbi_c.translation();
            model.data.V += -model.links[i].mass * model.gravity.transpose() * rMIi_c;
        }
    }

    /**
     * @brief Compute the hamiltonian of the robot model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param p The joint velocity of the robot.
     */
    template <typename Scalar, int nq>
    Eigen::Matrix<Scalar, 1, 1> hamiltonian(Model<Scalar, nq>& model,
                                            const Eigen::Matrix<Scalar, nq, 1>& q,
                                            const Eigen::Matrix<Scalar, nq, 1>& p) {
        // Compute the mass matrix and potential energy
        mass_matrix<Scalar, nq>(model, q);

        // Compute the inverse of the mass matrix
        Eigen::Matrix<Scalar, nq, nq> b    = Eigen::Matrix<Scalar, nq, nq>::Identity();
        Eigen::Matrix<Scalar, nq, nq> Minv = model.data.M.ldlt().solve(b);
        model.data.Minv                    = Minv;

        // Compute the kinetic energy
        model.data.T = Scalar(0.5 * p.transpose() * Minv * p);

        // Compute the total energy
        Eigen::Matrix<Scalar, 1, 1> H = Eigen::Matrix<Scalar, 1, 1>(model.data.T + model.data.V);
        return H;
    }

    /**
     *
     * @brief Compute the forward dynamics of the model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param p The momentum vector
     * @param u The input vector.
     */
    template <typename Scalar, int nq, int ni>
    Eigen::Matrix<Scalar, nq + nq, 1> forward_dynamics_without_constraints(Model<Scalar, nq>& model,
                                                                           const Eigen::Matrix<Scalar, nq, 1>& q,
                                                                           const Eigen::Matrix<Scalar, nq, 1>& p,
                                                                           const Eigen::Matrix<Scalar, ni, 1>& u) {
        // Number of states
        const int nx = nq + nq;
        // Cast to autodiff type for automatic differentiation
        Eigen::Matrix<autodiff::real, nq, 1> q_ad(q);  // the input vector q
        Eigen::Matrix<autodiff::real, nq, 1> p_ad(p);  // the input vector p
        auto model_ad = model.template cast<autodiff::real>();

        // Compute the jacobian of the hamiltonian wrt q and p
        Eigen::Matrix<autodiff::real, 1, 1> H_ad;


        Eigen::Matrix<Scalar, 1, nq> dH_dq =
            autodiff::jacobian(hamiltonian<autodiff::real, nq>, wrt(q_ad), at(model_ad, q_ad, p_ad), H_ad);

        Eigen::Matrix<Scalar, 1, nq> dH_dp = (model_ad.data.Minv * p_ad).template cast<Scalar>();

        // Create the interconnection and damping matrix
        Eigen::Matrix<Scalar, nx, nx> J = Eigen::Matrix<Scalar, nx, nx>::Zero();
        J.block(0, nq, nq, nq)          = Eigen::Matrix<Scalar, nq, nq>::Identity();
        J.block(nq, 0, nq, nq)          = -1 * Eigen::Matrix<Scalar, nq, nq>::Identity();
        J.block(nq, nq, nq, nq)         = model.data.Dp;

        // Stack the jacobians of the hamiltonian
        Eigen::Matrix<Scalar, nx, 1> dH = Eigen::Matrix<Scalar, nx, 1>::Zero();
        dH.block(0, 0, nq, 1)           = dH_dq.transpose();
        dH.block(nq, 0, nq, 1)          = dH_dp.transpose();

        // Compute the forward dynamics: dx_dt
        Eigen::Matrix<Scalar, nx, ni> G    = Eigen::Matrix<Scalar, nx, ni>::Zero();
        G.block(nq, 0, nq, nq)             = model.data.Gp;
        Eigen::Matrix<Scalar, nx, 1> dx_dt = J * dH + G * u;

        // Store the result
        model.data.dx_dt = dx_dt;
        return dx_dt;
    }

    /**
     * @brief Compute the hamiltonian of the robot model with active constraints.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param p The joint velocity of the robot.
     * @param active_constraints The active set of the robot.
     */
    template <typename Scalar, int nq>
    Eigen::Matrix<Scalar, 1, 1> hamiltonian_with_constraints(Model<Scalar, nq>& model,
                                                             const Eigen::Matrix<Scalar, nq, 1>& q,
                                                             const Eigen::Matrix<Scalar, nq, 1>& p,
                                                             const std::vector<std::string>& active_constraints) {
        // Compute the mass matrix and potential energy for unconstrained dynamics
        mass_matrix<Scalar, nq>(model, q);

        // Compute the jacobian of the active constraints
        Eigen::Matrix<Scalar, Eigen::Dynamic, nq> Jc;
        for (int i = 0; i < active_constraints.size(); i++) {
            // Compute the jacobian of the active constraint
            Eigen::Matrix<Scalar, 3, nq> Jci = RML::Jv(model, q, active_constraints[i]);
            // Vertically Concatenate the jacobian of the active constraint to the jacobian of the active constraints
            Jc.conservativeResize(Jc.rows() + Jci.rows(), nq);
            Jc.block(Jc.rows() - Jci.rows(), 0, Jci.rows(), Jci.cols()) = Jci;
        }
        model.data.Jc.resize(Jc.rows(), Jc.cols());
        model.data.Jc = Jc;

        // Compute the left annihilator of the jacobian of the active constraints
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Jcp = RML::null<Scalar>(Jc);
        model.data.Jcp.resize(Jcp.rows(), Jcp.cols());
        model.data.Jcp = Jcp;

        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Gr = Jcp.transpose() * model.data.Gp;
        model.data.Gr.resize(Gr.rows(), Gr.cols());
        model.data.Gr = Gr;

        model.data.Dp.setZero();
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Dr = Jcp.transpose() * model.data.Dp * Jcp;
        model.data.Dr.resize(Dr.rows(), Dr.cols());
        model.data.Dr = Dr;

        auto Mr = Jcp.transpose() * model.data.M * Jcp;
        model.data.Mr.resize(Mr.rows(), Mr.cols());
        model.data.Mr = Mr;

        model.data.nz = model.data.Gp.rows() - Gr.rows();
        model.data.nr = Mr.rows();

        // Compute the inverse of the mass matrix
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> b;
        b.resize(model.data.nr, model.data.nr);
        b.setIdentity();
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Mrinv = Mr.ldlt().solve(b);
        model.data.Mrinv.resize(model.data.nr, model.data.nr);
        model.data.Mrinv = Mrinv;

        // Compute the kinetic energy
        auto pr      = p.tail(model.data.nr);
        model.data.T = Scalar(0.5 * pr.transpose() * Mrinv * pr);

        // Compute the total energy
        Eigen::Matrix<Scalar, 1, 1> H = Eigen::Matrix<Scalar, 1, 1>(model.data.T + model.data.V);

        return H;
    }

    /**
     * @brief Compute the forward dynamics of the model with active constraints.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param p The momentum vector
     * @param u The input vector.
     * @param active_constraints The active constraints.
     * @return The forward dynamics of the model.
     */
    template <typename Scalar, int nq, int ni>
    Eigen::Matrix<Scalar, nq + nq, 1> forward_dynamics(Model<Scalar, nq>& model,
                                                       const Eigen::Matrix<Scalar, nq, 1>& q,
                                                       const Eigen::Matrix<Scalar, nq, 1>& p,
                                                       const Eigen::Matrix<Scalar, ni, 1>& u,
                                                       const std::vector<std::string>& active_constraints = {}) {
        // If there are no active constraints, use the quicker forward dynamics
        if (active_constraints.size() == 0) {
            return forward_dynamics_without_constraints(model, q, p, u);
        }
        // Cast to autodiff type for automatic differentiation
        Eigen::Matrix<autodiff::real, nq, 1> q_ad(q);
        Eigen::Matrix<autodiff::real, nq, 1> p_ad(p);
        auto model_ad = model.template cast<autodiff::real>();

        // Compute the jacobian of the hamiltonian wrt q and p
        Eigen::Matrix<autodiff::real, 1, 1> H_ad;
        Eigen::Matrix<Scalar, 1, nq> dH_dq = autodiff::jacobian(hamiltonian_with_constraints<autodiff::real, nq>,
                                                                wrt(q_ad),
                                                                at(model_ad, q_ad, p_ad, active_constraints),
                                                                H_ad);

        Eigen::Matrix<autodiff::real, Eigen::Dynamic, 1> pr_ad = p_ad.tail(model_ad.data.nr);
        Eigen::Matrix<Scalar, Eigen::Dynamic, 1> dH_dp         = (model_ad.data.Mrinv * pr_ad).template cast<Scalar>();

        Eigen::Matrix<Scalar, nq + nq, 1> dx_dt = Eigen::Matrix<Scalar, nq + nq, 1>::Zero();
        // qdot
        dx_dt.block(0, 0, nq, 1) = (model_ad.data.Jcp * dH_dp).template cast<Scalar>();

        // pdot
        dx_dt.block(nq + model_ad.data.nz, 0, model_ad.data.nr, 1) =
            (-model_ad.data.Jcp.transpose() * dH_dq.transpose() - model_ad.data.Dr * dH_dp + model_ad.data.Gr * u)
                .template cast<Scalar>();

        // Store the result
        model.data.dx_dt = dx_dt;
        return dx_dt;
    }


    /**
     * @brief Creates a vector of holonomic constraints.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     * @param M The over-paremeterised mass matrix.
     * @param V The over-paremeterised potential energy.
     * @return The constraint function for holonomic constraints.
     */
    template <typename Scalar, int nq>
    Eigen::Matrix<Scalar, Eigen::Dynamic, 1> holonomic_constraints(
        Model<Scalar, nq>& model,
        const Eigen::Matrix<Scalar, nq, 1>& q,
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>& M,
        Scalar& V) {

        std::vector<Scalar> Mp;
        std::vector<Scalar> Jp;
        Eigen::Matrix<Scalar, Eigen::Dynamic, 1> fc;

        // // Get the base link from the model
        auto base_link = model.links[model.base_link_idx];

        // Create over parametrised system
        auto start = std::chrono::high_resolution_clock::now();
        for (auto link : model.links) {
            // Compute FK to centre of mass
            Eigen::Transform<Scalar, 3, Eigen::Affine> Hbm =
                forward_kinematics_com<Scalar, nq>(model, q, base_link.name, link.name);
            Eigen::Matrix<Scalar, 3, 1> rMBb = Hbm.translation();
            // Add links contribution to potential energy m* g* h
            V -= link.mass * model.gravity.transpose() * rMBb;

            if (link.link_idx != -1 && model.joints[link.joint_idx].type == RML::JointType::REVOLUTE) {
                // Add to mass matrix list
                Mp.insert(Mp.end(), {link.mass, link.mass, link.mass});
                // Add inertia to J matrix TODO: Need to figure out inertia contribution
                Jp.insert(Jp.end(), {0});
                // Add to constraint vector
                fc.conservativeResize(fc.rows() + 3);
                fc.tail(3) = rMBb;
            }
            else if (link.link_idx != -1 && model.joints[link.joint_idx].type == RML::JointType::FIXED) {
                // Add to mass matrix list
                Mp.insert(Mp.end(), {link.mass, link.mass, link.mass});
                // Add to constraint vector
                fc.conservativeResize(fc.rows() + 3);
                fc.tail(3) = rMBb;
            }
            else if (link.link_idx != -1 && model.joints[link.joint_idx].type == RML::JointType::PRISMATIC) {
                // Add inertia to J matrix TODO: Need to figure out inertia contribution
                Jp.insert(Jp.end(), {0});
            }
        }
        // Compute reduced system
        M.resize(Mp.size() + Jp.size(), Mp.size() + Jp.size());
        M.setZero();
        auto stop     = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
        // std::cout << "Builindg M " << duration.count() << " microseconds" << std::endl;
        for (int i = 0; i < Jp.size(); i++) {
            M(i, i) = Jp[i];
        }
        for (int i = 0; i < Mp.size(); i++) {
            M(Jp.size() + i, Jp.size() + i) = Mp[i];
        }
        return fc;
    }

    /**
     * @brief Eliminates holonomic constraints from the dynamic equations of motion via the appropriate selection
     * of generalised coordinates.
     * @param model The robot model.
     * @param q_real The joint configuration of the robot.
     * @param M The mass matrix.
     * @param dfcdqh The jacobian of the holonomic constraints.
     * @return The mass matrix of the reduced system.
     */
    template <typename Scalar, int nq>
    void holonomic_reduction(Model<Scalar, nq>& model,
                             Eigen::Matrix<Scalar, nq, 1>& q_real,
                             Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>& M,
                             const Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>& dfcdqh) {
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Mh =
            Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>::Zero(nq, nq);
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> Q;
        Q.resize(nq + dfcdqh.rows(), dfcdqh.cols());
        Q.block(0, 0, nq, dfcdqh.cols()) = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>::Identity(nq, nq);
        Q.block(nq, 0, dfcdqh.rows(), dfcdqh.cols()) = dfcdqh.template cast<Scalar>();
        Mh                                           = Q.transpose() * M * Q;
        M.resize(nq, nq);
        M = Mh;
    }

    /**
     * @brief Compute the mass matrix, coriolis and gravity matrices of the robot model.
     * @param model The robot model.
     * @param q The joint configuration of the robot.
     */
    template <typename Scalar, int nq>
    void compute_dynamics(Model<Scalar, nq>& model,
                          const Eigen::Matrix<Scalar, nq, 1>& q,
                          Eigen::Matrix<Scalar, nq, nq>& Mh,
                          Eigen::Matrix<Scalar, nq, nq>& Ch,
                          Eigen::Matrix<Scalar, nq, 1>& g,
                          Scalar& Vh) {
        // Create the mass matrix, inertia matrix, constraint vector and potential energy matrices
        Eigen::Matrix<autodiff::real, Eigen::Dynamic, Eigen::Dynamic> M;
        Eigen::Matrix<autodiff::real, Eigen::Dynamic, 1> fc;
        autodiff::real V = 0;

        // Cast to autodiff::real type
        Eigen::Matrix<autodiff::real, nq, 1> q_real(q);
        auto autodiff_model = model.template cast<autodiff::real>();

        // Compute the holonomic constraint vector and its jacobian
        Eigen::Matrix<autodiff::real, Eigen::Dynamic, 1> F;
        Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic> dfcdqh =
            jacobian(holonomic_constraints<autodiff::real, nq>, wrt(q_real), at(autodiff_model, q_real, M, V), F);

        // Compute the mass matrix via holonomic constraint elimination
        holonomic_reduction<autodiff::real, nq>(autodiff_model, q_real, M, dfcdqh);
    };

}  // namespace RML

#endif
