#include "cellapp.h"
#include "entity.h"
#include "navigate_handler.h"

#include "controller.h"
#include "move_controller.h"
#include "navigation/navigation.h"
#include "navigation/navigation_mesh_handle.h"

namespace KBEngine
{
    //-------------------------------------------------------------------------------------
    NavigateHandler::NavigateHandler(KBEShared_ptr<Controller>& pController, const Position3D& destPos,
        float velocity, float distance, bool faceMovement,
        float maxMoveDistance, VECTOR_POS3D_PTR paths_ptr,
        PyObject* userarg) :
        MoveToPointHandler(pController, pController->pEntity()->layer(), pController->pEntity()->position(), velocity, distance, faceMovement, false, userarg),
        destPosIdx_(0),
        paths_(paths_ptr),
        maxMoveDistance_(maxMoveDistance)
    {
        destPos_ = (*paths_)[destPosIdx_++];

        updatableName = "NavigateHandler";
    }


    // ------------------------------------------------------------
    // ctor / dtor
    // ------------------------------------------------------------
    NavigateHandler::NavigateHandler(KBEShared_ptr<Controller>& pController,
        const Position3D& destPos,
        float distance,
        float velocity,
        int8 layer,
        float maxMoveDistance,
        bool faceMovement,
        PyObject* userarg,
        bool useDetour)
        : MoveToPointHandler(pController, layer, destPos, velocity, distance, faceMovement, false, userarg),
        navHandle_(nullptr),
        polyRef_(NavMeshHandle::INVALID_NAVMESH_POLYREF),
        currentPathIndex_(0),
        pathValid_(false),
        useDetour_(useDetour),
        destPosIdx_(0),
        paths_(),
        maxMoveDistance_(maxMoveDistance)
    {
        updatableName = "NavigateHandler";
        //Py_INCREF(pyuserarg_);

        if (!useDetour_ || !pController_ || !pController_->pEntity())
            return;

        SpaceMemory* pSpace = SpaceMemorys::findSpace(pController_->pEntity()->spaceID());
        if (!pSpace || !pSpace->isGood())
            return;

        navHandle_ = pSpace->pNavHandle();
    }

    NavigateHandler::NavigateHandler()
        : MoveToPointHandler(),
        navHandle_(nullptr),
        polyRef_(NavMeshHandle::INVALID_NAVMESH_POLYREF),
        currentPathIndex_(0),
        pathValid_(false),
        useDetour_(false),
        maxMoveDistance_(0.f)
    {
        updatableName = "NavigateHandler";
    }

    NavigateHandler::~NavigateHandler()
    {
        /*if (pyuserarg_)
            Py_DECREF(pyuserarg_);*/
    }

    // ------------------------------------------------------------
    // serialization
    // ------------------------------------------------------------
    void NavigateHandler::addToStream(KBEngine::MemoryStream& s)
    {
        MoveToPointHandler::addToStream(s);
        s << maxMoveDistance_;
    }

    void NavigateHandler::createFromStream(KBEngine::MemoryStream& s)
    {
        MoveToPointHandler::createFromStream(s);
        s >> maxMoveDistance_;
    }

    // ------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------
    void NavigateHandler::invalidatePath()
    {
        pathValid_ = false;
        straightPath_.clear();
        currentPathIndex_ = 0;
    }

    bool NavigateHandler::buildPath(const Position3D& currPos)
    {
        if (!navHandle_)
            return false;

        KBE_ASSERT(navHandle_->type() == NavigationHandle::NAV_MESH);
        NavMeshHandle* navMeshHandle = static_cast<NavMeshHandle*>(navHandle_.get());

        straightPath_.clear();

        int n = navMeshHandle->findStraightPath(
            layer_,
            currPos,
            destPos_,
            straightPath_);

        if (n <= 0 || straightPath_.empty())
            return false;

        currentPathIndex_ = 0;
        pathValid_ = true;

        // 初始化 polyRef
        polyRef_ = navMeshHandle->findNearestPoly(layer_, currPos, nullptr);
        return polyRef_ != NavMeshHandle::INVALID_NAVMESH_POLYREF;
    }

    // ------------------------------------------------------------
    // move over
    // ------------------------------------------------------------
    bool NavigateHandler::requestMoveOver(const Position3D& oldPos)
    {
        if (useDetour_)
        {
            if (pController_)
            {
                if (pController_->pEntity())
                    pController_->pEntity()->onMoveOver(
                        pController_->id(), layer_, oldPos, pyuserarg_);

                pController_->destroy();
            }
            return true;
        }
        else {
            if (destPosIdx_ == ((int)paths_->size()))
                return MoveToPointHandler::requestMoveOver(oldPos);
            else
                destPos_ = (*paths_)[destPosIdx_++];

            return false;
        }
        
    }

    bool NavigateHandler::requestMoveFailure()
    {
        if (pController_)
        {
            if (pController_->pEntity())
                pController_->pEntity()->onMoveFailure(
                    pController_->id(),  pyuserarg_);

            pController_->destroy();
        }
        return true;
    }


    //-------------------------------------------------------------------------------------
    bool NavigateHandler::stepMoveOnceWithoutDelete() {
        
        if (!useDetour_)
            return MoveToPointHandler::stepMoveOnceWithoutDelete();


        if (isDestroyed_)
        {
            return false;
        }

        if (!pController_ || !pController_->pEntity() || !navHandle_)
        {
            requestMoveFailure();
            return false;
        }
        
        if ( useDetour_ && navHandle_->type() == NavigationHandle::NAV_TILE)
        {
            ERROR_MSG("navigateToDetour does not support 2D.");
            return false;
        }

        NavMeshHandle* navMeshHandle = static_cast<NavMeshHandle*>(navHandle_.get());
        


        Entity* pEntity = pController_->pEntity();
        Py_INCREF(pEntity);

        Position3D currPos = pEntity->position();
        Position3D oldPos = currPos;
        
        pEntity->isOnNavigate(true);

        // 建路
        if (!pathValid_)
        {
            if (!buildPath(currPos))
            {
                retryCount_++;

                if (retryCount_ > 5)
                {
                    requestMoveFailure();
                    // pEntity->isOnNavigate(false);
                    Py_DECREF(pEntity);
                    return false;
                }

                Py_DECREF(pEntity);
                return true;
            }
            retryCount_ = 0;
        }

        if (straightPath_.empty())
        {
            requestMoveOver(oldPos);
            // pEntity->isOnNavigate(false);
            Py_DECREF(pEntity);
            return false;
        }

        // -----------------------------
        // 1. look-ahead 获取目标点
        // -----------------------------
        Position3D moveTarget = currPos;
        float remainingLookAhead = lookAheadDistance_;
        int idx = currentPathIndex_;

        while (idx < (int)straightPath_.size() && remainingLookAhead > 0.f)
        {
            Vector3 segment = straightPath_[idx] - moveTarget;
            float segLen = segment.length();

            if (segLen > remainingLookAhead)
            {
                KBEVec3Normalize(&segment, &segment);
                moveTarget += segment * remainingLookAhead;
                break;
            }
            else
            {
                moveTarget = straightPath_[idx];
                remainingLookAhead -= segLen;
                idx++;
            }
        }

        currentPathIndex_ = idx;

        // -----------------------------
        // 2. 计算移动向量
        // -----------------------------
        Vector3 moveDir = moveTarget - currPos;
        float dist = moveDir.length();

        if (dist < 0.05f)
        {
            // 推进路径索引，避免卡死
            currentPathIndex_++;

            // 如果已经是最后一个点
            if (currentPathIndex_ >= (int)straightPath_.size())
            {
                requestMoveOver(currPos);
                // pEntity->isOnNavigate(false);
                Py_DECREF(pEntity);
                return false;
            }

            Py_DECREF(pEntity);
            return true;
        }


        KBEVec3Normalize(&moveDir, &moveDir);
        moveDir *= velocity_;

        if (maxMoveDistance_ > 0.f && moveDir.length() > maxMoveDistance_)
        {
            KBEVec3Normalize(&moveDir, &moveDir);
            moveDir *= maxMoveDistance_;
        }

        if (!polyRef_)
        {
            invalidatePath();
            Py_DECREF(pEntity);
            return true;
        }


        // 极端情况下被释放了
        if (!navHandle_)
        {
            Py_DECREF(pEntity);
            return false;
        }

        // -----------------------------
        // 3. moveAlongSurface
        // -----------------------------
        Position3D nextPos;
        bool moved = navMeshHandle->moveAlongSurface(
            layer_,
            polyRef_,
            currPos,
            currPos + moveDir,
            nextPos);

        if (!moved)
        {
            retryCount_++;

            if (retryCount_ > 5)
            {
                ERROR_MSG("NavigateHandler::update: move failed too many times\n");

                requestMoveFailure();
                // pEntity->isOnNavigate(false);
                Py_DECREF(pEntity);
                return false;
            }

            invalidatePath();
            Py_DECREF(pEntity);
            return true;
        }

        float h = navMeshHandle->getPolyHeight(layer_, polyRef_, nextPos);

        if (h <= -FLT_MAX) // 或你引擎里的无效值判断
        {
            invalidatePath();
            Py_DECREF(pEntity);
            return true;
        }

        nextPos.y = h;

        // -----------------------------
        // 4. 设置方向与位置
        // -----------------------------
        Direction3D dir = pEntity->direction();
        if (faceMovement_ && (moveDir.x != 0.f || moveDir.z != 0.f))
            dir.yaw(moveDir.yaw());

        if (!isDestroyed_)
            pEntity->setPositionAndDirection(nextPos, dir);
        if (!isDestroyed_)
            pEntity->isOnGround(true);

        

        //pEntity->isOnGround(true);
        if (!isDestroyed_)
            pEntity->onMove(pController_->id(), layer_, oldPos, pyuserarg_);

        // -----------------------------
        // 5. 到达终点判断
        // -----------------------------
        float sqrDistToDest = (destPos_ - nextPos).squaredLength();
        if (isDestroyed_ || sqrDistToDest <= distance_ * distance_)
        {

            // pEntity->isOnNavigate(false);
            requestMoveOver(nextPos);
            Py_DECREF(pEntity);
            return false;
        }

        Py_DECREF(pEntity);
        return true;
    }

    // ------------------------------------------------------------
    // update
    // ------------------------------------------------------------
    bool NavigateHandler::update()
    {
        if (!stepMoveOnceWithoutDelete())
        {
            delete this;
            return false;
        }

        return true;
        
    }


} // namespace KBEngine
