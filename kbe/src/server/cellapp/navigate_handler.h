#ifndef KBE_NAVIGATE_HANDLER_H
#define KBE_NAVIGATE_HANDLER_H

#include "moveto_point_handler.h"
#include "updatable.h"
#include "math/math.h"
#include "pyscript/scriptobject.h"
#include "navigation/navigation.h"
#include "navigation/navigation_mesh_handle.h"

namespace KBEngine {

    class Controller;
    class Entity;

    /**
     * NavigateHandler
     * 基于 Detour NavMesh 的完整寻路控制器
     * - findPath + corridor
     * - funnel straight path
     * - moveAlongSurface steering
     */
    class NavigateHandler : public MoveToPointHandler
    {
    public:
        NavigateHandler(KBEShared_ptr<Controller>& pController, const Position3D& destPos, float velocity, float distance, bool faceMovement,
            float maxMoveDistance, VECTOR_POS3D_PTR paths_ptr,
            PyObject* userarg);

        NavigateHandler(KBEShared_ptr<Controller>& pController,
            const Position3D& destPos,
            float distance,
            float velocity,
            int8 layer,
            float maxMoveDistance,
            bool faceMovement,
            PyObject* userarg,
            bool useDetour);

        NavigateHandler();
        virtual ~NavigateHandler();

        void addToStream(KBEngine::MemoryStream& s);
        void createFromStream(KBEngine::MemoryStream& s);

        virtual bool update() override;

        void destroy() { isDestroyed_ = true; }

        float velocity() const { return velocity_; }
        void velocity(float v) { velocity_ = v; }

        virtual MoveType type() const { return MOVE_TYPE_NAV; }
        virtual bool isOnGround() { return false; }

        virtual bool requestMoveOver(const Position3D& oldPos);
        virtual bool requestMoveFailure();
        virtual const Position3D& destPos() { return destPos_; }

    private:

        int destPosIdx_;
        VECTOR_POS3D_PTR paths_;

        // ---------------- Detour 数据 ----------------
        NavigationHandlePtr navHandle_;
        dtPolyRef polyRef_ = NavMeshHandle::INVALID_NAVMESH_POLYREF;

        dtPolyRef startPoly_ = NavMeshHandle::INVALID_NAVMESH_POLYREF;
        dtPolyRef endPoly_ = NavMeshHandle::INVALID_NAVMESH_POLYREF;

        std::vector<dtPolyRef> polyPath_;
        std::vector<Position3D> straightPath_;
        int currentPathIndex_ = 0;

        bool pathValid_ = false;
        bool useDetour_ = false;

        float maxMoveDistance_ = 0.f;

        float lookAheadDistance_ = 2.0f;  // 每帧目标向前看的距离，可调


    private:
        // ---------------- 内部逻辑 ----------------
        bool buildPath(const Position3D& currPos);
        void invalidatePath();
    };

} // namespace KBEngine

#endif // KBE_NAVIGATE_HANDLER_H
