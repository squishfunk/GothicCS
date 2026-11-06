// GothicCS - CS viewmodel data (.csvm written by tools/convert_cs.py) and
// GoldSrc-style playback: per-frame bone poses, slerp between frames,
// rigid one-bone-per-vertex skinning. Engine-independent.
// Model space = CS view space: X forward, Y left, Z up, inches.
#pragma once
#include "Vec.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cs
{
	struct Quat { float x = 0, y = 0, z = 0, w = 1; };

	// 3x4 affine matrix: rows, last column = translation
	struct Mat34
	{
		float m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
		Vec3 Apply(const Vec3& v) const
		{
			return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] };
		}
		Vec3 Rotate(const Vec3& v) const
		{
			return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
		}
		static Mat34 Mul(const Mat34& a, const Mat34& b)
		{
			Mat34 r;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 4; j++)
					r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + (j == 3 ? a.m[i][3] : 0.0f);
			return r;
		}
		static Mat34 FromQuatPos(const Quat& q, const Vec3& p)
		{
			Mat34 r;
			const float x = q.x, y = q.y, z = q.z, w = q.w;
			r.m[0][0] = 1 - 2 * (y * y + z * z); r.m[0][1] = 2 * (x * y - z * w); r.m[0][2] = 2 * (x * z + y * w); r.m[0][3] = p.x;
			r.m[1][0] = 2 * (x * y + z * w); r.m[1][1] = 1 - 2 * (x * x + z * z); r.m[1][2] = 2 * (y * z - x * w); r.m[1][3] = p.y;
			r.m[2][0] = 2 * (x * z - y * w); r.m[2][1] = 2 * (y * z + x * w); r.m[2][2] = 1 - 2 * (x * x + y * y); r.m[2][3] = p.z;
			return r;
		}
	};

	inline Quat Slerp(Quat a, const Quat& b, float t)
	{
		float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		if (d < 0) { a = { -a.x, -a.y, -a.z, -a.w }; d = -d; }
		float s0, s1;
		if (d > 0.9995f) { s0 = 1 - t; s1 = t; }
		else
		{
			const float om = std::acos(d), so = std::sin(om);
			s0 = std::sin((1 - t) * om) / so;
			s1 = std::sin(t * om) / so;
		}
		Quat r{ s0 * a.x + s1 * b.x, s0 * a.y + s1 * b.y, s0 * a.z + s1 * b.z, s0 * a.w + s1 * b.w };
		const float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
		return { r.x / l, r.y / l, r.z / l, r.w / l };
	}

	struct VmTexture { std::string name; int w = 0, h = 0; uint32_t flags = 0; };
	struct VmTri { uint16_t v[3], n[3]; float uv[6]; uint16_t tex, bodypart; };
	struct VmEvent { int frame, event; std::string options; };
	struct VmSequence
	{
		std::string name;
		float fps = 30;
		int flags = 0;   // 1 = looping
		int frames = 0;
		std::vector<VmEvent> events;
		std::vector<float> pose;   // frames x bones x 7 (pos xyz, quat xyzw)
	};

	struct ViewModelData
	{
		std::vector<VmTexture> textures;
		std::vector<int> parent;
		std::vector<std::string> boneNames;
		std::vector<Vec3> verts;
		std::vector<int> vertBone;
		std::vector<Vec3> norms;
		std::vector<int> normBone;
		std::vector<VmTri> tris;
		std::vector<std::string> bodyparts;
		struct Attachment { int bone; Vec3 org; };
		std::vector<Attachment> attachments;
		std::vector<VmSequence> sequences;

		int FindSequence(const char* name) const
		{
			for (size_t i = 0; i < sequences.size(); i++)
				if (_stricmp(sequences[i].name.c_str(), name) == 0) return (int)i;
			return -1;
		}

		bool Load(const char* path, std::string* error = nullptr)
		{
			FILE* f = nullptr;
			if (fopen_s(&f, path, "rb") != 0 || !f) { if (error) *error = std::string("cannot open ") + path; return false; }
			std::vector<uint8_t> d;
			fseek(f, 0, SEEK_END);
			d.resize((size_t)ftell(f));
			fseek(f, 0, SEEK_SET);
			const size_t got = fread(d.data(), 1, d.size(), f);
			fclose(f);
			if (got != d.size() || d.size() < 12 || memcmp(d.data(), "CSVM", 4) != 0) { if (error) *error = "not a .csvm file"; return false; }
			size_t p = 4;
			bool ok = true;
			auto u32 = [&]() -> uint32_t { if (p + 4 > d.size()) { ok = false; return 0; } uint32_t v; memcpy(&v, &d[p], 4); p += 4; return v; };
			auto i32 = [&]() -> int32_t { return (int32_t)u32(); };
			auto f32 = [&]() -> float { uint32_t v = u32(); float r; memcpy(&r, &v, 4); return r; };
			auto u16 = [&]() -> uint16_t { if (p + 2 > d.size()) { ok = false; return 0; } uint16_t v; memcpy(&v, &d[p], 2); p += 2; return v; };
			auto str = [&](size_t n) -> std::string { if (p + n > d.size()) { ok = false; return {}; } std::string s((const char*)&d[p], strnlen((const char*)&d[p], n)); p += n; return s; };

			if (u32() != 1) { if (error) *error = "unsupported .csvm version"; return false; }
			u32();   // flags
			const uint32_t nt = u32();
			for (uint32_t i = 0; i < nt && ok; i++) { VmTexture t; t.name = str(64); t.w = u16(); t.h = u16(); t.flags = u32(); textures.push_back(t); }
			const uint32_t nb = u32();
			for (uint32_t i = 0; i < nb && ok; i++) { parent.push_back(i32()); boneNames.push_back(str(32)); }
			const uint32_t nv = u32();
			for (uint32_t i = 0; i < nv && ok; i++) { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); verts.push_back(v); vertBone.push_back(i32()); }
			const uint32_t nn = u32();
			for (uint32_t i = 0; i < nn && ok; i++) { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); norms.push_back(v); normBone.push_back(i32()); }
			const uint32_t ntr = u32();
			for (uint32_t i = 0; i < ntr && ok; i++)
			{
				VmTri t;
				for (auto& v : t.v) v = u16();
				for (auto& v : t.n) v = u16();
				for (auto& v : t.uv) v = f32();
				t.tex = u16(); t.bodypart = u16();
				tris.push_back(t);
			}
			const uint32_t nbp = u32();
			for (uint32_t i = 0; i < nbp && ok; i++) bodyparts.push_back(str(32));
			const uint32_t na = u32();
			for (uint32_t i = 0; i < na && ok; i++) { Attachment a; a.bone = i32(); a.org.x = f32(); a.org.y = f32(); a.org.z = f32(); attachments.push_back(a); }
			const uint32_t ns = u32();
			for (uint32_t i = 0; i < ns && ok; i++)
			{
				VmSequence s;
				s.name = str(32); s.fps = f32(); s.flags = i32(); s.frames = (int)u32();
				const uint32_t ne = u32();
				for (uint32_t e = 0; e < ne && ok; e++) { VmEvent ev; ev.frame = i32(); ev.event = i32(); ev.options = str(64); s.events.push_back(ev); }
				const size_t count = (size_t)s.frames * nb * 7;
				if (p + count * 4 > d.size()) { ok = false; break; }
				s.pose.resize(count);
				memcpy(s.pose.data(), &d[p], count * 4);
				p += count * 4;
				sequences.push_back(std::move(s));
			}
			// sanity: indices in range
			for (const VmTri& t : tris)
				for (int k = 0; k < 3 && ok; k++)
					if (t.v[k] >= verts.size() || t.n[k] >= norms.size() || t.tex >= textures.size()) ok = false;
			for (int b : vertBone) if (b < 0 || b >= (int)nb) ok = false;
			for (int b : normBone) if (b < 0 || b >= (int)nb) ok = false;
			if (!ok && error) *error = "truncated or corrupt .csvm";
			return ok;
		}

		// Bone matrices (model space) for `frame` (fractional) of a sequence.
		void Pose(int seq, float frame, std::vector<Mat34>& out) const
		{
			const size_t nb = parent.size();
			out.resize(nb);
			if (seq < 0 || seq >= (int)sequences.size()) return;
			const VmSequence& s = sequences[seq];
			if (s.frames <= 0) return;
			const bool loop = (s.flags & 1) != 0;
			if (frame < 0) frame = 0;
			int f0;
			float t;
			if (loop)
			{
				frame = std::fmod(frame, (float)s.frames);
				f0 = (int)frame;
				t = frame - f0;
			}
			else if (frame >= s.frames - 1) { f0 = s.frames - 1; t = 0; }
			else { f0 = (int)frame; t = frame - f0; }
			const int f1 = loop ? (f0 + 1) % s.frames : (f0 + 1 < s.frames ? f0 + 1 : f0);
			for (size_t b = 0; b < nb; b++)
			{
				const float* a = &s.pose[((size_t)f0 * nb + b) * 7];
				const float* c = &s.pose[((size_t)f1 * nb + b) * 7];
				const Vec3 pos(a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t);
				const Quat q = Slerp({ a[3], a[4], a[5], a[6] }, { c[3], c[4], c[5], c[6] }, t);
				const Mat34 local = Mat34::FromQuatPos(q, pos);
				out[b] = parent[b] >= 0 ? Mat34::Mul(out[parent[b]], local) : local;
			}
		}

		void Skin(const std::vector<Mat34>& bones, std::vector<Vec3>& outVerts, std::vector<Vec3>& outNorms) const
		{
			outVerts.resize(verts.size());
			outNorms.resize(norms.size());
			for (size_t i = 0; i < verts.size(); i++) outVerts[i] = bones[vertBone[i]].Apply(verts[i]);
			for (size_t i = 0; i < norms.size(); i++) outNorms[i] = bones[normBone[i]].Rotate(norms[i]);
		}
	};

	// One playing sequence, advanced like the GoldSrc client (frame = time * fps).
	struct ViewModelAnim
	{
		int seq = -1;
		float time = 0;       // seconds since the sequence started
		int lastFrame = -1;   // for firing events once

		void Start(int s) { seq = s; time = 0; lastFrame = -1; }

		float Frame(const ViewModelData& d) const
		{
			if (seq < 0 || seq >= (int)d.sequences.size()) return 0;
			return time * d.sequences[seq].fps;
		}

		// Advance and collect the events whose frame was reached.
		void Advance(const ViewModelData& d, float dt, std::vector<const VmEvent*>& fired)
		{
			if (seq < 0 || seq >= (int)d.sequences.size()) return;
			time += dt;
			const VmSequence& s = d.sequences[seq];
			int frame = (int)Frame(d);
			if (!(s.flags & 1) && frame > s.frames - 1) frame = s.frames - 1;
			for (const VmEvent& e : s.events)
				if (e.frame > lastFrame && e.frame <= frame) fired.push_back(&e);
			lastFrame = frame;
		}
	};
}
