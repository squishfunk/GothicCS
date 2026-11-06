// GothicCS - collision world: a triangle soup in CS space with a uniform grid,
// and a swept axis-aligned box trace against it (the player hull).
//
// The trace is an exact separating-axis test for a linearly moving box vs a
// triangle (13 candidate axes: 3 box axes, the triangle normal, 9 edge
// crosses). Like GoldSrc hull traces it stops kDistEpsilon short of the
// surface, so the box never rests exactly on a plane.
#pragma once
#include "Vec.h"
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include <cfloat>

namespace cs
{
	constexpr float kDistEpsilon = 0.03125f;

	struct TraceResult
	{
		float fraction = 1.0f;   // 0..1 of the requested move
		Vec3 endpos;
		Vec3 normal;             // plane hit (points out of the surface, towards the box)
		bool startsolid = false; // the box already overlapped a triangle at the start
		int surface = -1;        // TriWorld::surface of the triangle hit
		int tri = -1;
	};

	// A climbable volume (CS func_ladder); `axis` = its thin horizontal axis (the ladder's normal up to sign).
	struct Ladder { Vec3 mins, maxs, axis; };

	class TriWorld
	{
	public:
		std::vector<Vec3> verts;      // 3 per triangle
		std::vector<uint8_t> surface; // 1 per triangle (material group of the host game)
		std::vector<Ladder> ladders;
		bool cullBackfaces = true;    // triangles are one-sided (outward = counter-clockwise)

		void Clear() { verts.clear(); surface.clear(); ladders.clear(); cells_.clear(); stamp_.clear(); }

		// Ladder touched by the box [mn, mx] (grown by `grow` units); fills the outward normal towards `from`.
		bool LadderAt(const Vec3& mn, const Vec3& mx, const Vec3& from, Vec3& normal, float grow = 2.0f) const
		{
			for (const Ladder& l : ladders)
			{
				if (mx.x < l.mins.x - grow || mn.x > l.maxs.x + grow || mx.y < l.mins.y - grow || mn.y > l.maxs.y + grow ||
					mx.z < l.mins.z || mn.z > l.maxs.z + grow)
					continue;
				const Vec3 c = (l.mins + l.maxs) * 0.5f;
				normal = Dot(from - c, l.axis) >= 0 ? l.axis : -l.axis;
				return true;
			}
			return false;
		}
		int TriCount() const { return (int)surface.size(); }

		void AddTri(const Vec3& a, const Vec3& b, const Vec3& c, uint8_t surf)
		{
			// drop degenerate triangles
			if (Length(Cross(b - a, c - a)) < 1e-4f) return;
			verts.push_back(a); verts.push_back(b); verts.push_back(c);
			surface.push_back(surf);
		}

		void Build(float cellSize = 64.0f)
		{
			cell_ = cellSize;
			cells_.clear();
			const int n = TriCount();
			stamp_.assign(n, 0);
			for (int t = 0; t < n; t++)
			{
				Vec3 mn, mx;
				TriBounds(t, mn, mx);
				int i0, j0, k0, i1, j1, k1;
				CellOf(mn, i0, j0, k0);
				CellOf(mx, i1, j1, k1);
				for (int i = i0; i <= i1; i++)
					for (int j = j0; j <= j1; j++)
						for (int k = k0; k <= k1; k++)
							cells_[Key(i, j, k)].push_back(t);
			}
		}

		// Sweep the box [start+mins, start+maxs] to end.
		TraceResult TraceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const
		{
			TraceResult tr;
			tr.endpos = end;
			const Vec3 half = (maxs - mins) * 0.5f;
			const Vec3 offset = (maxs + mins) * 0.5f;   // box centre relative to origin
			const Vec3 c0 = start + offset;
			const Vec3 d = end - start;

			// candidate triangles from the grid cells covered by the swept box
			Vec3 smin((std::min)(start.x, end.x) + mins.x - 1, (std::min)(start.y, end.y) + mins.y - 1, (std::min)(start.z, end.z) + mins.z - 1);
			Vec3 smax((std::max)(start.x, end.x) + maxs.x + 1, (std::max)(start.y, end.y) + maxs.y + 1, (std::max)(start.z, end.z) + maxs.z + 1);
			int i0, j0, k0, i1, j1, k1;
			CellOf(smin, i0, j0, k0);
			CellOf(smax, i1, j1, k1);
			const uint32_t stamp = ++stampCounter_;

			float best = 1.0f;
			for (int i = i0; i <= i1; i++)
				for (int j = j0; j <= j1; j++)
					for (int k = k0; k <= k1; k++)
					{
						auto it = cells_.find(Key(i, j, k));
						if (it == cells_.end()) continue;
						for (int t : it->second)
						{
							if (stamp_[t] == stamp) continue;
							stamp_[t] = stamp;
							float enter;
							Vec3 n;
							bool solid = false;
							if (!SweepTri(t, c0, d, half, enter, n, solid)) continue;
							if (solid)
							{
								tr.startsolid = true;
								if (best > 0.0f || tr.tri < 0) { best = 0.0f; tr.normal = n; tr.tri = t; tr.surface = surface[t]; }
								continue;
							}
							if (enter < best || (enter == best && tr.tri < 0))
							{
								best = enter;
								tr.normal = n;
								tr.tri = t;
								tr.surface = surface[t];
							}
						}
					}
			if (tr.tri >= 0)
			{
				tr.fraction = best < 0.0f ? 0.0f : best;
				tr.endpos = start + d * tr.fraction;
			}
			return tr;
		}

		// Is the box at `pos` overlapping any triangle?
		bool BoxSolid(const Vec3& pos, const Vec3& mins, const Vec3& maxs) const
		{
			// a zero-length sweep reports startsolid for any real overlap
			return TraceBox(pos, pos + Vec3(0, 0, 0.001f), mins, maxs).startsolid
				&& TraceBox(pos, pos - Vec3(0, 0, 0.001f), mins, maxs).startsolid;
		}

	private:
		float cell_ = 64.0f;
		std::unordered_map<uint64_t, std::vector<int>> cells_;
		mutable std::vector<uint32_t> stamp_;
		mutable uint32_t stampCounter_ = 0;

		static uint64_t Key(int i, int j, int k)
		{
			return (uint64_t(uint32_t(i) & 0x1FFFFF) << 42) | (uint64_t(uint32_t(j) & 0x1FFFFF) << 21) | uint64_t(uint32_t(k) & 0x1FFFFF);
		}
		void CellOf(const Vec3& p, int& i, int& j, int& k) const
		{
			i = (int)std::floor(p.x / cell_);
			j = (int)std::floor(p.y / cell_);
			k = (int)std::floor(p.z / cell_);
		}
		void TriBounds(int t, Vec3& mn, Vec3& mx) const
		{
			const Vec3* v = &verts[t * 3];
			mn = mx = v[0];
			for (int a = 1; a < 3; a++)
				for (int c = 0; c < 3; c++)
				{
					mn[c] = (std::min)(mn[c], v[a][c]);
					mx[c] = (std::max)(mx[c], v[a][c]);
				}
		}

		// Swept SAT. For every axis the box centre's projection must enter the
		// triangle's interval widened by the box radius. Returns false when the
		// sweep never touches the triangle (or only moves away from it).
		bool SweepTri(int t, const Vec3& c0, const Vec3& d, const Vec3& half, float& outEnter, Vec3& outNormal, bool& outSolid) const
		{
			const Vec3* v = &verts[t * 3];
			const Vec3 e[3] = { v[1] - v[0], v[2] - v[1], v[0] - v[2] };
			const Vec3 face = Normalized(Cross(e[0], e[1]));   // outward (front) side
			// one-sided like the game's polygons: a box behind the front side ignores it
			if (cullBackfaces && Dot(c0 - v[0], face) < 0) return false;
			Vec3 axes[13];
			int na = 0;
			axes[na++] = Vec3(1, 0, 0);
			axes[na++] = Vec3(0, 1, 0);
			axes[na++] = Vec3(0, 0, 1);
			axes[na++] = face;
			for (int b = 0; b < 3; b++)
				for (int k = 0; k < 3; k++)
				{
					Vec3 bAxis(b == 0 ? 1.f : 0.f, b == 1 ? 1.f : 0.f, b == 2 ? 1.f : 0.f);
					Vec3 a = Cross(bAxis, e[k]);
					float l = Length(a);
					if (l < 1e-4f * Length(e[k])) continue;
					axes[na++] = a * (1.0f / l);
				}

			float enter = -FLT_MAX, exit = FLT_MAX;
			// motion parallel to a surface (after clipping) must not count as approaching it
			const float minApproach = 1e-4f * Length(d) + 1e-7f;
			Vec3 enterNormal;
			bool approaching = false;
			// for start-solid escape: the axis with the smallest penetration
			float minPen = FLT_MAX;
			Vec3 minPenNormal;
			float minPenSpeed = 0;

			for (int i = 0; i < na; i++)
			{
				const Vec3& a = axes[i];
				const float r = half.x * std::fabs(a.x) + half.y * std::fabs(a.y) + half.z * std::fabs(a.z);
				float p0 = Dot(v[0], a), p1 = Dot(v[1], a), p2 = Dot(v[2], a);
				const float lo = (std::min)(p0, (std::min)(p1, p2)) - r;
				const float hi = (std::max)(p0, (std::max)(p1, p2)) + r;
				const float c = Dot(c0, a);
				const float dd = Dot(d, a);
				const float width = hi - lo;
				// side of the box relative to the interval; s = separation (>0 apart)
				float s, vel;
				Vec3 n;
				if (c < (lo + hi) * 0.5f) { s = lo - c; vel = dd; n = -a; }
				else { s = c - hi; vel = -dd; n = a; }

				if (s < 0 && -s < minPen) { minPen = -s; minPenNormal = n; minPenSpeed = vel; }

				if (vel <= minApproach)
				{
					if (s >= 0) return false;   // separated and not approaching
					// overlapping on this axis, moving away (or parallel)
					const float tx = vel < -1e-7f ? (-s) / (-vel) : FLT_MAX;
					if (tx < exit) exit = tx;
					continue;
				}
				const float te = (s - kDistEpsilon) / vel;
				const float tx = (s + width) / vel;
				if (te > enter) { enter = te; enterNormal = n; approaching = true; }
				if (tx < exit) exit = tx;
				if (enter > exit || enter > 1.0f) return false;
			}
			if (exit <= 0.0f) return false;
			if (!approaching || enter <= -FLT_MAX)
			{
				// overlapping on every axis at the start: let the box move out
				// along its shallowest axis, block anything else
				if (minPenSpeed < 0) return false;
				outSolid = true;
				outEnter = 0.0f;
				outNormal = minPenNormal;
				return true;
			}
			if (enter > 1.0f) return false;
			outEnter = enter;
			// Internal edges of a triangle soup give edge-axis normals that point
			// sideways ("ghost" collisions on smooth terrain): when the face itself
			// opposes the motion, slide on the face normal instead.
			outNormal = (cullBackfaces && Dot(face, d) < 0) ? face : enterNormal;
			return true;
		}
	};
}
