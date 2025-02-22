#ifndef FIELDBCOPS_H
#define FIELDBCOPS_H

#include "amr-wind/core/FieldDescTypes.H"
#include "amr-wind/core/Field.H"

#include "AMReX_Gpu.H"
#include "AMReX_FArrayBox.H"
#include "AMReX_Geometry.H"
#include "AMReX_PhysBCFunct.H"

namespace amr_wind {

class Field;

/** Interface to setup up boundary conditions on a field
 *  \ingroup field_bc
 *
 *  This is the base class for all non-standard operations that need to be
 *  performed at a domain boundary. Examples include using a wall-function to
 *  specify shear-stress at domain boundary for viscous flows, and setting an
 *  inhomogeneous Neumann (e.g., non-zero gradient) BC for temperature above
 *  capping inversion.
 *
 *  Customized BC operators are registered on a field using
 *  Field::register_custom_bc and then called during the linear system solve in
 *  \ref eqsys by calling Field::apply_bc_funcs This operation happens after the
 *  Field::fillphysbc is called and the default BC values have been populated in
 *  ghost cells.
 */
class FieldBCIface
{
public:
    virtual ~FieldBCIface() = default;

    virtual void operator()(Field& field, const FieldState rho_state) = 0;
};

/** A no-op BC
 *  \ingroup field_fillpatch
 */
struct FieldBCNoOp
{
    using FunctorType = FieldBCNoOp;

    AMREX_GPU_HOST
    constexpr FieldBCNoOp() = default;

    AMREX_GPU_HOST
    constexpr explicit FieldBCNoOp(const Field& /*unused*/) {}

    FunctorType operator()() const { return *this; }

    AMREX_GPU_DEVICE
    void operator()(
        const amrex::IntVect& /* iv */,
        amrex::Array4<amrex::Real> const& /* field */,
        const int /* dcomp */,
        const int /* numcomp */,
        amrex::GeometryData const& /* geom */,
        const amrex::Real /* time */,
        const amrex::BCRec* /* bcr */,
        const int /* bcomp */,
        const int /* orig_comp */) const
    {}

    AMREX_GPU_DEVICE
    inline void set_inflow(
        const amrex::IntVect& /*unused*/,
        amrex::Array4<amrex::Real> const& /*unused*/,
        amrex::GeometryData const& /*unused*/,
        const amrex::Real /*unused*/,
        amrex::Orientation /*unused*/,
        const int /*unused*/,
        const int /*unused*/,
        const int /*unused*/) const
    {}
};

struct ConstDirichlet
{
    using DeviceType = ConstDirichlet;

    int m_ncomp;
    amrex::GpuArray<const amrex::Real*, AMREX_SPACEDIM * 2> m_bcv;

    AMREX_GPU_HOST
    explicit ConstDirichlet(const Field& fld)
        : m_ncomp(fld.num_comp()), m_bcv(fld.bc_values_device())
    {}

    AMREX_GPU_HOST
    DeviceType device_instance() const { return *this; }

    AMREX_GPU_DEVICE
    inline void operator()(
        const amrex::IntVect& iv,
        amrex::Array4<amrex::Real> const& field,
        amrex::GeometryData const& /*unused*/,
        const amrex::Real /*unused*/,
        amrex::Orientation ori,
        const int comp,
        const int dcomp,
        const int orig_comp) const
    {
        field(iv[0], iv[1], iv[2], dcomp + comp) = m_bcv[ori][orig_comp + comp];
    }
};

/** Sets constant values on specified boundaries
 *  \ingroup field_fillpatch
 */
template <typename InflowOp, typename WallOp>
struct DirichletOp
{
    using InflowOpType = InflowOp;
    using WallOpType = WallOp;

    int m_ncomp;
    amrex::GpuArray<BC, AMREX_SPACEDIM * 2> m_bc_type;
    const InflowOpType m_inflow_op;
    const WallOpType m_wall_op;

    AMREX_GPU_HOST
    constexpr explicit DirichletOp(const Field& fld)
        : m_ncomp(fld.num_comp())
        , m_bc_type(fld.bc_type())
        , m_inflow_op(fld)
        , m_wall_op(fld)
    {}

    AMREX_GPU_HOST
    DirichletOp(
        const Field& fld,
        const InflowOpType& inflow_op,
        const WallOpType& wall_op)
        : m_ncomp(fld.num_comp())
        , m_bc_type(fld.bc_type())
        , m_inflow_op(inflow_op)
        , m_wall_op(wall_op)
    {}

    AMREX_GPU_DEVICE
    inline void operator()(
        const amrex::IntVect& iv,
        amrex::Array4<amrex::Real> const& field,
        const int dcomp,
        const int numcomp,
        amrex::GeometryData const& geom,
        const amrex::Real time,
        const amrex::BCRec* bcr,
        const int bcomp,
        const int orig_comp) const
    {
        // do something for external Dirichlet (amrex::BCType::ext_dir)
        const amrex::Box& domain_box = geom.Domain();

        for (int n = 0; n < numcomp; ++n) {
            const amrex::BCRec& bc = bcr[bcomp + n];

            for (amrex::OrientationIter oit; oit != nullptr; ++oit) {
                auto ori = oit();
                const int idir = ori.coordDir();

                // Check if this boundary is a dirichlet type
                const auto bctyp = (ori.isLow() ? bc.lo(idir) : bc.hi(idir));
                if (bctyp != amrex::BCType::ext_dir) {
                    continue;
                }

                // Check if the point in question is on the boundary
                const bool is_bndry =
                    (ori.isLow() ? (iv[idir] < domain_box.smallEnd(idir))
                                 : (iv[idir] > domain_box.bigEnd(idir)));
                if (!is_bndry) {
                    continue;
                }

                if (m_bc_type[ori] == BC::mass_inflow) {
                    m_inflow_op(
                        iv, field, geom, time, ori, n, dcomp, orig_comp);
                } else {
                    m_wall_op(iv, field, geom, time, ori, n, dcomp, orig_comp);
                }
            }

#if 0
            if (bc.lo(0) == amrex::BCType::ext_dir and
                i < domain_box.smallEnd(0)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::x, amrex::Orientation::low)][n];
            } else if (
                bc.hi(0) == amrex::BCType::ext_dir and
                i > domain_box.bigEnd(0)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::x, amrex::Orientation::high)][n];
            }

            if (bc.lo(1) == amrex::BCType::ext_dir and
                j < domain_box.smallEnd(1)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::y, amrex::Orientation::low)][n];
            } else if (
                bc.hi(1) == amrex::BCType::ext_dir and
                j > domain_box.bigEnd(1)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::y, amrex::Orientation::high)][n];
            }

            if (bc.lo(2) == amrex::BCType::ext_dir and
                k < domain_box.smallEnd(2)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::z, amrex::Orientation::low)][n];
            } else if (
                bc.hi(2) == amrex::BCType::ext_dir and
                k > domain_box.bigEnd(2)) {
                field(i, j, k, n) = m_bcv[amrex::Orientation(
                    amrex::Direction::z, amrex::Orientation::high)][n];
            }
#endif
        }
    }

    AMREX_GPU_DEVICE
    inline void set_inflow(
        const amrex::IntVect& iv,
        amrex::Array4<amrex::Real> const& field,
        amrex::GeometryData const& geom,
        const amrex::Real time,
        amrex::Orientation ori,
        const int comp,
        const int dcomp,
        const int orig_comp) const
    {
        m_inflow_op(iv, field, geom, time, ori, comp, dcomp, orig_comp);
    }
};

struct FieldBCDirichlet
{
    using FunctorType = DirichletOp<ConstDirichlet, ConstDirichlet>;

    explicit FieldBCDirichlet(const Field& fld) : m_field(fld) {}

    inline FunctorType operator()() const { return FunctorType(m_field); }

    const Field& m_field;
};

template <typename InflowOp, typename WallOp>
struct BCOpCreator
{
    using InflowOpType = typename InflowOp::DeviceType;
    using WallOpType = typename WallOp::DeviceType;
    using FunctorType = DirichletOp<InflowOpType, WallOpType>;

    explicit BCOpCreator(const Field& fld)
        : m_field(fld), m_inflow_op(fld), m_wall_op(fld)
    {}

    explicit BCOpCreator(
        const Field& fld, const InflowOp& inflow_op, const WallOp& wall_op)
        : m_field(fld), m_inflow_op(inflow_op), m_wall_op(wall_op)
    {}

    inline FunctorType operator()() const
    {
        return FunctorType{
            m_field, m_inflow_op.device_instance(),
            m_wall_op.device_instance()};
    }

    const Field& m_field;
    InflowOp m_inflow_op;
    WallOp m_wall_op;
};

} // namespace amr_wind

#endif /* FIELDBCOPS_H */
