#ifndef mpCollision_h
#define mpCollision_h

#include "ispc_vectormath.h"
typedef int id_type;
typedef unsigned int uint;
typedef unsigned int16 uint16;
typedef unsigned int32 uint32;

struct Plane
{
    vec3f normal;
    float distance;
};

struct BoundingBox
{
    vec3f bl, ur;
};

struct Sphere
{
    vec3f center;
    float radius;
};

struct Capsule
{
    vec3f center, pos1, pos2;
    float radius;
    float rcp_lensq; // for optimization
};

struct Box
{
    vec3f center;
    Plane planes[6];
};


struct ColliderProperties
{
    int owner_id;
    float stiffness;
    void *hit_handler;
    void *force_handler;
};

struct PlaneCollider
{
    ColliderProperties props;
    BoundingBox bounds;
    Plane shape;
};

struct SphereCollider
{
    ColliderProperties props;
    BoundingBox bounds;
    Sphere shape;
};

struct CapsuleCollider
{
    ColliderProperties props;
    BoundingBox bounds;
    Capsule shape;
};

struct BoxCollider
{
    ColliderProperties props;
    BoundingBox bounds;
    Box shape;
};


enum ForceShape
{
    FS_AffectAll,
    FS_Sphere,
    FS_Capsule,
    FS_Box,
};

enum ForceDirection
{
    FD_Directional,
    FD_Radial,
    FD_RadialCapsule,
    FD_VectorField, //
};

struct ForceProperties
{
    int     shape_type; // ForceShape
    int     dir_type; // ForceDirection
    float   strength_near;
    float   strength_far;
    float   range_inner;
    float   range_outer;
    float   attenuation_exp;

    vec3f   directional_pos;
    vec3f   directional_dir;
    vec3f   radial_center;

    float   directional_plane_distance;
    float   rcp_range;
};

struct Force
{
    ForceProperties props;
    BoundingBox     bounds;
    Sphere          sphere;
    Capsule         capsule;
    Box             box;
};

struct Cell
{
    int begin, end;
    int soai;
    float density;
};

struct KernelParams
{
    vec3f world_center;
    vec3f world_extent;
    vec3i world_div;
    vec3f active_region_center;
    vec3f active_region_extent;
    vec3f coord_scaler;

    int solver_type;
    int enable_interaction;
    int enable_colliders;
    int enable_forces;
    int id_as_float;

    float timestep;
    float damping;
    float advection;
    float pressure_stiffness;

    int max_particles;
    float particle_size;

    float SPHRestDensity;
    float SPHParticleMass;
    float SPHViscosity;

    float RcpParticleSize2;
    float SPHDensityCoef;
    float SPHGradPressureCoef;
    float SPHLapViscosityCoef;
};

#endif // mpCollision_h