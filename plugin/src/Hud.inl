// GothicCS - HUD: health / armour / money / ammo, kill feed, centre messages,
// progress bar, flashbang white-out, sniper scope, crosshair and the
// old-style CS buy menu (texts read from the user's cstrike\titles.txt,
// copied by tools/convert_cs.py).
// Included inside GOTHIC_NAMESPACE after Bots.inl.

namespace GothicCS
{
	// ---- titles.txt (blocks "Key { ... }") -------------------------------------
	inline std::map<std::string, std::vector<std::string>>& Titles()
	{
		static std::map<std::string, std::vector<std::string>> t;
		static bool loaded = false;
		if (loaded) return t;
		loaded = true;
		FILE* f = nullptr;
		const std::string path = GothicDataDir() + "titles.txt";
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return t;
		char line[512];
		std::string key, prev;
		std::vector<std::string> body;
		bool in = false;
		while (fgets(line, sizeof(line), f))
		{
			std::string l = line;
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
			if (!in)
			{
				if (l == "{") { in = true; key = prev; body.clear(); }
				else if (!l.empty() && l[0] != '/') prev = l;
			}
			else if (l == "}") { in = false; t[key] = body; }
			else body.push_back(l);
		}
		fclose(f);
		return t;
	}

	inline void SetFont(const char* name) { if (screen) screen->SetFont(zSTRING(name)); }
	inline void SetColor(int r, int g, int b, int a = 255) { if (screen) screen->SetFontColor(zCOLOR(r, g, b, a)); }

	inline int TextWidth(const std::string& t)
	{
		zSTRING z(t.c_str());
		return screen->FontSize(z);
	}

	// One titles.txt menu line with \y \w \d \R codes: left part at x, \R part right-aligned at xr.
	inline void PrintMenuLine(const std::string& raw, int x, int xr, int y)
	{
		int r = 255, g = 255, b = 255;
		bool rightPart = false;
		int px = x;
		std::string cur;
		auto flush = [&]() {
			if (cur.empty()) return;
			SetColor(r, g, b);
			const int at = rightPart ? xr - TextWidth(cur) : px;
			screen->Print(at, y, zSTRING(cur.c_str()));
			if (!rightPart) px += TextWidth(cur);
			cur.clear();
		};
		for (size_t i = 0; i < raw.size(); i++)
		{
			const char c = i + 1 < raw.size() ? raw[i + 1] : 0;
			if (raw[i] == '\\' && (c == 'y' || c == 'w' || c == 'd' || c == 'R'))
			{
				flush();
				if (c == 'y') { r = 255; g = 210; b = 0; }
				else if (c == 'w') { r = 255; g = 255; b = 255; }
				else if (c == 'd') { r = 110; g = 110; b = 110; }
				else rightPart = true;
				i++;
				continue;
			}
			cur += raw[i];
		}
		flush();
	}

	inline const char* BuyMenuKey(State& s)
	{
		const bool ct = s.inv.ct, bomb = !s.bombsites.empty();
		switch (s.buyMenu)
		{
		case 9: return "Buy";
		case 1: return ct ? "CT_BuyPistol" : "T_BuyPistol";
		case 2: return "BuyShotgun";
		case 3: return ct ? "CT_BuySubMachineGun" : "T_BuySubMachineGun";
		case 4: return ct ? "CT_BuyRifle" : "T_BuyRifle";
		case 5: return "BuyMachineGun";
		case 8: return bomb ? (ct ? "DCT_BuyItem" : "DT_BuyItem") : (ct ? "CT_BuyItem" : "T_BuyItem");
		}
		return "";
	}

	inline void DrawBuyMenu(State& s)
	{
		if (!s.buyMenu || !screen) return;
		auto it = Titles().find(BuyMenuKey(s));
		SetFont("FONT_OLD_10_WHITE.TGA");
		const int lineH = screen->FontY() + 20;
		int y = 2600;
		if (it == Titles().end())
		{
			SetColor(255, 80, 80);
			screen->Print(300, y, zSTRING("Missing titles.txt - run tools/convert_cs.py"));
			return;
		}
		for (const std::string& l : it->second)
		{
			PrintMenuLine(l, 300, 2900, y);
			y += lineH;
		}
	}

	// Picks a menu entry (digit key). Returns true when the key was used by the menu.
	inline void DropWeapon(State& s, const cs::Weapon& w);

	inline void BuyMenuKeyPress(State& s, int key)
	{
		const char* msg = nullptr;
		if (s.buyMenu == 9)
		{
			if (key == 0) { s.buyMenu = 0; return; }
			if (key >= 1 && key <= 5) { s.buyMenu = key; return; }
			if (key == 6 || key == 7)
			{
				msg = s.inv.BuyFullAmmo(key == 6);
				if (!msg) PlaySfx(SfxName("items/9mmclip1.wav"));
				s.buyMenu = 0;
			}
			if (key == 8) { s.buyMenu = 8; return; }
		}
		else if (s.buyMenu == 8)
		{
			if (key == 0) { s.buyMenu = 0; return; }
			const int max = s.inv.ct ? 7 : 6;
			if (key > max) return;
			msg = s.inv.BuyEquipment(key);
			if (!msg || strstr(msg, "dokupiono"))
				PlaySfx(SfxName(key <= 2 ? "items/ammopickup2.wav" : (key == 6 ? "items/equip_nvg.wav" : (key == 7 ? "items/kevlar.wav" : "items/9mmclip1.wav"))));
			s.buyMenu = 0;
		}
		else
		{
			if (key == 0) { s.buyMenu = 0; return; }
			const cs::WeaponId id = cs::BuyMenuWeapon(s.buyMenu, key, s.inv.ct);
			if (id == cs::W_NONE) return;
			cs::Weapon old = cs::Info(id).slot == cs::SLOT_PRIMARY ? s.inv.primary : s.inv.pistol;
			cs::WeaponId dropped;
			msg = s.inv.BuyWeapon(id, dropped);
			if (!msg)
			{
				if (dropped != cs::W_NONE) DropWeapon(s, old);
				PlaySfx(SfxName("items/gunpickup2.wav"));
				cs::Weapon* w = cs::Info(id).slot == cs::SLOT_PRIMARY ? &s.inv.primary : &s.inv.pistol;
				if (s.inv.active == w) { s.inv.active = nullptr; }
				SelectWeapon(s, w);
			}
			s.buyMenu = 0;
		}
		if (msg) Toast(msg);
	}

	// ---- 2D helpers --------------------------------------------------------------
	inline zCTexture* NamedTexture(const char* name);
	// Untextured DrawPolySimple shows nothing: solid fills use a white texture (convert_cs.py).
	inline zCTexture* WhiteTexture() { return NamedTexture("CSS_WHITE.TGA"); }

	inline void DrawRect(float x, float y, float w, float h, zCOLOR c, zTRnd_AlphaBlendFunc blend = zRND_ALPHA_FUNC_BLEND)
	{
		zTRndSimpleVertex v[4];
		const float px[4] = { x, x + w, x + w, x }, py[4] = { y, y, y + h, y + h };
		const float z = ogame->GetCamera()->nearClipZ + 1.0f;
		for (int k = 0; k < 4; k++) { v[k].pos = zVEC2(px[k], py[k]); v[k].z = z; v[k].uv = zVEC2(0.5f, 0.5f); v[k].color = c; }
		zrenderer->SetAlphaBlendFunc(blend);
		zCTexture* white = WhiteTexture();
		zrenderer->DrawPolySimple(white, v, 4);
		zTRndSimpleVertex r[4] = { v[3], v[2], v[1], v[0] };
		zrenderer->DrawPolySimple(white, r, 4);
	}

	inline zCTexture* NamedTexture(const char* name)
	{
		static std::map<std::string, zCTexture*> cache;
		auto it = cache.find(name);
		if (it != cache.end()) return it->second;
		zSTRING n(name);
		zCTexture* t = zCTexture::Load(n, 0);
		cache[name] = t;
		return t;
	}

	inline void DrawTexRect(zCTexture* tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1)
	{
		zTRndSimpleVertex v[4];
		const float px[4] = { x, x + w, x + w, x }, py[4] = { y, y, y + h, y + h };
		const float pu[4] = { u0, u1, u1, u0 }, pv[4] = { v0, v0, v1, v1 };
		const float z = ogame->GetCamera()->nearClipZ + 1.0f;
		for (int k = 0; k < 4; k++)
		{
			v[k].pos = zVEC2(px[k], py[k]); v[k].z = z; v[k].uv = zVEC2(pu[k], pv[k]);
			v[k].color = Rgba(255, 255, 255, 255);
		}
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_BLEND);
		zrenderer->DrawPolySimple(tex, v, 4);
		zTRndSimpleVertex r[4] = { v[3], v[2], v[1], v[0] };
		zrenderer->DrawPolySimple(tex, r, 4);
	}

	// CS sniper scope: four arc quarters around the centre, black outside, thin cross.
	inline void DrawScope()
	{
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		const float h = vp.ydimFloat, w = vp.xdimFloat, cx = vp.xcenter, cy = vp.ycenter;
		const float r = h * 0.5f;
		const zCOLOR black = Rgba(0, 0, 0, 255);
		DrawRect(0, 0, cx - r, h, black, zRND_ALPHA_FUNC_NONE);
		DrawRect(cx + r, 0, w - (cx + r), h, black, zRND_ALPHA_FUNC_NONE);
		DrawTexRect(NamedTexture("CSS_SCOPE_ARC_NW.TGA"), cx - r, cy - r, r, r, 0, 0, 1, 1);
		DrawTexRect(NamedTexture("CSS_SCOPE_ARC_NE.TGA"), cx, cy - r, r, r, 0, 0, 1, 1);
		DrawTexRect(NamedTexture("CSS_SCOPE_ARC_SW.TGA"), cx - r, cy, r, r, 0, 0, 1, 1);
		DrawTexRect(NamedTexture("CSS_SCOPE_ARC.TGA"), cx, cy, r, r, 0, 0, 1, 1);
		DrawRect(0, cy, w, 1, black, zRND_ALPHA_FUNC_NONE);
		DrawRect(cx, 0, 1, h, black, zRND_ALPHA_FUNC_NONE);
	}

	// Static CS crosshair (cl_dynamiccrosshair 0), default green.
	inline void DrawCrosshair()
	{
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		const float cx = vp.xcenter, cy = vp.ycenter;
		const float scale = vp.ydimFloat / 480.0f;
		const float gap = 4.0f * scale, len = 5.0f * scale, th = (std::max)(1.0f, scale * 0.75f);
		const zCOLOR c = Rgba(50, 250, 50, 220);
		DrawRect(cx - gap - len, cy, len, th, c);
		DrawRect(cx + gap, cy, len, th, c);
		DrawRect(cx, cy - gap - len, th, len, c);
		DrawRect(cx, cy + gap, th, len, c);
	}

	inline void DrawProgressBar(State& s)
	{
		if (s.progressTotal <= 0) return;
		const zTViewportData& vp = ogame->GetCamera()->vpData;
		float f = float((s.clock - s.progressStart) / s.progressTotal);
		f = f < 0 ? 0 : (f > 1 ? 1 : f);
		const float w = vp.xdimFloat * 0.4f, h = vp.ydimFloat * 0.02f, x = vp.xcenter - w * 0.5f, y = vp.ydimFloat * 0.7f;
		DrawRect(x - 2, y - 2, w + 4, h + 4, Rgba(0, 0, 0, 160));
		DrawRect(x, y, w * f, h, Rgba(255, 180, 30, 220));
	}

	inline void DrawSpriteHud(State& s);
	inline int RoundScoreT();
	inline int RoundScoreCT();
	struct RoundState;
	inline bool RoundHasBots();

	// Polygons (called in the screen-items phase, before the HUD text).
	inline void DrawHudPolys(State& s)
	{
		const bool scoped = s.inv.active && cs::IsSniper(s.inv.active->id) && s.fov != 90;
		if (scoped) DrawScope();
		else if (s.alive && !s.buyMenu) DrawCrosshair();
		DrawProgressBar(s);
		const int fa = FlashAlphaNow(s);
		if (fa > 0) DrawRect(0, 0, ogame->GetCamera()->vpData.xdimFloat, ogame->GetCamera()->vpData.ydimFloat, Rgba(255, 255, 255, fa));
		if (!s.alive) DrawRect(0, 0, ogame->GetCamera()->vpData.xdimFloat, ogame->GetCamera()->vpData.ydimFloat, Rgba(90, 0, 0, 90));
		DrawSpriteHud(s);
		zrenderer->FlushPolys();
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_NONE);
	}

	// Text (Gothic font, CS HUD amber), printed every frame from the game loop.
	inline void DrawHudText(State& s)
	{
		if (!screen) return;
		zCFont* oldFont = screen->GetFont();
		const zCOLOR oldColor = screen->fontColor;
		char buf[96];
		// round score (top centre)
		{
			SetFont("FONT_OLD_10_WHITE.TGA");
			SetColor(255, 200, 50, 200);
			sprintf_s(buf, "T %d : %d CT   (%s)", RoundScoreT(), RoundScoreCT(), s.inv.ct ? "playing CT" : "playing T");
			zSTRING t(buf);
			screen->Print(4096 - screen->FontSize(t) / 2, 120, t);
		}

		if (s.god && s.showDebug)
		{
			SetColor(80, 255, 120, 255);
			zSTRING t("GODMODE ON [Page Up]");
			screen->Print(4096 - screen->FontSize(t) / 2, 420, t);
		}

		// kill feed (top right)
		SetFont("FONT_OLD_10_WHITE.TGA");
		const DWORD now = GetTickCount();
		s.killFeed.erase(std::remove_if(s.killFeed.begin(), s.killFeed.end(), [&](const KillFeedLine& k) { return now > k.until; }), s.killFeed.end());
		int y = 300;
		for (const KillFeedLine& k : s.killFeed)
		{
			zSTRING t(k.text.c_str());
			SetColor(255, 255, 255, 230);
			screen->Print(8192 - 250 - screen->FontSize(t), y, t);
			y += screen->FontY() + 30;
		}

		// centre print (bomb / round messages) and short notices
		if (s.centre[0] && now < s.centreUntil)
		{
			SetFont("FONT_OLD_20_WHITE.TGA");
			SetColor(255, 255, 255, 255);
			zSTRING t(s.centre);
			screen->Print(4096 - screen->FontSize(t) / 2, 2000, t);
		}
		if (s.toast[0] && now < s.toastUntil)
		{
			SetFont("FONT_OLD_10_WHITE.TGA");
			SetColor(255, 220, 120, 255);
			zSTRING t(s.toast);
			screen->Print(4096 - screen->FontSize(t) / 2, 5600, t);
		}
		if (!s.alive)
		{
			SetFont("FONT_OLD_20_WHITE.TGA");
			SetColor(255, 80, 80, 255);
			zSTRING t(RoundHasBots() ? "You died - respawning next round" : "You died - respawning shortly");
			screen->Print(4096 - screen->FontSize(t) / 2, 3600, t);
		}
		DrawBuyMenu(s);
		screen->SetFont(oldFont);
		screen->SetFontColor(oldColor);
	}
}
