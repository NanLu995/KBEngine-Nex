// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_NAVIGATEMESHHANDLE_H
#define KBE_NAVIGATEMESHHANDLE_H

#include "navigation/navigation_handle.h"

#include "recastnavigation/DetourNavMeshBuilder.h"
#include "recastnavigation/DetourNavMeshQuery.h"
#include "recastnavigation/DetourCommon.h"
#include "recastnavigation/DetourNavMesh.h"

namespace KBEngine {

	struct NavMeshSetHeader
	{
		int version;
		int tileCount;
		dtNavMeshParams params;
	};

	struct NavMeshSetHeaderEx
	{
		int magic;
		int version;
		int tileCount;
		dtNavMeshParams params;
	};

	struct NavMeshTileHeader
	{
		dtTileRef tileRef;
		int dataSize;
	};

	class NavMeshHandle : public NavigationHandle
	{
	public:
		static const int MAX_POLYS = 256;
		static const int NAV_ERROR_NEARESTPOLY = -2;

		static const long RCN_NAVMESH_VERSION = 1;
		static const int INVALID_NAVMESH_POLYREF = 0;

		struct NavmeshLayer
		{
			dtNavMesh* pNavmesh;
			dtNavMeshQuery* pNavmeshQuery;
		};

	public:
		NavMeshHandle();
		~NavMeshHandle() override;

		int findStraightPath(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& paths) override;

		int findRandomPointAroundCircle(int layer, const Position3D& centerPos, std::vector<Position3D>& points,
			uint32 max_points, float maxRadius) override;

		int raycast(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& hitPointVec) override;

		NavigationHandle::NAV_TYPE type() const override { return NAV_MESH; }

		static NavigationHandle* create(std::string resPath, const std::map< int, std::string >& params);
		static bool _create(int layer, const std::string& resPath, const std::string& res, NavMeshHandle* pNavMeshHandle);



		/**
		* 在指定 navmesh layer 中，查找与给定世界坐标最近的可行走 polygon。
		*
		* 【用途】
		* - 用于初始化 Entity 当前所在的 navmesh polygon（polyRef）
		* - 用于 Entity 传送 / 位置修正后的 polyRef 重定位
		* - 用于 moveAlongSurface 失败或 polyRef 丢失时的兜底恢复
		*
		* 【行为说明】
		* - 不会修改 Entity 位置，只是“查询”
		* - 返回的 polyRef 代表 Entity 当前“站在哪一块 navmesh 上”
		* - 可选返回该 polygon 上的最近合法点（投影点）
		*
		* 【重要约束】
		* - polyRef 是 Detour 导航的核心状态，后续所有贴地移动都依赖它
		* - polyRef 必须在同一个 layer 内使用，不能跨 layer 混用
		*
		* @param layer        navmesh 层（KBE 支持多套 navmesh）
		* @param pos          世界坐标
		* @param nearestPt   [可选] 返回 navmesh 上的最近投影点
		*
		* @return
		* - 成功：有效的 dtPolyRef
		* - 失败：INVALID_NAVMESH_POLYREF
		*/
		dtPolyRef findNearestPoly(
			int layer,
			const Position3D& pos,
			Position3D* nearestPt = nullptr
		);

		/**
		* 沿 navmesh 表面从起点朝目标点移动，返回一个合法的落点。
		*
		* 【用途】
		* - NavigateHandler 每帧移动的核心接口
		* - 用于替代“直线位移 + 碰撞”的传统移动方式
		*
		* 【行为说明】
		* - Entity 会被限制在 navmesh 表面上移动
		* - 即使目标点在障碍物之后，也不会穿越 navmesh 边界
		* - 内部会自动更新当前所在的 polygon（polyRef）
		*
		* 【重要特性】
		* - 不做寻路，只做“短距离贴地移动”
		* - 可以跨越多个 polygon
		* - polyRef 必须来自 findNearestPoly 或上一次 moveAlongSurface
		*
		* 【使用约定】
		* - 必须每帧连续调用
		* - inoutPoly 不能为 0，否则必须先重新定位 polyRef
		*
		* @param layer        navmesh 层
		* @param inoutPoly   当前所在 polygon（输入 & 输出）
		* @param start        当前世界坐标
		* @param end          期望移动到的目标坐标（通常是 start + velocity * dt）
		* @param outPos       实际在 navmesh 上的合法位置
		*
		* @return
		* - true  ：移动成功
		* - false ：polyRef 无效或查询失败
		*/
		bool moveAlongSurface(
			int layer,
			dtPolyRef& inoutPoly,
			const Position3D& start,
			const Position3D& end,
			Position3D& outPos
		);

		/**
		* 根据指定 polygon，计算某点在 navmesh 上的正确高度（y 值）。
		*
		* 【用途】
		* - 修正 Entity 在坡道、台阶、多层 navmesh 上的高度
		* - 消除移动过程中 y 轴抖动、悬空或下沉问题
		*
		* 【行为说明】
		* - 使用 Detour 内部的 polygon 几何信息计算高度
		* - 不进行射线检测，不依赖物理系统
		*
		* 【重要说明】
		* - 只能用于 Entity 当前所在的 polygon
		* - 必须在 moveAlongSurface 之后调用
		*
		* @param layer    navmesh 层
		* @param poly     当前所在 polygon
		* @param pos      当前世界坐标（x/z 有效，y 可忽略）
		*
		* @return
		* - 成功：navmesh 计算出的高度
		* - 失败：原始 pos.y
		*/
		float getPolyHeight(
			int layer,
			dtPolyRef poly,
			const Position3D& pos
		);

		std::map<int, NavmeshLayer> navmeshLayer;

	private:
		/* Derives overlap polygon of two polygon on the xz-plane.
			@param[in]		polyVertsA		Vertices of polygon A.
			@param[in]		nPolyVertsA		Vertices number of polygon A.
			@param[in]		polyVertsB		Vertices of polygon B.
			@param[in]		nPolyVertsB		Vertices number of polygon B.
			@param[out]		intsectPt		Vertices of overlap polygon.
			@param[out]		intsectPtCount	Vertices number of overlap polygon.
		*/
		void getOverlapPolyPoly2D(const float* polyVertsA, const int nPolyVertsA, const float* polyVertsB, const int nPolyVertsB, float* intsectPt, int* intsectPtCount);

		/* Sort vertices to clockwise. */
		void clockwiseSortPoints(float* verts, const int nVerts);

		/* Determines if two segment cross on xz-plane. */
		bool isSegSegCross2D(const float* p1, const float *p2, const float* q1, const float* q2);
};

}

#endif // KBE_NAVIGATEMESHHANDLE_H
