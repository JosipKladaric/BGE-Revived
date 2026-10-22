#include "bge/physics/physics_backend.h"

#include "bge/core/log.h"

#if defined(BGE_WITH_JOLT)
#include "Jolt/Jolt.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#endif

namespace bge {

#if defined(BGE_WITH_JOLT)
namespace {

namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer COUNT = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr JPH::uint COUNT = 2;
} // namespace BroadPhaseLayers

class BounceBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::COUNT; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == 0 ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad_phase) const override {
        if (layer == Layers::NON_MOVING) {
            return static_cast<JPH::BroadPhaseLayer::Type>(broad_phase) ==
                   static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING);
        }
        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::NON_MOVING) return b == Layers::MOVING;
        return true;
    }
};

void trace_impl(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    BGE_INFO("[jolt] %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool assert_failed_impl(const char* expression, const char* message, const char* file, JPH::uint line) {
    BGE_ERROR("[jolt] assert failed: %s:%d: (%s) %s", file, line, expression, message ? message : "");
    return true;
}
#endif

JPH::Vec3 to_jolt(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
Vec3 from_jolt(const JPH::Vec3& v) { return Vec3(v.GetX(), v.GetY(), v.GetZ()); }

JPH::Quat to_jolt(const Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
Quat from_jolt(const JPH::Quat& q) { return Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

} // namespace

// The Jolt backed world.
class JoltPhysicsWorld final : public PhysicsBackend {
public:
    JoltPhysicsWorld() = default;
    ~JoltPhysicsWorld() override { shutdown(); }

    bool init() {
        if (ready_) return true;
        JPH::RegisterDefaultAllocator();
        JPH::Trace = trace_impl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assert_failed_impl;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        temp_allocator_ = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        job_system_ = std::make_unique<JPH::JobSystemThreadPool>();
        job_system_->Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
        physics_system_.Init(1024, 0, 64, 1024, m_bp_interface, m_object_vs_bp_filter, m_object_pair_filter);
        ready_ = true;
        BGE_INFO("Physics: Jolt %d.%d.%d ready", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
        return true;
    }

    void shutdown() {
        if (!ready_) return;
        ready_ = false;
        // Release every body we still own before tearing the system down.
        JPH::BodyInterface& api = physics_system_.GetBodyInterface();
        for (JPH::Body* body : bodies_) {
            if (!body) continue;
            api.RemoveBody(body->GetID());
            api.DestroyBody(body->GetID());
        }
        bodies_.clear();
        job_system_.reset();
        temp_allocator_.reset();
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    uint32_t create_body(const PhysicsBodyDesc& desc) override {
        if (!ready_) init();
        JPH::ShapeSettings::ShapeResult shape;
        if (desc.sphere) {
            shape = JPH::SphereShapeSettings(desc.radius).Create();
        } else {
            shape = JPH::BoxShapeSettings(to_jolt(desc.half_extents)).Create();
        }
        if (shape.HasError()) {
            BGE_ERROR("[jolt] shape error: %s", shape.GetError().c_str());
            return 0;
        }

        JPH::BodyCreationSettings settings(shape.Get(), to_jolt(desc.position), to_jolt(desc.rotation),
                                           desc.is_static ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                                         desc.is_static ? Layers::NON_MOVING : Layers::MOVING);
        settings.mRestitution = desc.restitution;
        settings.mFriction = desc.friction;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;

        JPH::BodyInterface& api = physics_system_.GetBodyInterface();
        JPH::Body* body = api.CreateBody(settings);
        if (!body) {
            BGE_ERROR("[jolt] could not create the body");
            return 0;
        }
        api.AddBody(body->GetID(), desc.is_static ? JPH::EActivation::DontActivate
                                                 : JPH::EActivation::Activate);
        bodies_.push_back(body);
        // Handles are 1 based so that 0 always means "no body".
        return static_cast<uint32_t>(bodies_.size());
    }

    void destroy_body(uint32_t body) override {
        if (body == 0 || body > bodies_.size()) return;
        JPH::Body* handle = bodies_[body - 1];
        if (!handle) return;
        JPH::BodyInterface& api = physics_system_.GetBodyInterface();
        api.RemoveBody(handle->GetID());
        api.DestroyBody(handle->GetID());
        bodies_[body - 1] = nullptr;
    }

    bool valid(uint32_t body) const override {
        return body > 0 && body <= bodies_.size() && bodies_[body - 1] != nullptr;
    }

    void set_transform(uint32_t body, const Vec3& position, const Quat& rotation) override {
        JPH::Body* handle = body_of(body);
        if (!handle) return;
        JPH::BodyInterface& api = physics_system_.GetBodyInterface();
        api.SetPositionAndRotation(handle->GetID(), to_jolt(position), to_jolt(rotation),
                                   JPH::EActivation::Activate);
        api.SetLinearAndAngularVelocity(handle->GetID(), JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }

    void set_velocity(uint32_t body, const Vec3& velocity) override {
        JPH::Body* handle = body_of(body);
        if (!handle) return;
        physics_system_.GetBodyInterface().SetLinearVelocity(handle->GetID(), to_jolt(velocity));
    }

    void apply_impulse(uint32_t body, const Vec3& impulse) override {
        JPH::Body* handle = body_of(body);
        if (!handle) return;
        physics_system_.GetBodyInterface().AddImpulse(handle->GetID(), to_jolt(impulse));
    }

    Vec3 position(uint32_t body) const override {
        JPH::Body* handle = body_of(body);
        if (!handle) return Vec3{0, 0, 0};
        return from_jolt(handle->GetPosition());
    }

    Quat rotation(uint32_t body) const override {
        JPH::Body* handle = body_of(body);
        if (!handle) return Quat::identity();
        return from_jolt(handle->GetRotation());
    }

    Vec3 velocity(uint32_t body) const override {
        JPH::Body* handle = body_of(body);
        if (!handle) return Vec3{0, 0, 0};
        return from_jolt(handle->GetLinearVelocity());
    }

    void step(float delta_seconds) override {
        if (!ready_ || delta_seconds <= 0.0f) return;
        physics_system_.Update(delta_seconds, 1, temp_allocator_.get(), job_system_.get());
    }

    size_t body_count() const override { return bodies_.size(); }
    const char* name() const override { return "jolt"; }

private:
    JPH::Body* body_of(uint32_t body) const {
        if (body == 0 || body > bodies_.size()) return nullptr;
        return bodies_[body - 1];
    }

    BounceBroadPhaseLayerInterface m_bp_interface;
    ObjectVsBroadPhaseLayerFilter m_object_vs_bp_filter;
    ObjectLayerPairFilter m_object_pair_filter;
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system_;
    JPH::PhysicsSystem physics_system_;
    std::vector<JPH::Body*> bodies_;
    bool ready_ = false;
};

#endif // BGE_WITH_JOLT

std::unique_ptr<PhysicsBackend> create_physics_backend(bool prefer_jolt) {
#if defined(BGE_WITH_JOLT)
    if (prefer_jolt) {
        auto world = std::make_unique<JoltPhysicsWorld>();
        if (world->init()) return world;
    }
#else
    (void)prefer_jolt;
#endif
    return nullptr;  // the caller falls back to the built-in solver
}

bool jolt_available() {
#if defined(BGE_WITH_JOLT)
    return true;
#else
    return false;
#endif
}

} // namespace bge
