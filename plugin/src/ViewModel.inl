// GothicCS - first-person CS viewmodel drawn by the plugin.
// Included from Plugin.ipp inside GOTHIC_NAMESPACE (after GothicCS.inl's
// helpers). The model is posed and skinned on the CPU every frame (exactly
// like GoldSrc: one bone per vertex) and drawn as screen-space polygons with
// zCRenderer::DrawPolySimple after the game world, on a cleared depth buffer,
// so it never clips into walls.

namespace GothicCS
{
	struct ViewModel
	{
		cs::ViewModelData data;
		std::vector<zCTexture*> textures;
		cs::ViewModelAnim anim;
		std::vector<cs::Mat34> bones;
		std::vector<cs::Vec3> skinned, skinnedNorm;
		bool loaded = false;
		std::string name, error;
		int drawnTris = 0;
		float drawMs = 0;
		// Debug draw modes (PAGE UP cycles, shown in the HOME overlay):
		//   bit 0: depth test off (painter's order only) instead of a cleared depth buffer
		//   bit 1: no texture (vertex colour only)
		//   bit 2: constant depth = camera near clip + 1 (like Gothic's 2D views)
		int drawMode = 1;

		bool Load(const char* weaponName)
		{
			if (loaded && _stricmp(name.c_str(), weaponName) == 0) return true;
			Unload();
			char exe[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, exe, MAX_PATH);   // ...\System\GOTHIC.EXE
			std::string path = exe;
			path = path.substr(0, path.find_last_of("\\/")) + "\\..\\_work\\Data\\GothicCS\\viewmodels\\" + weaponName + ".csvm";
			data = cs::ViewModelData{};
			if (!data.Load(path.c_str(), &error)) { loaded = false; return false; }
			for (const cs::VmTexture& t : data.textures)
			{
				zSTRING texName(t.name.c_str());
				zCTexture* tex = zCTexture::Load(texName, 0);
				textures.push_back(tex);
			}
			name = weaponName;
			loaded = true;
			error.clear();
			return true;
		}

		void Unload()
		{
			for (zCTexture* t : textures) if (t) t->Release();
			textures.clear();
			loaded = false;
		}

		void Play(const char* seqName)
		{
			if (!loaded) return;
			const int s = data.FindSequence(seqName);
			if (s >= 0) anim.Start(s);
		}

		void PlayIndex(int seq)
		{
			if (loaded && seq >= 0 && seq < (int)data.sequences.size()) anim.Start(seq);
		}

		const char* SequenceName() const
		{
			return loaded && anim.seq >= 0 && anim.seq < (int)data.sequences.size() ? data.sequences[anim.seq].name.c_str() : "-";
		}
	};

	struct DrawTri { float depth; int index; };

	// Light at the hero (Gothic's own static + dynamic vob lighting), used as
	// the viewmodel's ambient colour, with a simple directional term.
	inline void HeroLight(float rgb[3])
	{
		zCOLOR st = player->lightColorStat, dy = player->lightColorDyn;
		rgb[0] = (std::min)(255.0f, float(st.r) + float(dy.r));
		rgb[1] = (std::min)(255.0f, float(st.g) + float(dy.g));
		rgb[2] = (std::min)(255.0f, float(st.b) + float(dy.b));
		// unlit / not yet computed: fall back to neutral
		if (rgb[0] + rgb[1] + rgb[2] < 30.0f) rgb[0] = rgb[1] = rgb[2] = 160.0f;
	}

	// Draws the posed model. Model space = CS view space (X forward, Y left,
	// Z up, inches); the camera sits at the origin looking down +X.
	inline void DrawViewModel(ViewModel& vm, float vFovDeg)
	{
		vm.drawnTris = 0;
		if (!vm.loaded || !zrenderer || !ogame || !ogame->GetCamera()) return;
		LARGE_INTEGER t0, t1, fq;
		QueryPerformanceCounter(&t0);

		cs::ViewModelData& d = vm.data;
		d.Pose(vm.anim.seq, vm.anim.Frame(d), vm.bones);
		d.Skin(vm.bones, vm.skinned, vm.skinnedNorm);

		const zTViewportData& vp = ogame->GetCamera()->vpData;
		const float w = vp.xdim > 0 ? vp.xdimFloat : float(zrenderer->vid_xdim);
		const float h = vp.ydim > 0 ? vp.ydimFloat : float(zrenderer->vid_ydim);
		const float cx = vp.xdim > 0 ? vp.xcenter : w * 0.5f, cy = vp.ydim > 0 ? vp.ycenter : h * 0.5f;
		const float focal = (h * 0.5f) / std::tan(vFovDeg * 0.5f * cs::kDegToRad);
		const float kNear = 0.5f;   // inches

		float light[3];
		HeroLight(light);
		const cs::Vec3 lightDir = cs::Normalized(cs::Vec3(0.3f, 0.4f, 1.0f));

		// visible triangles, far to near (the depth buffer is used too)
		static std::vector<DrawTri> order;
		order.clear();
		for (int i = 0; i < (int)d.tris.size(); i++)
		{
			const cs::VmTri& t = d.tris[i];
			const cs::Vec3& a = vm.skinned[t.v[0]], & b = vm.skinned[t.v[1]], & c = vm.skinned[t.v[2]];
			if (a.x < kNear && b.x < kNear && c.x < kNear) continue;
			// back faces: outward normal pointing away from the eye at the origin
			if (cs::Dot(cs::Cross(b - a, c - a), a) >= 0) continue;
			order.push_back({ (std::max)(a.x, (std::max)(b.x, c.x)), i });
		}
		std::sort(order.begin(), order.end(), [](const DrawTri& x, const DrawTri& y) { return x.depth > y.depth; });

		const bool noDepth = (vm.drawMode & 1) != 0, noTex = (vm.drawMode & 2) != 0, flatZ = (vm.drawMode & 4) != 0;
		const float nearZ = ogame->GetCamera()->nearClipZ;
		zrenderer->FlushPolys();
		const zTRnd_PolySortMode oldSort = zrenderer->GetPolySortMode();
		zrenderer->SetPolySortMode(zRND_SORT_NONE);   // draw now, do not queue
		const zTRnd_ZBufferCmp oldCmp = zrenderer->GetZBufferCompare();
		const int oldWrite = zrenderer->GetZBufferWriteEnabled();
		if (noDepth)
		{
			zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);
			zrenderer->SetZBufferWriteEnabled(FALSE);
		}
		else
		{
			zCOLOR clear;
			clear.dword = 0;
			zrenderer->Vid_Clear(clear, zRND_CLEAR_ZBUFFER);
			zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_LESS_EQUAL);
			zrenderer->SetZBufferWriteEnabled(TRUE);
		}
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_NONE);

		for (const DrawTri& dt : order)
		{
			const cs::VmTri& t = d.tris[dt.index];
			// clip against the near plane (Sutherland-Hodgman on x >= kNear)
			struct V { cs::Vec3 p; float u, v; float shade; };
			V in[3], out[4];
			for (int k = 0; k < 3; k++)
			{
				in[k].p = vm.skinned[t.v[k]];
				in[k].u = t.uv[k * 2];
				in[k].v = t.uv[k * 2 + 1];
				const float l = cs::Dot(vm.skinnedNorm[t.n[k]], lightDir);
				in[k].shade = 0.65f + 0.35f * (l > 0 ? l : 0);
			}
			int n = 0;
			for (int k = 0; k < 3; k++)
			{
				const V& A = in[k], & B = in[(k + 1) % 3];
				const bool ain = A.p.x >= kNear, bin = B.p.x >= kNear;
				if (ain) out[n++] = A;
				if (ain != bin)
				{
					const float s = (kNear - A.p.x) / (B.p.x - A.p.x);
					V m;
					m.p = A.p + (B.p - A.p) * s;
					m.u = A.u + (B.u - A.u) * s;
					m.v = A.v + (B.v - A.v) * s;
					m.shade = A.shade + (B.shade - A.shade) * s;
					out[n++] = m;
				}
			}
			if (n < 3) continue;
			zTRndSimpleVertex sv[4];
			for (int k = 0; k < n; k++)
			{
				const V& v = out[k];
				sv[k].pos = zVEC2(cx - focal * v.p.y / v.p.x, cy - focal * v.p.z / v.p.x);
				sv[k].z = flatZ ? nearZ + 1.0f : (std::max)(v.p.x * 2.54f, nearZ + 1.0f);   // camera depth, cm
				sv[k].uv = zVEC2(v.u, v.v);
				sv[k].color.r = (unsigned char)(light[0] * v.shade);
				sv[k].color.g = (unsigned char)(light[1] * v.shade);
				sv[k].color.b = (unsigned char)(light[2] * v.shade);
				sv[k].color.alpha = 255;
			}
			zCTexture* tex = noTex ? nullptr : (t.tex < vm.textures.size() ? vm.textures[t.tex] : nullptr);
			// submit both windings: the device's cull mode is unknown here
			zrenderer->DrawPolySimple(tex, sv, n);
			zTRndSimpleVertex rv[4];
			for (int k = 0; k < n; k++) rv[k] = sv[n - 1 - k];
			zrenderer->DrawPolySimple(tex, rv, n);
			vm.drawnTris++;
		}
		zrenderer->FlushPolys();
		zrenderer->SetZBufferCompare(oldCmp);
		zrenderer->SetZBufferWriteEnabled(oldWrite);
		zrenderer->SetPolySortMode(oldSort);

		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&fq);
		vm.drawMs = float(double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(fq.QuadPart));
	}

	// Debug: two test squares in the top-right corner (red = vertex colour
	// only, right = the first viewmodel texture) to see whether
	// DrawPolySimple shows anything at all in this render state.
	inline void DrawTestQuads(ViewModel& vm)
	{
		if (!zrenderer || !ogame || !ogame->GetCamera()) return;
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		const float z = ogame->GetCamera()->nearClipZ + 1.0f;
		const zTRnd_PolySortMode oldSort = zrenderer->GetPolySortMode();
		zrenderer->SetPolySortMode(zRND_SORT_NONE);
		for (int q = 0; q < 2; q++)
		{
			const float x0 = vp.xdimFloat - 260.0f + q * 130.0f, y0 = 20.0f, sz = 110.0f;
			zTRndSimpleVertex v[4];
			const float px[4] = { x0, x0 + sz, x0 + sz, x0 }, py[4] = { y0, y0, y0 + sz, y0 + sz };
			const float pu[4] = { 0, 1, 1, 0 }, pv[4] = { 0, 0, 1, 1 };
			for (int k = 0; k < 4; k++)
			{
				v[k].pos = zVEC2(px[k], py[k]);
				v[k].z = z;
				v[k].uv = zVEC2(pu[k], pv[k]);
				v[k].color.r = 255; v[k].color.g = q ? 255 : 40; v[k].color.b = q ? 255 : 40; v[k].color.alpha = 255;
			}
			zCTexture* tex = (q == 1 && !vm.textures.empty()) ? vm.textures[0] : nullptr;
			zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);
			zrenderer->DrawPolySimple(tex, v, 4);
			zTRndSimpleVertex r[4] = { v[3], v[2], v[1], v[0] };
			zrenderer->DrawPolySimple(tex, r, 4);
		}
		zrenderer->FlushPolys();
		zrenderer->SetPolySortMode(oldSort);
	}

	// CS muzzle flash (studio event 5001 "XY": sprite muzzleflash<Y+1>, scale X/10)
	// as an additive camera-facing sprite at the viewmodel's attachment 0.
	inline void DrawMuzzleFlash(ViewModel& vm, const char* spriteName, float scale, int frame, float roll, float vFovDeg)
	{
		if (!vm.loaded || vm.data.attachments.empty() || vm.bones.empty()) return;
		const cs::ViewModelData::Attachment& a = vm.data.attachments[0];
		if (a.bone < 0 || a.bone >= (int)vm.bones.size()) return;
		const cs::Vec3 p = vm.bones[a.bone].Apply(a.org);
		ScreenProjector proj;
		if (!proj.Init(vFovDeg)) return;
		float sx, sy;
		if (!proj.Project(p, sx, sy)) return;
		Sprite* spr = GetSprite(spriteName);
		if (!spr->ok) return;
		const SpriteFrame& fr = spr->frames[frame % spr->frames.size()];
		const float w = proj.focal * fr.w * scale / p.x, h = proj.focal * fr.h * scale / p.x;
		const zTRnd_PolySortMode oldSort = zrenderer->GetPolySortMode();
		zrenderer->SetPolySortMode(zRND_SORT_NONE);
		zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);
		DrawSpriteQuad(*spr, frame, sx, sy, w, h, roll, Rgba(255, 255, 255, 255), zRND_ALPHA_FUNC_ADD);
		zrenderer->FlushPolys();
		zrenderer->SetPolySortMode(oldSort);
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_NONE);
	}
}
