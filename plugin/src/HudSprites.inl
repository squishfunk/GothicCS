// GothicCS - CS HUD drawn from the CS sprites (sprites\hud.txt, 640 set,
// 640hud7.spr): health cross + digits, armour suit icon, money with the
// dollar sign, round timer with the stopwatch, ammo with the divider, C4 /
// defuser / buy zone icons. Drawn additively in the CS HUD colour
// (255,160,0); a value that changes flashes to full brightness and fades back
// to the HUD's minimum alpha (100), like the HL HUD.
// Included inside GOTHIC_NAMESPACE after Hud.inl.

namespace GothicCS
{
	struct HudRect { std::string sprite; int x = 0, y = 0, w = 0, h = 0; };

	inline std::map<std::string, HudRect>& HudRects()
	{
		static std::map<std::string, HudRect> m;
		static bool loaded = false;
		if (loaded) return m;
		loaded = true;
		FILE* f = nullptr;
		const std::string path = GothicDataDir() + "hud.txt";
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return m;
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			char name[64], spr[64];
			int res, x, y, w, h;
			if (sscanf_s(line, "%63s %d %63s %d %d %d %d", name, (unsigned)sizeof(name), &res, spr, (unsigned)sizeof(spr), &x, &y, &w, &h) == 7 && res == 640)
				m[name] = { spr, x, y, w, h };
		}
		fclose(f);
		return m;
	}

	struct HudFade { int value = -999999; double changedAt = -100; };

	// brightness of a HUD element: 255 right after a change, fading to 100 (MIN_ALPHA)
	inline int HudAlpha(HudFade& f, int value, double now)
	{
		if (value != f.value) { f.value = value; f.changedAt = now; }
		const double t = now - f.changedAt;
		const int a = int(255 - t * 155.0 / 1.0);   // ~1 s fade
		return a < 100 ? 100 : (a > 255 ? 255 : a);
	}

	// One hud.txt entry at pixel (x, y), additive, colour scaled by alpha.
	inline void DrawHudIcon(const char* name, float x, float y, int r, int g, int b, int alpha, float scale)
	{
		auto it = HudRects().find(name);
		if (it == HudRects().end()) return;
		const HudRect& hr = it->second;
		Sprite* spr = GetSprite(hr.sprite.c_str());
		if (!spr->ok) return;
		const SpriteFrame& fr = spr->frames[0];
		const float pw = fr.w / fr.u, ph = fr.h / fr.v;   // padded texture size in pixels
		const float u0 = hr.x / pw, v0 = hr.y / ph, u1 = (hr.x + hr.w) / pw, v1 = (hr.y + hr.h) / ph;
		zTRndSimpleVertex v[4];
		const float w = hr.w * scale, h = hr.h * scale;
		const float px[4] = { x, x + w, x + w, x }, py[4] = { y, y, y + h, y + h };
		const float pu[4] = { u0, u1, u1, u0 }, pv[4] = { v0, v0, v1, v1 };
		const float z = ogame->GetCamera()->nearClipZ + 1.0f;
		const zCOLOR c = Rgba(r * alpha / 255, g * alpha / 255, b * alpha / 255, 255);
		for (int k = 0; k < 4; k++) { v[k].pos = zVEC2(px[k], py[k]); v[k].z = z; v[k].uv = zVEC2(pu[k], pv[k]); v[k].color = c; }
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_ADD);
		zrenderer->DrawPolySimple(fr.tex, v, 4);
	}

	inline float HudIconW(const char* name)
	{
		auto it = HudRects().find(name);
		return it == HudRects().end() ? 0.0f : (float)it->second.w;
	}

	// Digits (number_0..9), `minDigits` pads with zeros; returns the width drawn.
	inline float DrawHudNumber(int value, float x, float y, int r, int g, int b, int alpha, float scale, int minDigits = 1)
	{
		char buf[16];
		sprintf_s(buf, "%0*d", minDigits, value < 0 ? 0 : value);
		const float dw = HudIconW("number_0") * scale;
		float px = x;
		for (const char* c = buf; *c; c++)
		{
			char name[16];
			sprintf_s(name, "number_%c", *c);
			DrawHudIcon(name, px, y, r, g, b, alpha, scale);
			px += dw;
		}
		return px - x;
	}

	inline float HudNumberWidth(int value, float scale, int minDigits = 1)
	{
		char buf[16];
		sprintf_s(buf, "%0*d", minDigits, value < 0 ? 0 : value);
		return strlen(buf) * HudIconW("number_0") * scale;
	}

	struct HudFades { HudFade health, armor, money, ammo, clip, timer; };
	inline HudFades& Fades() { static HudFades f; return f; }

	inline int RoundSecondsLeft();
	inline bool InBuyZone(State& s);

	inline void DrawSpriteHud(State& s)
	{
		if (HudRects().empty() || !ogame || !ogame->GetCamera()) return;
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		const float W = vp.xdimFloat, H = vp.ydimFloat;
		const float scale = (std::max)(1.0f, H / 768.0f);   // 640 set at its native size up to 768 lines
		const float fontH = 25.0f * scale;
		const float y = H - fontH - fontH * 0.5f;
		const double now = s.clock;
		const int R = 255, G = 160, B = 0;   // CS HUD colour
		HudFades& f = Fades();
		zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);

		// health
		int a = HudAlpha(f.health, s.inv.health, now);
		const int hr = s.inv.health <= 25 ? 250 : R, hg = s.inv.health <= 25 ? 0 : G;   // low health turns red
		float x = 10.0f * scale;
		DrawHudIcon("cross", x, y, hr, hg, B, a, scale);
		x += HudIconW("cross") * scale + 6 * scale;
		DrawHudNumber(s.inv.health, x, y, hr, hg, B, a, scale, 3);

		// armour
		a = HudAlpha(f.armor, s.inv.armor, now);
		x = W / 5.0f;
		DrawHudIcon(s.inv.helmet ? "suithelmet_empty" : "suit_empty", x, y, R, G, B, a, scale);
		if (s.inv.armor > 0) DrawHudIcon(s.inv.helmet ? "suithelmet_full" : "suit_full", x, y, R, G, B, a, scale);
		x += HudIconW("suit_full") * scale + 6 * scale;
		DrawHudNumber(s.inv.armor, x, y, R, G, B, a, scale, 3);

		// round timer (bottom centre); while the bomb is planted it shows the C4
		// countdown instead, in red (like the CS servers' c4 timer plugins)
		const bool c4 = Bomb().planted;
		const int secs = c4 ? (std::max)(0, (int)std::ceil(Bomb().blowTime - s.clock)) : RoundSecondsLeft();
		if (secs >= 0)
		{
			a = c4 ? 255 : HudAlpha(f.timer, secs < 20 ? secs : -1, now);
			const int tr = (c4 || secs < 20) ? 250 : R, tg = (c4 || secs < 20) ? 0 : G;
			const float tw = HudIconW("stopwatch") * scale + 6 * scale + HudNumberWidth(secs / 60, scale, 1) + 8 * scale + HudNumberWidth(secs % 60, scale, 2);
			x = W * 0.5f - tw * 0.5f;
			DrawHudIcon("stopwatch", x, y, tr, tg, B, a, scale);
			x += HudIconW("stopwatch") * scale + 6 * scale;
			x += DrawHudNumber(secs / 60, x, y, tr, tg, B, a, scale, 1);
			DrawRect(x + 2 * scale, y + 6 * scale, 3 * scale, 3 * scale, Rgba(tr * a / 255, tg * a / 255, 0, 255), zRND_ALPHA_FUNC_ADD);
			DrawRect(x + 2 * scale, y + 16 * scale, 3 * scale, 3 * scale, Rgba(tr * a / 255, tg * a / 255, 0, 255), zRND_ALPHA_FUNC_ADD);
			x += 8 * scale;
			DrawHudNumber(secs % 60, x, y, tr, tg, B, a, scale, 2);
		}

		// ammo (bottom right): clip | reserve, or the grenade / C4 count
		if (cs::Weapon* w = s.inv.active)
		{
			int clip = -1, reserve = -1;
			if (w->I().maxClip > 0) { clip = w->clip; reserve = s.inv.ammo[w->I().ammo]; }
			else if (cs::IsGrenade(w->id) || w->id == cs::W_C4) reserve = w->count;
			a = HudAlpha(f.ammo, clip * 1000 + reserve, now);
			x = W - 10 * scale;
			if (reserve >= 0)
			{
				x -= HudNumberWidth(reserve, scale);
				DrawHudNumber(reserve, x, y, R, G, B, a, scale);
			}
			if (clip >= 0)
			{
				x -= 12 * scale;
				DrawHudIcon("divider", x, y - 8 * scale, R, G, B, a, scale);
				x -= 8 * scale + HudNumberWidth(clip, scale);
				DrawHudNumber(clip, x, y, R, G, B, a, scale);
			}
		}

		// money (right, above the ammo)
		a = HudAlpha(f.money, s.inv.money, now);
		const float my = y - fontH * 1.6f;
		x = W - 10 * scale - HudNumberWidth(s.inv.money, scale);
		DrawHudNumber(s.inv.money, x, my, R, G, B, a, scale);
		DrawHudIcon("dollar", x - HudIconW("dollar") * scale - 4 * scale, my, R, G, B, a, scale);

		// status icons (left, above health): buy zone, C4 (flashing on a bombsite), defuse kit
		float iy = y - fontH * 2.2f, ix = 10 * scale;
		if (InBuyZone(s)) { DrawHudIcon("buyzone", ix, iy, 0, 160, 0, 255, scale); ix += 36 * scale; }
		if (s.inv.c4.count > 0)
		{
			const bool zone = InBombZone(s, s.pm.origin);
			const int ca = zone ? (int(now * 4) % 2 ? 255 : 90) : 255;
			DrawHudIcon("c4", ix, iy, 0, 160, 0, ca, scale);
			ix += 36 * scale;
		}
		if (s.inv.defuser) DrawHudIcon("defuser", ix, iy, 0, 160, 0, 255, scale);
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_NONE);
	}
}
