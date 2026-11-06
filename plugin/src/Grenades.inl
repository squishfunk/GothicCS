// GothicCS - CS grenades in Gothic's world: HE, flashbang, smoke.
// Throw (weapon side), entity physics and explosions follow
// THIRD_PARTY_NOTICES.md (ReGameDLL_CS, original CS 1.6). The bounce itself is
// the GoldSrc MOVETYPE_BOUNCE rule (overbounce 2 - friction, stop below 30 u/s
// on floors). The flying grenade is a Gothic vob with the CS world model.
// Included inside GOTHIC_NAMESPACE after Combat.inl and Effects.inl.

namespace GothicCS
{
	struct Grenade
	{
		cs::GrenadeKind kind = cs::GREN_HE;
		cs::Vec3 pos, vel;
		float gravity = 0.55f, friction = 0.7f;
		double detonateAt = 0;
		bool onGround = false;
		int bounces = 0;
		zCVob* vob = nullptr;
		float spin = 0;
		oCNpc* owner = nullptr;
		bool detonated = false;    // smoke: popped, still lying there
		double removeAt = 0;
	};

	struct SmokeCloud { cs::Vec3 center; double until; };

	inline std::vector<Grenade>& Grenades() { static std::vector<Grenade> g; return g; }
	inline std::vector<SmokeCloud>& SmokeClouds() { static std::vector<SmokeCloud> c; return c; }

	inline zCVob* MakeModelVob(const char* visual)
	{
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world) return nullptr;
		zCVob* v = new zCVob();
		v->SetVisual(zSTRING(visual));
		v->SetCollDetStat(FALSE);
		v->SetCollDetDyn(FALSE);
		v->ignoredByTraceRay = TRUE;
		v->dontWriteIntoArchive = TRUE;
		world->AddVob(v);
		return v;
	}

	inline void RemoveModelVob(zCVob*& v)
	{
		if (!v) return;
		if (oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr) world->RemoveVob(v);
		v->Release();
		v = nullptr;
	}

	// Place a CS model vob (model space = CS axes) at a CS position with a yaw.
	inline void PlaceModelVob(zCVob* v, const cs::Vec3& pos, float yawDeg, float rollDeg = 0)
	{
		if (!v) return;
		cs::Vec3 f, r, u;
		cs::AngleVectors(0, yawDeg, &f, &r, &u);
		// CS model axes: x forward, y left, z up
		const cs::Vec3 left = -r;
		const float c = std::cos(rollDeg * cs::kDegToRad), sn = std::sin(rollDeg * cs::kDegToRad);
		const cs::Vec3 l2 = left * c + u * sn, u2 = u * c - left * sn;
		// Gothic object space of a .3DS = (x, z, y) of the CS model axes
		const zVEC3 gx = DirToGothic(f), gy = DirToGothic(u2), gz = DirToGothic(l2), p = ToGothic(pos);
		zMAT4 m;
		m.v[0] = zVEC4(gx[0], gy[0], gz[0], p[0]);
		m.v[1] = zVEC4(gx[1], gy[1], gz[1], p[1]);
		m.v[2] = zVEC4(gx[2], gy[2], gz[2], p[2]);
		m.v[3] = zVEC4(0, 0, 0, 1);
		v->SetTrafoObjToWorld(m);
	}

	inline void ThrowGrenade(State& s, cs::GrenadeKind kind, const cs::Vec3& eye, float pitch, float yaw, const cs::Vec3& punch,
		const cs::Vec3& ownerVel, oCNpc* owner)
	{
		float ax = pitch + punch.x;
		if (ax < 0) ax = -10.0f + ax * (80.0f / 90.0f);
		else ax = -10.0f + ax * (100.0f / 90.0f);
		float vel = (90.0f - ax) * 6.0f;
		if (vel > 750.0f) vel = 750.0f;
		cs::Vec3 f;
		cs::AngleVectors(ax, yaw + punch.y, &f, nullptr, nullptr);
		Grenade g;
		g.kind = kind;
		g.pos = eye + f * 16.0f;
		g.vel = f * vel + ownerVel;
		if (kind == cs::GREN_HE) { g.gravity = 0.55f; g.friction = 0.7f; }
		else { g.gravity = 0.5f; g.friction = 0.8f; }
		g.detonateAt = s.clock + 1.5;
		g.owner = owner;
		g.spin = yaw;
		const char* visual = kind == cs::GREN_HE ? "CSW_HEGRENADE.3DS" : (kind == cs::GREN_FLASH ? "CSW_FLASHBANG.3DS" : "CSW_SMOKEGRENADE.3DS");
		g.vob = MakeModelVob(visual);
		PlaceModelVob(g.vob, g.pos, g.spin);
		Grenades().push_back(g);
	}

	// ---- explosions --------------------------------------------------------------
	inline void RadiusDamageAll(State& s, const cs::Vec3& src, float damage, float radius, const char* weapon, bool bomb);

	inline void ExplodeHE(State& s, Grenade& g)
	{
		const float now = (float)s.clock;
		// pull out of the floor: 45.6 units above it (dmg 100)
		cs::Vec3 p = g.pos;
		cs::TraceResult tr = s.world.TraceBox(p + cs::Vec3(0, 0, 8), p - cs::Vec3(0, 0, 32), cs::Vec3(), cs::Vec3());
		if (tr.fraction < 1.0f) p = tr.endpos + tr.normal * ((100.0f - 24.0f) * 0.6f);
		WorldSprite& a = AddWorldSprite("fexplo", p + cs::Vec3(0, 0, 20), now, 1.0f, 2.5f, 30.0f);
		a.life = (float)GetSprite("fexplo")->frames.size() / 30.0f;
		WorldSprite& b = AddWorldSprite("eexplo", p + cs::Vec3(s.rng.Float(-64, 64), s.rng.Float(-64, 64), s.rng.Float(30, 35)), now, 1.0f, 3.0f, 30.0f);
		b.life = (float)GetSprite("eexplo")->frames.size() / 30.0f;
		WorldSprite& sm = AddWorldSprite("steam1", p, now + 0.55f, 2.0f, 3.5f + s.rng.Float(0, 1.0f), 5.0f);
		sm.born = now + 0.55f;
		static const char* kExp[] = { "weapons/explode3.wav", "weapons/explode4.wav", "weapons/explode5.wav" };
		static const char* kDebris[] = { "weapons/debris1.wav", "weapons/debris2.wav", "weapons/debris3.wav" };
		PlaySfxAt(SfxName(kExp[s.rng.Long(0, 2)]), ToGothic(p));
		PlaySfxAt(SfxName(kDebris[s.rng.Long(0, 2)]), ToGothic(p));
		RadiusDamageAll(s, p + cs::Vec3(0, 0, 1), 100.0f, 350.0f, "grenade", false);
	}

	inline void BlindPlayer(State& s, float fadeTime, float hold, float alpha)
	{
		// stacking: an active full-white hold extends the new one
		const double now = s.clock;
		const float currentHold = float(s.flashStart + s.flashHold - now);
		if (currentHold > 0 && alpha == 255) hold += currentHold;
		if (s.flashStart != 0 && s.flashFade != 0 && s.flashStart + s.flashFade + s.flashHold > now)
		{
			if (s.flashFade > fadeTime) fadeTime = s.flashFade;
			if (s.flashAlpha >= alpha) alpha = s.flashAlpha;
		}
		s.flashStart = now;
		s.flashHold = hold;
		s.flashFade = fadeTime;
		s.flashAlpha = alpha;
	}

	inline void BlindBots(State& s, const cs::Vec3& src);

	inline void ExplodeFlash(State& s, Grenade& g)
	{
		const cs::Vec3 src = g.pos + cs::Vec3(0, 0, 1);
		PlaySfxAt(SfxName(s.rng.Long(0, 1) ? "weapons/flashbang-1.wav" : "weapons/flashbang-2.wav"), ToGothic(src));
		WorldSprite& sm = AddWorldSprite("steam1", src, (float)s.clock + 0.3f, 1.5f, 2.5f, 6.0f);
		sm.born = (float)s.clock + 0.3f;
		// RadiusFlash on the player: 1500 units, two-way line of sight
		if (s.alive)
		{
			const cs::Vec3 spot = s.pm.origin + cs::Vec3(0, 0, s.pm.viewHeight * s.rng.Float(0.5f, 1.1f));
			const float dist = cs::Length(spot - src);
			if (dist <= 1500.0f && LineOfSight(ToGothic(src), ToGothic(spot), player))
			{
				float adj = 4.0f - dist * (4.0f / 1500.0f);
				if (adj < 0) adj = 0;
				cs::Vec3 f;
				cs::AngleVectors(s.pitch, s.yaw, &f, nullptr, nullptr);
				const float dot = cs::Dot(src - s.pm.Eye(), f);
				if (dot < 0) BlindPlayer(s, adj * 1.75f, adj / 3.5f, 200.0f);
				else BlindPlayer(s, adj * 3.0f, adj / 1.5f, 255.0f);
			}
		}
		BlindBots(s, src);
	}

	inline void PopSmoke(State& s, Grenade& g)
	{
		PlaySfxAt(SfxName("weapons/sg_explode.wav"), ToGothic(g.pos));
		g.detonated = true;
		g.removeAt = s.clock + 21.1;
		g.vel = cs::Vec3(s.rng.Float(-175, 175), s.rng.Float(-175, 175), s.rng.Float(250, 350));
		g.onGround = false;
		// the cloud (client side in CS: gas_puff_01 sprites around the pop point, ~20 s)
		const float now = (float)s.clock;
		for (int i = 0; i < 26; i++)
		{
			const float a = s.rng.Float(0, 360) * cs::kDegToRad, r = s.rng.Float(0, 110);
			const cs::Vec3 p = g.pos + cs::Vec3(std::cos(a) * r, std::sin(a) * r, s.rng.Float(10, 90));
			WorldSprite& w = AddWorldSprite("gas_puff_01", p, now + i * 0.04f, 20.0f, s.rng.Float(2.2f, 3.2f), 0.0f);
			w.born = now + i * 0.04f;
			w.frame = s.rng.Long(0, 1);
			w.color = Rgba(150, 150, 150, 235);
			w.fadeIn = 1.0f;
			w.fadeOut = 5.0f;
			w.roll = s.rng.Float(0, 360);
			w.velocity = cs::Vec3(s.rng.Float(-3, 3), s.rng.Float(-3, 3), s.rng.Float(0, 2));
		}
		SmokeClouds().push_back({ g.pos + cs::Vec3(0, 0, 40), s.clock + 20.0 });
	}

	// Does a line pass through an active smoke cloud? (blocks bot vision)
	inline bool SmokeBlocks(State& s, const cs::Vec3& a, const cs::Vec3& b)
	{
		for (const SmokeCloud& c : SmokeClouds())
		{
			if (c.until < s.clock) continue;
			const cs::Vec3 ab = b - a;
			const float l2 = cs::Dot(ab, ab);
			float t = l2 > 0 ? cs::Dot(c.center - a, ab) / l2 : 0;
			t = t < 0 ? 0 : (t > 1 ? 1 : t);
			if (cs::Length(a + ab * t - c.center) < 120.0f) return true;
		}
		return false;
	}

	// ---- per 100 Hz step -------------------------------------------------------------
	inline void GrenadesStep(State& s, float dt)
	{
		auto& list = Grenades();
		for (Grenade& g : list)
		{
			if (!g.onGround)
			{
				g.vel.z -= 800.0f * g.gravity * dt;
				const cs::Vec3 end = g.pos + g.vel * dt;
				cs::TraceResult tr = s.world.TraceBox(g.pos, end, cs::Vec3(), cs::Vec3());
				if (tr.fraction < 1.0f && !tr.startsolid)
				{
					g.pos = tr.endpos;
					const float backoff = 2.0f - g.friction;
					g.vel = g.vel - tr.normal * (cs::Dot(g.vel, tr.normal) * backoff);
					if (tr.normal.z > 0.7f && cs::Dot(g.vel, g.vel) < 900.0f) { g.onGround = true; g.vel = cs::Vec3(); }
					else
					{
						if (g.bounces < 5)
						{
							const char* snd = g.kind == cs::GREN_HE ? "weapons/he_bounce-1.wav" :
								(s.rng.Long(0, 2) == 0 ? "weapons/grenade_hit1.wav" : (s.rng.Long(0, 1) ? "weapons/grenade_hit2.wav" : "weapons/grenade_hit3.wav"));
							PlaySfxAt(SfxName(snd), ToGothic(g.pos));
						}
						if (++g.bounces >= 10) { g.onGround = true; g.vel = cs::Vec3(); }
					}
				}
				else if (!tr.startsolid)
					g.pos = end;
				else
					g.vel = cs::Vec3();
				g.spin += cs::Length(g.vel) * dt * 2.0f;
			}
			else if (g.kind == cs::GREN_SMOKE)
				g.vel = g.vel * 0.95f;

			if (!g.detonated && s.clock >= g.detonateAt)
			{
				if (g.kind == cs::GREN_HE) { ExplodeHE(s, g); g.removeAt = s.clock; g.detonated = true; }
				else if (g.kind == cs::GREN_FLASH) { ExplodeFlash(s, g); g.removeAt = s.clock; g.detonated = true; }
				else if (g.onGround) PopSmoke(s, g);   // smoke pops only on the ground
			}
		}
		for (Grenade& g : list)
			if (g.detonated && s.clock >= g.removeAt) RemoveModelVob(g.vob);
		list.erase(std::remove_if(list.begin(), list.end(), [](const Grenade& g) { return g.vob == nullptr && g.detonated; }), list.end());
		auto& clouds = SmokeClouds();
		clouds.erase(std::remove_if(clouds.begin(), clouds.end(), [&](const SmokeCloud& c) { return c.until < s.clock; }), clouds.end());
	}

	inline void GrenadesFrame()
	{
		for (Grenade& g : Grenades())
			if (g.vob) PlaceModelVob(g.vob, g.pos, g.spin, g.onGround ? 90.0f : g.spin * 1.7f);
	}

	inline void RemoveAllGrenades()
	{
		for (Grenade& g : Grenades()) RemoveModelVob(g.vob);
		Grenades().clear();
		SmokeClouds().clear();
	}

	// Flashbang white-out colour for this frame (UTIL_ScreenFade FFADE_IN).
	inline int FlashAlphaNow(State& s)
	{
		if (s.flashStart == 0) return 0;
		const double t = s.clock - s.flashStart;
		if (t < s.flashHold) return (int)s.flashAlpha;
		if (s.flashFade <= 0) return 0;
		const double f = 1.0 - (t - s.flashHold) / s.flashFade;
		return f > 0 ? int(s.flashAlpha * f) : 0;
	}
}
