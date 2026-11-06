// GothicCS - bombsites and the C4 (plant, beeps, defuse, explosion).
// Rules from THIRD_PARTY_NOTICES.md (ReGameDLL_CS, original CS 1.6):
// planting 3 s on the ground inside a bomb zone (point bomb target: within
// 256 units), mp_c4timer 45 s, beep every 1.4 s with the beep wave changing
// at 0 / 11 / 20.9 / 29.81 / 37.83 s, LED glow every 2 s, defuse with USE
// within 64 units and +-45 deg (10 s, 5 s with a kit), explosion 500 damage
// in 1750 units (linear, through walls).
// Bombsites are chosen in game (END) and saved per world in
// <Gothic>\System\GothicCS_bombsites.ini.
// Included inside GOTHIC_NAMESPACE after Grenades.inl.

namespace GothicCS
{
	struct PlantedBomb
	{
		bool planted = false;
		cs::Vec3 pos;
		double plantTime = 0, blowTime = 0;
		double nextBeep = 0, nextBlink = 0, nextFreq = 0;
		float freqInterval = 11.0f;
		int wave = 0;
		const char* beep = "weapons/c4_beep1.wav";
		zCVob* vob = nullptr;
		float yaw = 0;
		bool defusing = false;
		double defuseEnd = 0, nextDefuse = 0;
		bool exploded = false;
		bool defusedThisRound = false;
		double removeAt = 0;
	};
	inline PlantedBomb& Bomb() { static PlantedBomb b; return b; }

	inline std::string WorldKey()
	{
		oCWorld* w = ogame ? ogame->GetGameWorld() : nullptr;
		return w ? std::string(w->GetWorldFilename().ToChar()) : std::string("WORLD");
	}

	inline std::string BombsiteIni() { return SystemDir() + "\\GothicCS_bombsites.ini"; }

	inline void LoadBombsites(State& s)
	{
		s.bombsites.clear();
		const std::string ini = BombsiteIni(), key = WorldKey();
		for (char name : { 'A', 'B' })
		{
			char v[128] = {}, k[2] = { name, 0 };
			GetPrivateProfileStringA(key.c_str(), k, "", v, sizeof(v), ini.c_str());
			cs::Vec3 p;
			if (sscanf_s(v, "%f %f %f", &p.x, &p.y, &p.z) == 3) s.bombsites.push_back({ p, name });
		}
	}

	inline void SaveBombsites(State& s)
	{
		const std::string ini = BombsiteIni(), key = WorldKey();
		WritePrivateProfileStringA(key.c_str(), "A", nullptr, ini.c_str());
		WritePrivateProfileStringA(key.c_str(), "B", nullptr, ini.c_str());
		for (const Bombsite& b : s.bombsites)
		{
			char v[128], k[2] = { b.name, 0 };
			sprintf_s(v, "%.1f %.1f %.1f", b.pos.x, b.pos.y, b.pos.z);
			WritePrivateProfileStringA(key.c_str(), k, v, ini.c_str());
		}
	}

	// END: first A, then B, then clear both.
	inline void CycleBombsite(State& s)
	{
		if (s.bombsites.size() >= 2) { s.bombsites.clear(); Toast("Bombsites cleared"); }
		else
		{
			const char name = s.bombsites.empty() ? 'A' : 'B';
			s.bombsites.push_back({ s.pm.Feet(), name });
			char msg[96];
			sprintf_s(msg, "Bombsite %c set here (radius 256 units = 6.5 m)", name);
			Toast(msg);
		}
		SaveBombsites(s);
	}

	inline const Bombsite* BombsiteAt(State& s, const cs::Vec3& origin)
	{
		for (const Bombsite& b : s.bombsites)
			if (cs::Length(origin - b.pos) <= 256.0f + 36.0f) return &b;   // 256 from the hull centre (feet + 36)
		return nullptr;
	}

	inline bool InBombZone(State& s, const cs::Vec3& origin) { return Cfg().bombAnywhere || BombsiteAt(s, origin) != nullptr; }

	inline void PlantBomb(State& s, const cs::Vec3& feet)
	{
		PlantedBomb& b = Bomb();
		RemoveModelVob(b.vob);
		b = PlantedBomb();
		b.planted = true;
		b.pos = feet;
		b.plantTime = s.clock;
		b.blowTime = s.clock + 45.0;
		b.nextFreq = s.clock;
		b.freqInterval = float(45 / 4);   // integer division, as in CS: 11
		b.nextBeep = s.clock + 0.5;
		b.nextBlink = s.clock + 2.0;
		b.yaw = s.yaw;
		b.vob = MakeModelVob("CSW_C4.3DS");
		PlaceModelVob(b.vob, b.pos, b.yaw);
		PlaySfxAt(SfxName("weapons/c4_plant.wav"), ToGothic(feet));
		PlaySfx(SfxName("radio/bombpl.wav"));
		CentrePrint("The bomb has been planted!");
	}

	inline void RadiusDamageAll(State& s, const cs::Vec3& src, float damage, float radius, const char* weapon, bool bomb);

	inline void ExplodeBomb(State& s)
	{
		PlantedBomb& b = Bomb();
		b.exploded = true;
		b.planted = false;
		b.defusing = false;
		s.frozen = false;
		s.progressTotal = 0;
		const float now = (float)s.clock;
		cs::Vec3 p = b.pos + cs::Vec3(0, 0, 45.6f);
		// four TE_SPRITE at scale 15.1 (byte 151): fexplo, eexplo, fexplo, zerogxplode
		AddWorldSprite("fexplo", p - cs::Vec3(0, 0, 10), now, (float)GetSprite("fexplo")->frames.size() / 15.0f, 15.1f, 15.0f).color = Rgba(150, 150, 150, 255);
		const cs::Vec3 off(s.rng.Float(-512, 512), s.rng.Float(-512, 512), s.rng.Float(-10, 10));
		AddWorldSprite("eexplo", p + off, now, (float)GetSprite("eexplo")->frames.size() / 15.0f, 15.1f, 15.0f).color = Rgba(150, 150, 150, 255);
		AddWorldSprite("fexplo", p + off, now, (float)GetSprite("fexplo")->frames.size() / 15.0f, 15.1f, 15.0f).color = Rgba(150, 150, 150, 255);
		AddWorldSprite("zerogxplode", p + off, now, (float)GetSprite("zerogxplode")->frames.size() / 15.0f, 15.1f, 15.0f).color = Rgba(17, 17, 17, 255);
		WorldSprite& sm = AddWorldSprite("steam1", p, now + 0.85f, 3.0f, 15.0f, 8.0f);
		sm.born = now + 0.85f;
		PlaySfx(SfxName("weapons/c4_explode1.wav"));   // heard map-wide (attenuation 0.25)
		RadiusDamageAll(s, p + cs::Vec3(0, 0, 1), 500.0f, 1750.0f, "C4", true);
		b.removeAt = s.clock + 0.1;
	}

	inline void DefuseEnd(State& s, bool success)
	{
		PlantedBomb& b = Bomb();
		b.defusing = false;
		s.frozen = false;
		s.progressTotal = 0;
		if (!success) return;
		PlaySfxAt(SfxName("weapons/c4_disarmed.wav"), ToGothic(b.pos));
		PlaySfx(SfxName("radio/bombdef.wav"));
		b.planted = false;
		b.defusedThisRound = true;
		RemoveModelVob(b.vob);
	}

	// E held this step: start / keep defusing (CTs only, 64 units, +-45 deg).
	inline void BombUse(State& s, bool useHeld)
	{
		PlantedBomb& b = Bomb();
		if (!b.planted || !s.alive) return;
		if (!useHeld) return;
		const cs::Vec3 eye = s.pm.Eye();
		const cs::Vec3 to = b.pos + cs::Vec3(0, 0, 4) - eye;
		cs::Vec3 f;
		cs::AngleVectors(s.pitch, s.yaw, &f, nullptr, nullptr);
		const bool inReach = cs::Length(b.pos - s.pm.origin) <= 64.0f + 36.0f && cs::Dot(cs::Normalized(to), f) > 0.7f;
		if (!inReach) return;
		if (!s.inv.ct) { if (!b.defusing) Toast("Only CT can defuse (M - switch team)"); return; }
		if (b.defusing) { b.nextDefuse = s.clock + 0.5; return; }
		b.defusing = true;
		const float t = s.inv.defuser ? 5.0f : 10.0f;
		b.defuseEnd = s.clock + t;
		b.nextDefuse = s.clock + 0.5;
		s.frozen = true;
		s.progressTotal = t;
		s.progressStart = s.clock;
		Toast(s.inv.defuser ? "Defusing Bomb With Defuse Kit" : "Defusing Bomb Without Defuse Kit");
		PlaySfx(SfxName("weapons/c4_disarm.wav"));
	}

	// C4Think, per 100 Hz step.
	inline void BombStep(State& s)
	{
		PlantedBomb& b = Bomb();
		if (b.exploded && b.removeAt && s.clock >= b.removeAt) { RemoveModelVob(b.vob); b.removeAt = 0; }
		if (!b.planted) return;
		if (s.clock >= b.nextFreq)
		{
			b.nextFreq = s.clock + b.freqInterval;
			b.freqInterval *= 0.9f;
			static const char* kBeep[] = { "weapons/c4_beep1.wav", "weapons/c4_beep2.wav", "weapons/c4_beep3.wav",
				"weapons/c4_beep4.wav", "weapons/c4_beep5.wav" };
			if (b.wave < 5) b.beep = kBeep[b.wave];
			b.wave++;
		}
		if (s.clock >= b.nextBeep)
		{
			b.nextBeep = s.clock + 1.4;
			PlaySfxAt(SfxName(b.beep), ToGothic(b.pos));
		}
		if (s.clock >= b.nextBlink)
		{
			b.nextBlink = s.clock + 2.0;
			WorldSprite& led = AddWorldSprite("ledglow", b.pos + cs::Vec3(0, 0, 5), (float)s.clock, 0.1f, 0.3f, 0.0f);
			led.color = Rgba(255, 255, 255, 255);
		}
		if (s.clock >= b.blowTime) { ExplodeBomb(s); return; }   // the explosion beats a defuse in progress
		if (b.defusing)
		{
			if (s.clock > b.nextDefuse || !s.pm.onGround)
			{
				if (!s.pm.onGround) Toast("You must be on the ground to defuse");
				DefuseEnd(s, false);
			}
			else if (s.clock >= b.defuseEnd) DefuseEnd(s, true);
		}
	}

	inline void RemoveBomb()
	{
		RemoveModelVob(Bomb().vob);
		Bomb() = PlantedBomb();
	}
}
