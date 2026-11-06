// GothicCS - combat: bullets (FireBullets3 with wall penetration), shotgun
// pellets, knife, damage to the player / bots / Gothic NPCs, kill feed.
// Included inside GOTHIC_NAMESPACE after GothicCS.inl, Hits.inl, Effects.inl.

namespace GothicCS
{
	struct Bot;
	inline Bot* BotOf(oCNpc* npc);
	inline void DamageBot(State& s, Bot& b, float damage, cs::HitGroup group, float armorRatio, const char* weaponName,
		bool blast = false);

	// Who fires: the player or a bot.
	struct Shooter
	{
		cs::Vec3 eye;
		float pitch = 0, yaw = 0;
		oCNpc* self = nullptr;     // never hit by own bullets
		bool isPlayer = true;
		cs::Rng* rng = nullptr;
		const char* weapon = "";
		float armorRatio = 1.0f;
		const char* name = "";
	};

	// ---- kills ---------------------------------------------------------------
	inline void OnKill(State& s, const char* victim, const char* weapon, bool headshot)
	{
		char buf[160];
		sprintf_s(buf, "Nameless Hero  [%s%s]  %s", weapon, headshot ? " +HS" : "", victim);
		s.killFeed.push_back({ buf, GetTickCount() + 6000 });
		if (s.killFeed.size() > 5) s.killFeed.erase(s.killFeed.begin());
		s.inv.AddMoney(cs::REWARD_KILL);
	}

	// CS damage on a Gothic NPC (not a bot), through Gothic's own damage code.
	inline void DamageNpc(State& s, oCNpc* npc, float csDamage, cs::HitGroup group, const zVEC3& at, const char* weapon)
	{
		if (!npc || npc->IsDead()) return;
		if (Bot* b = BotOf(npc)) { DamageBot(s, *b, csDamage, group, 1.0f, weapon); return; }
		const int hpBefore = npc->GetAttribute(NPC_ATR_HITPOINTS);
		const int hpMax = npc->GetAttribute(NPC_ATR_HITPOINTSMAX);
		float pass = csDamage;
		if (Cfg().damageCS)   // every NPC counts as a 100 HP CS player; Gothic armour skipped
			pass = csDamage * float(hpMax > 0 ? hpMax : 100) / 100.0f + float(npc->protection[oEDamageIndex_Point]);
		npc->OnDamage(player, player, pass, oEDamageType_Point, at);
		sprintf_s(s.lastShot, "%s [%s] CS dmg %.1f -> HP %d -> %d / %d", npc->GetName(0).ToChar(), cs::HitGroupName(group),
			csDamage, hpBefore, npc->GetAttribute(NPC_ATR_HITPOINTS), hpMax);
		if (npc->IsDead()) OnKill(s, npc->GetName(0).ToChar(), weapon, group == cs::HitGroup::Head);
	}

	// The player's CS hull hit by a ray (bot bullets): entry distance in units or -1.
	inline float RayPlayerHull(State& s, const cs::Vec3& o, const cs::Vec3& d, float len, cs::HitGroup& group)
	{
		if (!s.alive) return -1;
		const cs::Vec3 mn = s.pm.origin + s.pm.Mins(), mx = s.pm.origin + s.pm.Maxs();
		float t0 = 0, t1 = len;
		for (int i = 0; i < 3; i++)
		{
			if (std::fabs(d[i]) < 1e-8f) { if (o[i] < mn[i] || o[i] > mx[i]) return -1; continue; }
			float a = (mn[i] - o[i]) / d[i], c = (mx[i] - o[i]) / d[i];
			if (a > c) std::swap(a, c);
			t0 = (std::max)(t0, a);
			t1 = (std::min)(t1, c);
			if (t0 > t1) return -1;
		}
		// hit group by height in the hull (CS player hitboxes: head / chest / stomach / legs)
		const float f = (o.z + d.z * t0 - mn.z) / (mx.z - mn.z);
		group = f > 0.86f ? cs::HitGroup::Head : (f > 0.6f ? cs::HitGroup::Chest : (f > 0.45f ? cs::HitGroup::Stomach : cs::HitGroup::Leg));
		return t0;
	}

	// FireBullets3: one bullet with CS penetration through world surfaces and bodies.
	inline void FireBullets3(State& s, const Shooter& sh, const cs::Vec3& dirIn, float distance, int penetration,
		cs::BulletType bullet, float damage, float rangeMod)
	{
		int power;
		float penDistance;
		cs::BulletPenetration(bullet, power, penDistance);
		cs::Vec3 dir = cs::Normalized(dirIn);
		cs::Vec3 src = sh.eye;
		int curDamage = (int)damage;
		float dmgModifier = 0.5f;
		bool first = true;
		s.shotsTotal += sh.isPlayer ? 1 : 0;
		while (penetration != 0)
		{
			const zVEC3 o = ToGothic(src), d = DirToGothic(dir);
			const float lenCm = distance * kCmPerUnit;
			const WorldHit wh = TraceWorld(o, d, lenCm, sh.self);
			const NpcHit nh = TraceCharacters(o, d, wh.hit ? wh.dist : lenCm, sh.self, player);
			cs::HitGroup pg = cs::HitGroup::Generic;
			float tPlayer = sh.isPlayer ? -1.0f : RayPlayerHull(s, src, dir, distance, pg);
			const float tWorld = wh.hit ? wh.dist / kCmPerUnit : 1e30f;
			const float tNpc = nh.npc ? nh.dist / kCmPerUnit : 1e30f;
			if (tPlayer >= 0 && (tPlayer > tWorld || tPlayer > tNpc)) tPlayer = -1;
			const float tHit = tPlayer >= 0 ? tPlayer : (std::min)(tWorld, tNpc);
			if (tHit >= 1e29f) break;   // nothing hit

			const bool bodyHit = tPlayer >= 0 || tNpc <= tWorld;
			if (!bodyHit)
			{
				// texture-dependent penetration (CS: metal, concrete, wood; Gothic material groups)
				if (wh.mat == zMAT_GROUP_METAL) { power = int(power * 0.15f); dmgModifier = 0.2f; }
				else if (wh.mat == zMAT_GROUP_STONE) power = int(power * 0.25f);
				else if (wh.mat == zMAT_GROUP_WOOD) dmgModifier = 0.6f;
			}
			penetration--;
			const float curDistance = tHit;
			curDamage = int(curDamage * std::pow(rangeMod, curDistance / 500.0f));
			if (curDistance > penDistance) penetration = 0;
			float distModifier;
			if (bodyHit || penetration == 0) { power = 42; dmgModifier = 0.75f; distModifier = 0.75f; }
			else distModifier = 0.5f;

			const cs::Vec3 hitPos = src + dir * curDistance;
			if (tPlayer >= 0)
			{
				const float dmg = curDamage * cs::HitGroupMultiplier(pg);
				DamagePlayer(s, dmg, pg, sh.armorRatio, sh.name, sh.weapon);
			}
			else if (bodyHit && nh.npc)
			{
				const float dmg = curDamage * cs::HitGroupMultiplier(nh.group);
				if (sh.isPlayer)
				{
					if (Bot* b = BotOf(nh.npc)) DamageBot(s, *b, dmg, nh.group, sh.armorRatio, sh.weapon);
					else DamageNpc(s, nh.npc, dmg, nh.group, nh.point, sh.weapon);
				}
			}
			else
			{
				if (first || sh.isPlayer) Impact(s, wh);
				if (sh.isPlayer)
					sprintf_s(s.lastShot, "shot %d: %s, %.1f m, mat %d, dmg %d", s.shotsTotal,
						wh.vob ? "obiekt" : "swiat", curDistance * kCmPerUnit / 100.0f, wh.mat, curDamage);
			}
			first = false;

			// continue behind the surface; for a wall the exit side must lie within `power`
			const cs::Vec3 newSrc = hitPos + dir * (float)power;
			if (!bodyHit && penetration != 0)
			{
				const zVEC3 back = ToGothic(hitPos) - ToGothic(newSrc);
				const float bl = back.Length();
				const WorldHit exitHit = bl > 1 ? TraceWorld(ToGothic(newSrc), back * (1.0f / bl), bl, sh.self) : WorldHit();
				if (!exitHit.hit) break;   // still inside: thicker than the penetration power
			}
			src = newSrc;
			distance = (distance - curDistance) * distModifier;
			curDamage = int(curDamage * dmgModifier);
			if (distance <= 0 || curDamage <= 0) break;
		}
	}

	// Shotgun FireBullets: pellets, damage 20 * (1 - fraction), no penetration.
	inline void FireBuckshot(State& s, const Shooter& sh, int pellets, float cone, float distance, const cs::Vec3& punch)
	{
		for (int i = 0; i < pellets; i++)
		{
			const cs::Vec3 dir = cs::Normalized(cs::PelletDirection(sh.pitch, sh.yaw, punch, cone, *sh.rng));
			const zVEC3 o = ToGothic(sh.eye), d = DirToGothic(dir);
			const float lenCm = distance * kCmPerUnit;
			const WorldHit wh = TraceWorld(o, d, lenCm, sh.self);
			const NpcHit nh = TraceCharacters(o, d, wh.hit ? wh.dist : lenCm, sh.self, player);
			cs::HitGroup pg = cs::HitGroup::Generic;
			const float tPlayer = sh.isPlayer ? -1.0f : RayPlayerHull(s, sh.eye, dir, distance, pg);
			const float tWorld = wh.hit ? wh.dist / kCmPerUnit : 1e30f;
			const float tNpc = nh.npc ? nh.dist / kCmPerUnit : 1e30f;
			if (tPlayer >= 0 && tPlayer < tWorld && tPlayer < tNpc)
			{
				const float dmg = int(20.0f * (1.0f - tPlayer / distance)) * cs::HitGroupMultiplier(pg);
				DamagePlayer(s, dmg, pg, 1.0f, sh.name, sh.weapon);
			}
			else if (nh.npc && tNpc <= tWorld)
			{
				const float dmg = int(20.0f * (1.0f - tNpc / distance)) * cs::HitGroupMultiplier(nh.group);
				if (sh.isPlayer)
				{
					if (Bot* b = BotOf(nh.npc)) DamageBot(s, *b, dmg, nh.group, 1.0f, sh.weapon);
					else DamageNpc(s, nh.npc, dmg, nh.group, nh.point, sh.weapon);
				}
			}
			else if (wh.hit && (i % 3) == 0)
				Impact(s, wh);
		}
	}

	// Knife: a line along the view (no punch); when it misses, a fatter test
	// (CS: head hull + FindHullIntersection).
	inline cs::KnifeResult KnifeAttack(State& s, const Shooter& sh, float range, float damage, bool stab)
	{
		cs::Vec3 f;
		cs::AngleVectors(sh.pitch, sh.yaw, &f, nullptr, nullptr);
		const zVEC3 o = ToGothic(sh.eye), d = DirToGothic(f);
		const float lenCm = range * kCmPerUnit;
		const WorldHit wh = TraceWorld(o, d, lenCm, sh.self);
		NpcHit nh = TraceCharacters(o, d, wh.hit ? wh.dist : lenCm, sh.self, player);
		if (!nh.npc && !wh.hit)
		{
			// hull fallback: the 8 corners of a 16-unit box around the line end
			for (int k = 0; k < 8 && !nh.npc; k++)
			{
				const cs::Vec3 off((k & 1) ? 8.f : -8.f, (k & 2) ? 8.f : -8.f, (k & 4) ? 8.f : -8.f);
				const cs::Vec3 end = sh.eye + f * range + off;
				const cs::Vec3 dd = end - sh.eye;
				const float l = cs::Length(dd);
				nh = TraceCharacters(o, DirToGothic(dd * (1.0f / l)), l * kCmPerUnit, sh.self, player);
			}
		}
		if (nh.npc && (!wh.hit || nh.dist <= wh.dist))
		{
			float dmg = damage * cs::HitGroupMultiplier(nh.group);
			if (stab)
			{
				// backstab: victim faces away from the attacker
				zVEC3 at = nh.npc->GetAtVectorWorld();
				cs::Vec3 va = DirToCS(at);
				va.z = 0; f.z = 0;
				if (cs::Dot(cs::Normalized(f), cs::Normalized(va)) > 0.8f) dmg *= 3.0f;
			}
			if (Bot* b = BotOf(nh.npc)) DamageBot(s, *b, dmg, nh.group, 1.7f, "knife");
			else DamageNpc(s, nh.npc, dmg, nh.group, nh.point, "knife");
			return cs::KNIFE_BODY;
		}
		return wh.hit ? cs::KNIFE_WORLD : cs::KNIFE_MISS;
	}

	// ---- the player as weapon host ----------------------------------------------
	inline void ThrowGrenade(State& s, cs::GrenadeKind kind, const cs::Vec3& eye, float pitch, float yaw, const cs::Vec3& punch,
		const cs::Vec3& ownerVel, oCNpc* owner);
	inline bool InBombZone(State& s, const cs::Vec3& origin);
	inline void PlantBomb(State& s, const cs::Vec3& feet);
	inline void SelectWeapon(State& s, cs::Weapon* w);

	struct PlayerHost : cs::WeaponHost
	{
		State& s;
		explicit PlayerHost(State& st) : s(st) {}
		Shooter Me()
		{
			Shooter sh;
			sh.eye = s.pm.Eye();
			sh.pitch = s.pitch; sh.yaw = s.yaw;
			sh.self = player;
			sh.isPlayer = true;
			sh.rng = &s.rng;
			sh.weapon = s.inv.active ? s.inv.active->I().model : "";
			sh.armorRatio = s.inv.active ? s.inv.active->I().armorRatio : 1.0f;
			sh.name = "Nameless Hero";
			return sh;
		}
		float Speed2D() override { return cs::Length2D(s.pm.velocity); }
		bool OnGround() override { return s.pm.onGround; }
		bool Ducking() override { return s.pm.ducked; }
		cs::Vec3& Punch() override { return s.punch; }
		int& Fov() override { return s.fov; }
		int& AmmoReserve(cs::AmmoType t) override { return s.inv.ammo[t]; }
		float& NextAttack() override { return playerNextAttack; }
		cs::Rng& Random() override { return s.rng; }
		void Anim(int sequence) override { if (s.vm) s.vm->PlayIndex(sequence); }
		void Sound(const char* csPath, float) override { PlaySfx(SfxName(csPath)); }
		void FireBullets3(float spread, float distance, int penetration, cs::BulletType bullet, float damage, float rangeMod,
			const cs::Vec3& punch, float sideOffset) override
		{
			Shooter sh = Me();
			if (sideOffset != 0)
			{
				cs::Vec3 r;
				cs::AngleVectors(s.pitch, s.yaw, nullptr, &r, nullptr);
				sh.eye += r * sideOffset;
			}
			const cs::Vec3 dir = cs::ShotDirection(s.pitch, s.yaw, punch, spread, s.rng);
			GothicCS::FireBullets3(s, sh, dir, distance, penetration, bullet, damage, rangeMod);
		}
		void FireBuckshot(int pellets, float cone, float distance, const cs::Vec3& punch) override
		{
			GothicCS::FireBuckshot(s, Me(), pellets, cone, distance, punch);
		}
		cs::KnifeResult KnifeAttack(float range, float damage, bool stab) override
		{
			return GothicCS::KnifeAttack(s, Me(), range, damage, stab);
		}
		void ThrowGrenade(cs::GrenadeKind kind, const cs::Vec3& punch) override
		{
			GothicCS::ThrowGrenade(s, kind, s.pm.Eye(), s.pitch, s.yaw, punch, s.pm.velocity, player);
		}
		bool InBombZone() override { return GothicCS::InBombZone(s, s.pm.origin); }
		void PlantBomb() override { GothicCS::PlantBomb(s, s.pm.Feet()); }
		void Message(const char* text) override { Toast(text); }
		void Progress(float seconds) override { s.progressTotal = seconds; s.progressStart = s.clock; }
		void Freeze(bool frozen) override { s.frozen = frozen; }
		void Retire() override { SelectWeapon(s, s.inv.Best(s.inv.active)); }
		void MuzzleFlash() override {}   // the viewmodel's studio event 5001 draws it

		float playerNextAttack = 0;
	};

	inline PlayerHost& Host() { static PlayerHost h(S()); return h; }

	inline float MaxSpeedOf(State& s)
	{
		if (!s.inv.active) return 250.0f;
		const cs::WeaponInfo& in = s.inv.active->I();
		return s.fov != 90 ? in.zoomSpeed : in.maxSpeed;
	}

	inline ViewModel* GetViewModel(State& s, const char* model)
	{
		auto it = s.viewmodels.find(model);
		if (it != s.viewmodels.end()) return it->second;
		ViewModel* vm = new ViewModel();
		if (!vm->Load(model)) Toast(("Missing model " + std::string(model) + " - run tools/convert_cs.py").c_str());
		s.viewmodels[model] = vm;
		return vm;
	}

	inline void SelectWeapon(State& s, cs::Weapon* w)
	{
		if (!w || w == s.inv.active) return;
		if (cs::IsGrenade(w->id) && w->count <= 0) return;
		if (w->id == cs::W_C4 && w->count <= 0) return;
		if (s.inv.active) s.inv.active->Holster(Host());
		s.inv.last = s.inv.active;
		s.inv.active = w;
		s.vm = GetViewModel(s, w->I().model);
		w->Deploy((float)s.clock, Host());
		s.frozen = false;
		s.progressTotal = 0;
	}

	// ---- damage to the player -------------------------------------------------------
	inline void PlayerDie(State& s, const char* attacker, const char* weapon, bool headshot)
	{
		s.alive = false;
		s.deathTime = s.clock;
		s.frozen = false;
		s.progressTotal = 0;
		static const char* kDie[] = { "player/die1.wav", "player/die2.wav", "player/die3.wav" };
		PlaySfx(SfxName(kDie[s.rng.Long(0, 2)]));
		char buf[160];
		sprintf_s(buf, "%s  [%s%s]  Nameless Hero", attacker, weapon, headshot ? " +HS" : "");
		s.killFeed.push_back({ buf, GetTickCount() + 6000 });
		if (s.killFeed.size() > 5) s.killFeed.erase(s.killFeed.begin());
	}

	inline void DamagePlayer(State& s, float damage, cs::HitGroup group, float armorRatio, const char* attacker,
		const char* weapon, bool blast)
	{
		if (!s.alive || damage <= 0 || s.god) return;
		const int armorBefore = s.inv.armor;
		cs::ApplyCsDamage(s.inv.health, s.inv.armor, s.inv.helmet, damage, group, armorRatio, blast);
		if (!blast)
		{
			if (group == cs::HitGroup::Head)
			{
				PlaySfx(SfxName(s.inv.helmet && armorBefore > 0 ? "player/bhit_helmet-1.wav" :
					(s.rng.Long(0, 2) == 0 ? "player/headshot1.wav" : (s.rng.Long(0, 1) ? "player/headshot2.wav" : "player/headshot3.wav"))));
				s.punch.x = (std::max)(-12.0f, damage * -0.5f);
			}
			else if (armorBefore > 0 && group != cs::HitGroup::Leg) PlaySfx(SfxName("player/bhit_kevlar-1.wav"));
			else
			{
				static const char* kFlesh[] = { "player/bhit_flesh-1.wav", "player/bhit_flesh-2.wav", "player/bhit_flesh-3.wav" };
				PlaySfx(SfxName(kFlesh[s.rng.Long(0, 2)]));
				if (group == cs::HitGroup::Chest) s.punch.x = (std::max)(-4.0f, damage * -0.1f);
			}
		}
		if (s.inv.health <= 0)
		{
			s.inv.health = 0;
			PlayerDie(s, attacker, weapon, group == cs::HitGroup::Head);
		}
	}
}
