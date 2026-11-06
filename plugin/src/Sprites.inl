// GothicCS - CS sprites (.spr converted by tools/convert_cs.py into one
// texture per frame + an index file), drawn as screen-space polygons with
// zCRenderer::DrawPolySimple. Included inside GOTHIC_NAMESPACE.

namespace GothicCS
{
	enum SpriteFormat { SprNormal = 0, SprAdditive = 1, SprIndexAlpha = 2, SprAlphaTest = 3 };

	struct SpriteFrame { int w = 0, h = 0, ox = 0, oy = 0; float u = 1, v = 1; zCTexture* tex = nullptr; };

	struct Sprite
	{
		std::string name;
		int type = 0, format = 0, width = 0, height = 0;
		std::vector<SpriteFrame> frames;
		bool ok = false;
	};

	inline std::string GothicDataDir()
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);   // ...\System\GOTHIC.EXE
		std::string p = exe;
		return p.substr(0, p.find_last_of("\\/")) + "\\..\\_work\\Data\\GothicCS\\";
	}

	// Cached by name ("muzzleflash3", "fexplo", ...). Never returns null.
	inline Sprite* GetSprite(const char* name)
	{
		static std::map<std::string, Sprite*> cache;
		std::string key = name;
		for (char& c : key) c = (char)toupper((unsigned char)c);
		auto it = cache.find(key);
		if (it != cache.end()) return it->second;
		Sprite* s = new Sprite();
		s->name = key;
		cache[key] = s;
		FILE* f = nullptr;
		const std::string path = GothicDataDir() + "sprites\\" + key + ".txt";
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return s;
		int n = 0;
		if (fscanf_s(f, "type %d format %d width %d height %d frames %d", &s->type, &s->format, &s->width, &s->height, &n) == 5)
		{
			for (int i = 0; i < n; i++)
			{
				SpriteFrame fr;
				if (fscanf_s(f, "%d %d %d %d %f %f", &fr.w, &fr.h, &fr.ox, &fr.oy, &fr.u, &fr.v) != 6) break;
				char tex[64];
				sprintf_s(tex, "CSS_%s_F%02d.TGA", key.c_str(), i);
				zSTRING texName(tex);
				fr.tex = zCTexture::Load(texName, 0);
				s->frames.push_back(fr);
			}
			s->ok = !s->frames.empty();
		}
		fclose(f);
		return s;
	}

	struct ScreenProjector
	{
		float cx = 0, cy = 0, focal = 1, w = 0, h = 0;
		bool Init(float vFovDeg)
		{
			if (!ogame || !ogame->GetCamera()) return false;
			const zTViewportData& vp = ogame->GetCamera()->vpData;
			w = vp.xdimFloat; h = vp.ydimFloat; cx = vp.xcenter; cy = vp.ycenter;
			focal = (h * 0.5f) / std::tan(vFovDeg * 0.5f * cs::kDegToRad);
			return w > 0 && h > 0;
		}
		// view space (X forward, Y left, Z up) -> pixels
		bool Project(const cs::Vec3& v, float& sx, float& sy) const
		{
			if (v.x < 0.5f) return false;
			sx = cx - focal * v.y / v.x;
			sy = cy - focal * v.z / v.x;
			return true;
		}
	};

	inline zTRnd_AlphaBlendFunc BlendFor(const Sprite& s)
	{
		return s.format == SprAdditive ? zRND_ALPHA_FUNC_ADD : (s.format == SprNormal ? zRND_ALPHA_FUNC_NONE : zRND_ALPHA_FUNC_BLEND);
	}

	// Screen-aligned quad of one sprite frame centred at (sx, sy); size in
	// pixels; roll in degrees; colour multiplies the texture (alpha = opacity).
	inline void DrawSpriteQuad(const Sprite& s, int frame, float sx, float sy, float sizeW, float sizeH, float rollDeg,
		zCOLOR color, zTRnd_AlphaBlendFunc blend)
	{
		if (!s.ok || !zrenderer) return;
		const SpriteFrame& fr = s.frames[(frame % (int)s.frames.size() + (int)s.frames.size()) % (int)s.frames.size()];
		const float c = std::cos(rollDeg * cs::kDegToRad), sn = std::sin(rollDeg * cs::kDegToRad);
		const float hx = sizeW * 0.5f, hy = sizeH * 0.5f;
		const float px[4] = { -hx, hx, hx, -hx }, py[4] = { -hy, -hy, hy, hy };
		const float pu[4] = { 0, fr.u, fr.u, 0 }, pv[4] = { 0, 0, fr.v, fr.v };
		zTRndSimpleVertex v[4];
		const float z = ogame && ogame->GetCamera() ? ogame->GetCamera()->nearClipZ + 1.0f : 1.0f;
		for (int k = 0; k < 4; k++)
		{
			v[k].pos = zVEC2(sx + px[k] * c - py[k] * sn, sy + px[k] * sn + py[k] * c);
			v[k].z = z;
			v[k].uv = zVEC2(pu[k], pv[k]);
			v[k].color = color;
		}
		zrenderer->SetAlphaBlendFunc(blend);
		zrenderer->DrawPolySimple(fr.tex, v, 4);
		zTRndSimpleVertex r[4] = { v[3], v[2], v[1], v[0] };
		zrenderer->DrawPolySimple(fr.tex, r, 4);
	}

	// Full-screen coloured quad (flashbang white-out, damage tint...).
	inline void DrawScreenFill(zCOLOR color)
	{
		if (!zrenderer || !ogame || !ogame->GetCamera() || color.alpha == 0) return;
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		zTRndSimpleVertex v[4];
		const float px[4] = { 0, vp.xdimFloat, vp.xdimFloat, 0 }, py[4] = { 0, 0, vp.ydimFloat, vp.ydimFloat };
		for (int k = 0; k < 4; k++)
		{
			v[k].pos = zVEC2(px[k], py[k]);
			v[k].z = ogame->GetCamera()->nearClipZ + 1.0f;
			v[k].uv = zVEC2(0.5f, 0.5f);
			v[k].color = color;
		}
		zSTRING white("CSS_WHITE.TGA");
		static zCTexture* tex = zCTexture::Load(white, 0);
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_BLEND);
		zrenderer->DrawPolySimple(tex, v, 4);
		zTRndSimpleVertex r[4] = { v[3], v[2], v[1], v[0] };
		zrenderer->DrawPolySimple(tex, r, 4);
	}

	inline zCOLOR Rgba(int r, int g, int b, int a)
	{
		zCOLOR c;
		c.r = (unsigned char)r; c.g = (unsigned char)g; c.b = (unsigned char)b; c.alpha = (unsigned char)a;
		return c;
	}
}
