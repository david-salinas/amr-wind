
#ifndef DIFFUSION_H
#define DIFFUSION_H

#include "AMReX_MLMG.H"
#include "amr-wind/core/FieldRepo.H"

namespace diffusion {

void wall_model_bc_moeng(
    amr_wind::Field& velocity,
    const amrex::Real utau,
    const amr_wind::FieldState fstate,
    const amrex::FArrayBox& instplanar);

void wall_model_bc(
    amr_wind::Field& velocity,
    const amrex::Real utau,
    const amrex::Real umag,
    const amr_wind::FieldState fstate);

void temp_wall_model_bc(
    amr_wind::Field& temperature,
    const amrex::FArrayBox& instplanar,
    const amr_wind::FieldState fstate);

amrex::Vector<amrex::Array<amrex::LinOpBCType, AMREX_SPACEDIM>>
get_diffuse_tensor_bc(
    amr_wind::Field& velocity, amrex::Orientation::Side side) noexcept;

amrex::Array<amrex::LinOpBCType, AMREX_SPACEDIM> get_diffuse_scalar_bc(
    amr_wind::Field& scalar, amrex::Orientation::Side side) noexcept;

amrex::Array<amrex::MultiFab, AMREX_SPACEDIM> average_velocity_eta_to_faces(
    const amrex::Geometry& geom, amrex::MultiFab const& cc_eta);

void fixup_eta_on_domain_faces(
    const amrex::Geometry& geom,
    amrex::Array<amrex::MultiFab, AMREX_SPACEDIM>& fc,
    amrex::MultiFab const& cc);

void viscosity_to_uniform_space(
    amrex::Array<amrex::MultiFab, AMREX_SPACEDIM>& b,
    const amr_wind::FieldRepo& repo,
    int lev);

} // namespace diffusion

#endif /* DIFFUSION_H */
