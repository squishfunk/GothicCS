// GothicCS - CS bots: Gothic NPCs played like Counter-Strike players.
// The NPC (a Gothic human instance from the scripts) is taken out of
// Gothic's AI (asleep, movement locked) and driven by the same CS movement
// and weapon code as the player; it holds the CS player-weapon model
// (p_*.mdl -> CSP_*.3DS) in its right hand and uses Gothic's own crossbow /
// run animations. Damage between player and bots uses the CS rules.
// Included inside GOTHIC_NAMESPACE after Combat.inl / Grenades.inl / Bomb.inl.

namespace GothicCS
{
	struct Bot
	{
		oCNpc* npc = nullptr;
		cs::PlayerMove pm;
		cs::Weapon weapon;
		int ammo[cs::A_COUNT] = {};
		cs::Rng rng;
		cs::Vec3 punch;
		int fov = 90;
		float nextAttack = 0;
		float yaw = 0, pitch = 0;
		int health = 100, armor = 50;   // bots buy kevlar + helmet
		bool helmet = false;
		bool dead = false;
		float hipHeight = 90.0f;
		zCVob* gun = nullptr;
		std::string name;
		// behaviour
		cs::Vec3 lastSeen, guardPost;
		double lastSeenAt = 0, reactAt = 0, defuseStarted = -1;
		bool seen = false, everSeen = false;
		float losCheckAt = 0, strafeUntil = 0, burstUntil = 0, pauseUntil = 0, aimJitterAt = 0;
		double blindUntil = 0;
		float strafe = 0;
		float aimErrYaw = 0, aimErrPitch = 0;
		int aniIdle = -1, aniRun = -1, aniCurrent = -1;
		double muzzleFlashUntil = 0;
	};

	inline std::vector<Bot*>& Bots() { static std::vector<Bot*> b; return b; }

	inline Bot* BotOf(oCNpc* npc)
	{
		for (Bot* b : Bots()) if (b->npc == npc && !b->dead) return b;
		return nullptr;
	}

	inline void BotTakeControl(oCNpc* npc);

	inline bool IsBotNpc(oCNpc* npc) { return npc && BotOf(npc); }

	// The bot's NPC is in the world (Gothic may still take it out, e.g. a world
	// change). Out of the world: no thinking, no gun; back in: take control again.
	inline bool BotInWorld(Bot& b)
	{
		const bool in = b.npc && b.npc->GetHomeWorld() != nullptr;
		if (!in && b.gun) RemoveModelVob(b.gun);
		if (in && !b.gun)
		{
			BotTakeControl(b.npc);
			b.aniCurrent = -1;
			b.gun = MakeModelVob((std::string("CSP_") + b.weapon.I().model + ".3DS").c_str());
		}
		return in;
	}

	struct BotHost : cs::WeaponHost
	{
		State& s;
		Bot& b;
		BotHost(State& st, Bot& bot) : s(st), b(bot) {}
		Shooter Me()
		{
			Shooter sh;
			sh.eye = b.pm.Eye();
			sh.pitch = b.pitch + b.aimErrPitch; sh.yaw = b.yaw + b.aimErrYaw;
			sh.self = b.npc;
			sh.isPlayer = false;
			sh.rng = &b.rng;
			sh.weapon = b.weapon.I().model;
			sh.armorRatio = b.weapon.I().armorRatio;
			sh.name = b.name.c_str();
			return sh;
		}
		float Speed2D() override { return cs::Length2D(b.pm.velocity); }
		bool OnGround() override { return b.pm.onGround; }
		bool Ducking() override { return b.pm.ducked; }
		cs::Vec3& Punch() override { return b.punch; }
		int& Fov() override { return b.fov; }
		int& AmmoReserve(cs::AmmoType t) override { return b.ammo[t]; }
		float& NextAttack() override { return b.nextAttack; }
		cs::Rng& Random() override { return b.rng; }
		void Anim(int) override {}
		void Sound(const char* csPath, float) override { PlaySfx3D(SfxName(csPath), b.npc); }
		void FireBullets3(float spread, float distance, int penetration, cs::BulletType bullet, float damage, float rangeMod,
			const cs::Vec3& punch, float) override
		{
			Shooter sh = Me();
			const cs::Vec3 dir = cs::ShotDirection(sh.pitch, sh.yaw, punch, spread, b.rng);
			GothicCS::FireBullets3(s, sh, dir, distance, penetration, bullet, damage, rangeMod);
		}
		void FireBuckshot(int pellets, float cone, float distance, const cs::Vec3& punch) override
		{
			GothicCS::FireBuckshot(s, Me(), pellets, cone, distance, punch);
		}
		cs::KnifeResult KnifeAttack(float, float, bool) override { return cs::KNIFE_MISS; }
		void ThrowGrenade(cs::GrenadeKind, const cs::Vec3&) override {}
		bool InBombZone() override { return false; }
		void PlantBomb() override {}
		void Message(const char*) override {}
		void Progress(float) override {}
		void Freeze(bool) override {}
		void Retire() override {}
		void MuzzleFlash() override { b.muzzleFlashUntil = s.clock + 0.03; }
	};

	inline int FindAni(zCModel* model, std::initializer_list<const char*> names)
	{
		if (!model) return -1;
		for (const char* n : names)
		{
			const int id = model->GetAniIDFromAniName(zSTRING(n));
			if (id >= 0) return id;
		}
		return -1;
	}

	// A Gothic human script instance whose name starts with one of the prefixes
	// (T: "ORG_" New Camp rogues ...; CT: "GRD_" Old Camp guards).
	inline int FindNpcInstance(std::initializer_list<const char*> prefixes, cs::Rng& rng)
	{
		if (!parser) return -1;
		std::vector<int> found;
		const int n = parser->symtab.GetNumInList();
		for (const char* prefix : prefixes)
		{
			const size_t pl = strlen(prefix);
			for (int i = 0; i < n; i++)
			{
				zCPar_Symbol* sym = parser->GetSymbol(i);
				if (!sym || sym->type != zPAR_TYPE_INSTANCE) continue;
				if (_strnicmp(sym->name.ToChar(), prefix, pl) == 0) found.push_back(i);
			}
			if (!found.empty()) break;
		}
		if (found.empty()) return -1;
		return found[rng.Long(0, (int)found.size() - 1)];
	}

	inline void BotTakeControl(oCNpc* npc)
	{
		npc->SetMovLock(TRUE);
		if (npc->human_ai) npc->human_ai->SetPhysicsEnabled(FALSE);
		npc->SetPhysicsEnabled(FALSE);
		npc->SetCollDetStat(FALSE);
		npc->SetCollDetDyn(FALSE);
		npc->ClearEM();
		npc->SetSleeping(TRUE);   // no Gothic AI / perception; we animate it ourselves
	}

	inline void BotPlace(Bot& b)
	{
		b.npc->SetPositionWorld(ToGothic(b.pm.Feet()) + zVEC3(0, b.hipHeight, 0));
		cs::Vec3 f;
		cs::AngleVectors(0, b.yaw, &f, nullptr, nullptr);
		b.npc->SetHeadingAtWorld(DirToGothic(f));
	}

	inline Bot* SpawnBotAt(State& s, const cs::Vec3& spot, float yaw)
	{
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world || !player) return nullptr;
		// the enemy team: player T -> bot CT (Old Camp guard), player CT -> bot T (New Camp)
		const int inst = s.inv.ct ? FindNpcInstance({ "ORG_", "SLD_", "BAU_" }, s.rng) : FindNpcInstance({ "GRD_", "VLK_" }, s.rng);
		if (inst < 0) { Toast("No NPC instance available for this bot"); return nullptr; }

		oCNpc* npc = static_cast<oCNpc*>(world->CreateVob(zVOB_TYPE_NSC, inst));
		if (!npc) { Toast("Could not create NPC"); return nullptr; }
		ogame->GetSpawnManager()->SpawnNpc(npc, ToGothic(spot) + zVEC3(0, 100, 0), 0.0f);

		Bot* b = new Bot();
		b->npc = npc;
		b->name = npc->GetName(0).ToChar();
		b->rng.s = s.rng.Next() | 1;
		b->hipHeight = s.hipHeight;
		b->pm.Spawn(s.world, spot);
		b->yaw = yaw;
		b->guardPost = spot;
		const cs::WeaponId wid = s.inv.ct ? cs::W_AK47 : cs::W_M4A1;
		b->weapon.id = wid;
		b->weapon.clip = b->weapon.I().maxClip;
		b->ammo[b->weapon.I().ammo] = cs::Ammo(b->weapon.I().ammo).maxCarry;
		BotHost host(s, *b);
		b->weapon.Deploy((float)s.clock, host);
		b->pm.maxspeed = b->weapon.I().maxSpeed;
		BotTakeControl(npc);
		BotPlace(*b);

		zCModel* model = npc->GetModel();
		b->aniIdle = FindAni(model, { "S_CBOWAIM", "S_CBOWRUN", "S_BOWAIM", "S_RUN" });
		b->aniRun = FindAni(model, { "S_CBOWRUNL", "S_BOWRUNL", "S_RUNL" });

		b->gun = MakeModelVob((std::string("CSP_") + b->weapon.I().model + ".3DS").c_str());
		Bots().push_back(b);
		char msg[128];
		sprintf_s(msg, "Bot %s: %s (%s)", s.inv.ct ? "T" : "CT", b->name.c_str(), b->weapon.I().name);
		Toast(msg);
		return b;
	}

	inline void RecordBotSpawn(const cs::Vec3& pos, float yaw);

	// K: a bot 8 m in front of the player; it respawns there every round.
	inline Bot* SpawnBot(State& s)
	{
		cs::Vec3 f;
		cs::AngleVectors(0, s.yaw, &f, nullptr, nullptr);
		cs::Vec3 spot = s.pm.Feet() + f * (800.0f / kCmPerUnit);
		bool hit = false;
		const zVEC3 ground = GroundBelow(ToGothic(spot) + zVEC3(0, 150, 0), hit);
		if (hit) spot = ToCS(ground);
		Bot* b = SpawnBotAt(s, spot, s.yaw + 180.0f);
		if (b) RecordBotSpawn(spot, s.yaw + 180.0f);
		return b;
	}

	inline void RemoveAllBots()
	{
		for (Bot* b : Bots())
		{
			RemoveModelVob(b->gun);
			if (b->npc && !b->dead && ogame && ogame->GetGameWorld())
			{
				b->npc->SetSleeping(FALSE);
				ogame->GetSpawnManager()->DeleteNpc(b->npc);   // like Gothic's own removal (spawn list + world)
			}
			delete b;
		}
		Bots().clear();
	}

	inline void BotDie(State& s, Bot& b, const char* weaponName, bool headshot)
	{
		b.dead = true;
		RemoveModelVob(b.gun);
		// back to Gothic, which kills it with its own death animation
		b.npc->SetSleeping(FALSE);
		b.npc->SetMovLock(FALSE);
		b.npc->OnDamage(player, player, float(b.npc->GetAttribute(NPC_ATR_HITPOINTSMAX) + 10000), oEDamageType_Point,
			b.npc->GetPositionWorld());
		static const char* kDie[] = { "player/die1.wav", "player/die2.wav", "player/die3.wav" };
		PlaySfx3D(SfxName(kDie[s.rng.Long(0, 2)]), b.npc);
		OnKill(s, b.name.c_str(), weaponName, headshot);
	}

	inline void DamageBot(State& s, Bot& b, float damage, cs::HitGroup group, float armorRatio, const char* weaponName, bool blast)
	{
		if (b.dead) return;
		cs::ApplyCsDamage(b.health, b.armor, b.helmet, damage, group, armorRatio, blast);
		sprintf_s(s.lastShot, "bot %s [%s] dmg %.1f -> HP %d armor %d", b.name.c_str(), cs::HitGroupName(group), damage, b.health, b.armor);
		if (group == cs::HitGroup::Head)
			PlaySfx3D(SfxName(b.helmet && b.armor > 0 ? "player/bhit_helmet-1.wav" : "player/headshot1.wav"), b.npc);
		else
			PlaySfx3D(SfxName(b.armor > 0 && group != cs::HitGroup::Leg ? "player/bhit_kevlar-1.wav" : "player/bhit_flesh-1.wav"), b.npc);
		if (b.health <= 0) BotDie(s, b, weaponName, group == cs::HitGroup::Head);
	}

	// Explosions (HE / C4): RadiusDamage on the player, bots and Gothic NPCs.
	inline void RadiusDamageAll(State& s, const cs::Vec3& src, float damage, float radius, const char* weapon, bool bomb)
	{
		if (s.alive)
		{
			const float d = cs::RadiusDamageAt(damage, radius, cs::Length(s.pm.origin - src));
			if (d > 0) DamagePlayer(s, d, cs::HitGroup::Generic, 1.0f, bomb ? "C4" : "Nameless Hero", weapon, true);
		}
		for (Bot* b : Bots())
		{
			if (b->dead) continue;
			const float d = cs::RadiusDamageAt(damage, radius, cs::Length(b->pm.origin - src));
			if (d > 0) DamageBot(s, *b, d, cs::HitGroup::Generic, 1.0f, weapon, true);
		}
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world || !world->voblist_npcs) return;
		std::vector<oCNpc*> victims;
		for (zCListSort<oCNpc>* n = world->voblist_npcs->next; n; n = n->next)
		{
			oCNpc* npc = n->data;
			if (!npc || npc == player || npc->IsDead() || BotOf(npc)) continue;
			const cs::Vec3 c = ToCS((npc->bbox3D.mins + npc->bbox3D.maxs) * 0.5f);
			if (cs::Length(c - src) < radius) victims.push_back(npc);
		}
		for (oCNpc* npc : victims)
		{
			const cs::Vec3 c = ToCS((npc->bbox3D.mins + npc->bbox3D.maxs) * 0.5f);
			const float d = cs::RadiusDamageAt(damage, radius, cs::Length(c - src));
			if (d > 0) DamageNpc(s, npc, d, cs::HitGroup::Generic, npc->GetPositionWorld(), weapon);
		}
	}

	// RadiusFlash on bots: blinded bots stop seeing for the blind duration (fadeTime * 0.33 + hold).
	inline void BlindBots(State& s, const cs::Vec3& src)
	{
		for (Bot* b : Bots())
		{
			if (b->dead) continue;
			const cs::Vec3 eye = b->pm.Eye();
			const float dist = cs::Length(eye - src);
			if (dist > 1500.0f || !LineOfSight(ToGothic(src), ToGothic(eye), b->npc)) continue;
			const float adj = (std::max)(0.0f, 4.0f - dist * (4.0f / 1500.0f));
			cs::Vec3 f;
			cs::AngleVectors(b->pitch, b->yaw, &f, nullptr, nullptr);
			const bool facing = cs::Dot(src - eye, f) >= 0;
			const float fade = facing ? adj * 3.0f : adj * 1.75f, hold = facing ? adj / 1.5f : adj / 3.5f;
			b->blindUntil = s.clock + hold + fade * 0.33f;
		}
	}

	inline float AngleDiff(float a, float b) { return std::fmod(a - b + 540.0f, 360.0f) - 180.0f; }

	inline void BotThink(State& s, Bot& b, float dt)
	{
		const float now = (float)s.clock;
		BotHost host(s, b);
		const cs::Vec3 eye = b.pm.Eye();
		const cs::Vec3 target = s.pm.origin + cs::Vec3(0, 0, 10);   // the player's chest
		const bool blind = s.clock < b.blindUntil;

		if (now >= b.losCheckAt)
		{
			b.losCheckAt = now + 0.15f;
			const bool wasSeen = b.seen;
			b.seen = !blind && s.alive && cs::Length(target - eye) < 4000.0f && !SmokeBlocks(s, eye, target) &&
				LineOfSight(ToGothic(eye), ToGothic(target), b.npc);
			if (b.seen) { b.lastSeen = target; b.everSeen = true; b.lastSeenAt = s.clock;
				if (!wasSeen) b.reactAt = s.clock + b.rng.Float(0.45f, 0.8f); }
		}
		if (blind || !s.alive) b.seen = false;
		if (!b.seen && s.clock - b.lastSeenAt > 5.0) b.everSeen = false;
		const bool bombDuty = !s.inv.ct && Bomb().planted;
		const cs::Vec3 destination = bombDuty ? Bomb().pos : (b.everSeen ? b.lastSeen : b.guardPost);
		cs::MoveInput in;
		const cs::Vec3 aimAt = b.seen ? target : destination + cs::Vec3(0, 0, 36);
		const cs::Vec3 to = aimAt - eye;
		float dy = 0;
		if (b.seen || cs::Length2D(destination - b.pm.Feet()) > 48.0f)
		{
			const float wantYaw = std::atan2(to.y, to.x) / cs::kDegToRad;
			const float wantPitch = -std::atan2(to.z, cs::Length2D(to)) / cs::kDegToRad;
			const float turn = 540.0f * dt;
			dy = AngleDiff(wantYaw, b.yaw);
			b.yaw += (std::max)(-turn, (std::min)(turn, dy));
			b.pitch += (std::max)(-turn, (std::min)(turn, wantPitch - b.pitch));
		}
		if (now >= b.aimJitterAt)
		{
			b.aimJitterAt = now + 0.5f;
			b.aimErrYaw = b.rng.Float(-1.5f, 1.5f);
			b.aimErrPitch = b.rng.Float(-1.0f, 1.0f);
		}

		bool attack = false;
		if (b.seen && !blind)
		{
			if (now >= b.strafeUntil)
			{
				b.strafeUntil = now + b.rng.Float(0.6f, 1.4f);
				b.strafe = (float)b.rng.Long(-1, 1);
			}
			in.side = b.strafe;
			in.forward = cs::Length(to) > 900.0f ? 1.0f : 0.0f;
			if (std::fabs(dy) < 4.0f && now >= b.pauseUntil && s.clock >= b.reactAt)
			{
				if (b.burstUntil <= 0) b.burstUntil = now + b.rng.Float(0.25f, 0.45f);
				attack = now < b.burstUntil;
				if (!attack) { b.burstUntil = 0; b.pauseUntil = now + b.rng.Float(0.3f, 0.7f); }
			}
		}
		else if (cs::Length2D(destination - b.pm.Feet()) > 48.0f)
			in.forward = 1.0f;
		// A continuous, exposed ten-second defuse; combat or displacement cancels it.
		const bool canDefuse = bombDuty && !b.seen && !blind && b.pm.onGround &&
			cs::Length(b.pm.Feet() - Bomb().pos) <= 64.0f &&
			LineOfSight(ToGothic(eye), ToGothic(Bomb().pos + cs::Vec3(0, 0, 8)), b.npc);
		if (canDefuse) {
			in.forward = in.side = 0;
			if (b.defuseStarted < 0) {
				b.defuseStarted = s.clock;
				PlaySfxAt(SfxName("weapons/c4_disarm.wav"), ToGothic(Bomb().pos));
				Toast("A guard is defusing the bomb!");
			}
			if (s.clock < Bomb().blowTime && s.clock - b.defuseStarted >= 10.0) DefuseEnd(s, true);
		} else b.defuseStarted = -1;
		in.yaw = b.yaw;
		in.pitch = b.pitch;

		// move only inside the collision window around the player
		if ((ToGothic(b.pm.Feet()) - s.worldCenter).Length() < kWorldHalfCm - 300.0f)
			b.pm.Step(s.world, in, dt);

		// weapon at 100 Hz, CS timers
		b.weapon.Tick(dt);
		b.nextAttack = (std::max)(b.nextAttack - dt, -0.001f);
		cs::DropPunchAngle(b.punch, dt);
		b.weapon.PostFrame(now, attack, false, false, host);
	}

	// Per rendered frame: place the NPC, animate it, put the gun in its hand.
	inline void BotPose(Bot& b)
	{
		BotPlace(b);
		zCModel* model = b.npc->GetModel();
		if (!model) return;
		const int want = cs::Length2D(b.pm.velocity) > 20.0f && b.aniRun >= 0 ? b.aniRun : b.aniIdle;
		if (want >= 0 && want != b.aniCurrent)
		{
			model->StartAni(want, 0);
			b.aniCurrent = want;
		}
		model->AdvanceAnis();
		model->vobTrans = zVEC3(0, 0, 0);   // root motion: we move the NPC ourselves
		if (b.gun)
		{
			if (zCModelNodeInst* hand = model->SearchNode(zSTRING("BIP01 R HAND")))
			{
				zMAT4 world = b.npc->trafoObjToWorld;
				b.gun->SetTrafoObjToWorld(world * model->GetTrafoNodeToModel(hand));
			}
		}
	}

	inline bool FreezePeriod();
	inline bool BotsRoundLive();

	inline void BotsStep(State& s, float dt)
	{
		if (!BotsRoundLive()) return;   // nobody moves or shoots during freeze time
		for (Bot* b : Bots())
			if (!b->dead && BotInWorld(*b)) BotThink(s, *b, dt);
	}

	inline void BotsFrame(State& s, const ViewCamera& cam)
	{
		for (Bot* b : Bots())
		{
			if (b->dead || !b->npc || !BotInWorld(*b)) continue;
			BotPose(*b);
			// muzzle flash at the gun (p_ model "flash" bone is not exported; muzzle ~ 25 units ahead of the eye line)
			if (s.clock < b->muzzleFlashUntil && cam.ok)
			{
				cs::Vec3 f;
				cs::AngleVectors(b->pitch, b->yaw, &f, nullptr, nullptr);
				WorldSprite& w = AddWorldSprite("muzzleflash1", b->pm.Eye() + f * 30.0f - cs::Vec3(0, 0, 8), (float)s.clock, 0.03f, 0.3f, 0.0f);
				w.roll = b->rng.Float(0, 360);
				b->muzzleFlashUntil = 0;
			}
		}
	}
}
