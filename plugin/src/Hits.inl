// GothicCS - bullet hits on Gothic NPCs and monsters.
// Gothic's own ray test does not report characters reliably, so bullets are
// tested against capsules built from each character's skeleton (humans:
// BIP01 HEAD / SPINE / limbs -> CS hit groups); monsters without a human
// skeleton use their bounding box (generic hit group).
// Included inside GOTHIC_NAMESPACE.

namespace GothicCS
{
	struct NpcHit
	{
		oCNpc* npc = nullptr;
		float dist = 1e30f;       // cm along the ray
		zVEC3 point;
		cs::HitGroup group = cs::HitGroup::Generic;
	};

	struct Capsule { const char* a; const char* b; float radius; cs::HitGroup group; float extend; };

	// Segments between skeleton nodes (radius in cm). `extend` lengthens a
	// single-node capsule (head) upwards.
	static const Capsule kHumanCapsules[] = {
		{ "BIP01 HEAD", nullptr, 13.0f, cs::HitGroup::Head, 22.0f },
		{ "BIP01 SPINE2", "BIP01 NECK", 19.0f, cs::HitGroup::Chest, 0 },
		{ "BIP01 SPINE1", "BIP01 SPINE2", 19.0f, cs::HitGroup::Chest, 0 },
		{ "BIP01 PELVIS", "BIP01 SPINE1", 18.0f, cs::HitGroup::Stomach, 0 },
		{ "BIP01 L UPPERARM", "BIP01 L FOREARM", 7.0f, cs::HitGroup::Arm, 0 },
		{ "BIP01 L FOREARM", "BIP01 L HAND", 6.0f, cs::HitGroup::Arm, 0 },
		{ "BIP01 R UPPERARM", "BIP01 R FOREARM", 7.0f, cs::HitGroup::Arm, 0 },
		{ "BIP01 R FOREARM", "BIP01 R HAND", 6.0f, cs::HitGroup::Arm, 0 },
		{ "BIP01 L THIGH", "BIP01 L CALF", 10.0f, cs::HitGroup::Leg, 0 },
		{ "BIP01 L CALF", "BIP01 L FOOT", 8.0f, cs::HitGroup::Leg, 0 },
		{ "BIP01 R THIGH", "BIP01 R CALF", 10.0f, cs::HitGroup::Leg, 0 },
		{ "BIP01 R CALF", "BIP01 R FOOT", 8.0f, cs::HitGroup::Leg, 0 },
	};

	inline float Dot3(const zVEC3& a, const zVEC3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

	// Ray segment (o + t d, t in [0, len], |d| = 1) vs capsule (segment p-q,
	// radius r): distance along the ray to the capsule surface, or -1.
	// Closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9).
	inline float RayCapsule(const zVEC3& o, const zVEC3& d, float len, const zVEC3& p, const zVEC3& q, float r)
	{
		const zVEC3 d1 = d * len;          // ray segment
		const zVEC3 d2 = q - p;            // capsule axis
		const zVEC3 rr = o - p;
		const float a = Dot3(d1, d1), e = Dot3(d2, d2), f = Dot3(d2, rr);
		float s, t;   // s on the ray (0..1), t on the axis (0..1)
		if (e <= 1e-6f)
		{
			t = 0;
			s = -Dot3(d1, rr) / a;
			s = s < 0 ? 0 : (s > 1 ? 1 : s);
		}
		else
		{
			const float c = Dot3(d1, rr), b = Dot3(d1, d2), den = a * e - b * b;
			s = den > 1e-6f ? (b * f - c * e) / den : 0;
			s = s < 0 ? 0 : (s > 1 ? 1 : s);
			t = (b * s + f) / e;
			if (t < 0) { t = 0; s = -c / a; s = s < 0 ? 0 : (s > 1 ? 1 : s); }
			else if (t > 1) { t = 1; s = (b - c) / a; s = s < 0 ? 0 : (s > 1 ? 1 : s); }
		}
		const zVEC3 c1 = o + d1 * s, c2 = p + d2 * t;
		const zVEC3 diff = c1 - c2;
		const float dist2 = Dot3(diff, diff);
		if (dist2 > r * r) return -1;
		const float entry = s * len - std::sqrt(r * r - dist2);   // back up to the surface
		return entry < 0 ? 0 : entry;
	}

	inline bool RayAabb(const zVEC3& o, const zVEC3& d, float len, const zTBBox3D& box, float& tEnter)
	{
		float t0 = 0, t1 = len;
		for (int i = 0; i < 3; i++)
		{
			if (std::fabs(d[i]) < 1e-8f)
			{
				if (o[i] < box.mins[i] || o[i] > box.maxs[i]) return false;
				continue;
			}
			float a = (box.mins[i] - o[i]) / d[i], b = (box.maxs[i] - o[i]) / d[i];
			if (a > b) std::swap(a, b);
			if (a > t0) t0 = a;
			if (b < t1) t1 = b;
			if (t0 > t1) return false;
		}
		tEnter = t0;
		return true;
	}

	// Nearest living character hit by the ray from `o` along unit `d` within `len` cm.
	inline NpcHit TraceCharacters(const zVEC3& o, const zVEC3& d, float len, oCNpc* ignore, oCNpc* ignore2 = nullptr)
	{
		NpcHit best;
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world || !world->voblist_npcs) return best;
		for (zCListSort<oCNpc>* n = world->voblist_npcs->next; n; n = n->next)
		{
			oCNpc* npc = n->data;
			if (!npc || npc == ignore || npc == ignore2 || npc->IsDead() || !npc->GetHomeWorld()) continue;
			// broad phase: bounding box grown a little (limbs swing outside it)
			zTBBox3D box = npc->bbox3D;
			box.mins -= zVEC3(20, 20, 20);
			box.maxs += zVEC3(20, 20, 20);
			float tBox;
			if (!RayAabb(o, d, len, box, tBox) || tBox > best.dist) continue;

			zCModel* model = npc->GetModel();
			bool human = false;
			if (model)
			{
				// a human skeleton has arms and legs; monsters may share only some BIP01 names
				int found = 0;
				for (const char* n : { "BIP01 HEAD", "BIP01 L UPPERARM", "BIP01 R THIGH", "BIP01 SPINE1" })
					if (model->SearchNode(zSTRING(n))) found++;
				human = found == 4;
			}
			if (human)
			{
				for (const Capsule& c : kHumanCapsules)
				{
					zCModelNodeInst* na = model->SearchNode(zSTRING(c.a));
					if (!na) continue;
					const zVEC3 pa = model->GetNodePositionWorld(na);
					zVEC3 pb = pa + zVEC3(0, c.extend, 0);
					if (c.b)
					{
						zCModelNodeInst* nb = model->SearchNode(zSTRING(c.b));
						if (!nb) continue;
						pb = model->GetNodePositionWorld(nb);
					}
					const float t = RayCapsule(o, d, len, pa, pb, c.radius);
					if (t >= 0 && t < best.dist)
					{
						best.npc = npc; best.dist = t; best.point = o + d * t; best.group = c.group;
					}
				}
			}
			else
			{
				// monster: its bounding box (slightly grown, small animals are thin); head = near BIP01 HEAD
				zTBBox3D mb = npc->bbox3D;
				mb.mins -= zVEC3(5, 5, 5);
				mb.maxs += zVEC3(5, 5, 5);
				float t;
				if (RayAabb(o, d, len, mb, t) && t < best.dist)
				{
					best.npc = npc; best.dist = t; best.point = o + d * t; best.group = cs::HitGroup::Generic;
					if (model)
						if (zCModelNodeInst* head = model->SearchNode(zSTRING("BIP01 HEAD")))
						{
							const float th = RayCapsule(o, d, len, model->GetNodePositionWorld(head), model->GetNodePositionWorld(head), 18.0f);
							if (th >= 0) { best.dist = th; best.point = o + d * th; best.group = cs::HitGroup::Head; }
						}
				}
			}
		}
		return best;
	}

	// Static world (and collidable vobs) along the ray, characters ignored.
	struct WorldHit { bool hit = false; float dist = 0; zVEC3 point, normal; int mat = -1; zCVob* vob = nullptr; };

	inline WorldHit TraceWorld(const zVEC3& o, const zVEC3& d, float len, oCNpc* ignore)
	{
		WorldHit h;
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		const int flags = zTRACERAY_STAT_POLY | zTRACERAY_POLY_NORMAL | zTRACERAY_POLY_IGNORE_TRANSP |
			zTRACERAY_VOB_IGNORE_NO_CD_DYN | zTRACERAY_VOB_IGNORE_CHARACTER;
		if (!world || !world->TraceRayNearestHit(o, d * len, ignore, flags)) return h;
		const zTTraceRayReport& r = world->traceRayReport;
		h.hit = true;
		h.point = r.foundIntersection;
		h.dist = (r.foundIntersection - o).Length();
		h.normal = r.foundPolyNormal;
		h.mat = r.foundPoly && r.foundPoly->material ? (int)r.foundPoly->material->matGroup : -1;
		h.vob = r.foundVob;
		return h;
	}

	// Is the straight line between two points free of world geometry?
	inline bool LineOfSight(const zVEC3& a, const zVEC3& b, oCNpc* ignore)
	{
		zVEC3 d = b - a;
		const float len = d.Length();
		if (len < 1) return true;
		d = d * (1.0f / len);
		const WorldHit h = TraceWorld(a, d, len, ignore);
		return !h.hit || h.dist >= len - 5.0f;
	}
}
