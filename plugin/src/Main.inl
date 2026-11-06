// GothicCS - CS mode: enter / leave, input, the 100 Hz step (movement,
// weapons, grenades, bomb, bots), camera, drops / pickups, and the two
// render hooks. Included last inside GOTHIC_NAMESPACE.

namespace GothicCS
{
	inline void ApplyMoveCvars(State& s);
	inline void SetGod(State& s, bool on);

	// Gothic's spawn manager inserts/removes NPCs around the CAMERA every SpawnTime
	// seconds; at noclip / bhop speeds NPCs then pop in and out. While in CS mode use
	// larger ranges and more frequent checks; restore Gothic's values on leave.
	// Gothic's own "insert every NPC in range now" (used after loading a game):
	// after flying far with noclip the spawn manager otherwise waits for its timer.
	inline void InsertWaitingInRange(oCSpawnManager* sm);

	inline void RespawnNpcsNow()
	{
		oCSpawnManager* sm = ogame ? ogame->GetSpawnManager() : nullptr;
		if (!sm) return;
		sm->SetSpawningEnabled(TRUE);
		sm->InitCameraPos();          // camPos = current camera (may be stale after noclip)
		sm->SpawnImmediately(TRUE);
		InsertWaitingInRange(sm);
	}

	// Insert every waiting NPC whose spawn point is within Gothic's insert range,
	// also when it is in view (Gothic itself only inserts out of view).
	inline void InsertWaitingInRange(oCSpawnManager* sm)
	{
		// fallback: insert every waiting NPC whose spawn point is within Gothic's insert range
		zCVob* camVob = ogame->GetCameraVob();
		if (!camVob) return;
		const zVEC3 cam = camVob->GetPositionWorld();
		const float range = oCSpawnManager::GetInsertRange();
		for (int i = sm->spawnList.GetNumInList() - 1; i >= 0; i--)
		{
			oCSpawnManager::oSSpawnNode* node = sm->spawnList[i];
			if (!node || !node->npc) continue;
			if ((node->spawnPos - cam).Length() > range) continue;
			if (sm->InsertNpc(node->npc, node->spawnPos)) sm->RemoveFromList(node);
		}
	}

	// Gothic's own periodic insert check, driven by us 4x per second in CS mode
	// (in CS mode Gothic's spawn manager otherwise left all NPCs waiting).
	inline void SpawnTick(State& s)
	{
		oCSpawnManager* sm = ogame ? ogame->GetSpawnManager() : nullptr;
		if (!sm || !ogame->GetCameraVob()) return;
		sm->InitCameraPos();          // every step: removal checks use the real camera
		static double next = 0;
		if (next - s.clock > 1.0) next = s.clock;   // clock restarted
		if (s.clock < next) return;
		next = s.clock + 0.25;        // noclip covers ~30 m per second
		{
			sm->CheckInsertNpcs();
			if (Cfg().spawnInView) InsertWaitingInRange(sm);
		}
	}

	inline void SpawnRanges(bool csMode)
	{
		static bool saved = false;
		static float ins = 0, rem = 0, time = 0;
		const bool change = Cfg().spawnRangeScale > 1.0f || Cfg().spawnCheckTime > 0.05f;
		if (csMode && !saved && change)
		{
			ins = oCSpawnManager::GetInsertRange(); rem = oCSpawnManager::GetRemoveRange(); time = oCSpawnManager::GetSpawnTime();
			saved = true;
			const float k = Cfg().spawnRangeScale > 1.0f ? Cfg().spawnRangeScale : 1.0f;
			oCSpawnManager::SetInsertRange(ins * k);
			oCSpawnManager::SetRemoveRange(rem * k);
			if (Cfg().spawnCheckTime > 0.05f) oCSpawnManager::SetSpawnTime(Cfg().spawnCheckTime);
		}
		else if (!csMode && saved)
		{
			oCSpawnManager::SetInsertRange(ins); oCSpawnManager::SetRemoveRange(rem); oCSpawnManager::SetSpawnTime(time);
			saved = false;
		}
	}


	// ---- dropped weapons (G, buying a new gun) -----------------------------------
	struct DroppedWeapon { cs::Weapon w; zCVob* vob = nullptr; cs::Vec3 pos; double pickupAfter = 0; float yaw = 0; };
	inline std::vector<DroppedWeapon>& Dropped() { static std::vector<DroppedWeapon> d; return d; }

	inline void DropWeapon(State& s, const cs::Weapon& w)
	{
		if (!w.Valid()) return;
		DroppedWeapon d;
		d.w = w;
		cs::Vec3 f;
		cs::AngleVectors(0, s.yaw, &f, nullptr, nullptr);
		cs::Vec3 p = s.pm.Feet() + f * 40.0f + cs::Vec3(0, 0, 20);
		cs::TraceResult tr = s.world.TraceBox(p, p - cs::Vec3(0, 0, 120), cs::Vec3(), cs::Vec3());
		if (tr.fraction < 1.0f) p = tr.endpos + cs::Vec3(0, 0, 1);
		d.pos = p;
		d.yaw = s.yaw + 90.0f;
		d.pickupAfter = s.clock + 1.0;
		d.vob = MakeModelVob(w.id == cs::W_C4 ? "CSW_BACKPACK.3DS" : (std::string("CSW_") + w.I().model + ".3DS").c_str());
		PlaceModelVob(d.vob, d.pos, d.yaw);
		Dropped().push_back(d);
	}

	inline void DropActive(State& s)
	{
		cs::Weapon* w = s.inv.active;
		if (!w || w->id == cs::W_KNIFE || cs::IsGrenade(w->id)) return;   // CS: knife and grenades cannot be dropped
		DropWeapon(s, *w);
		if (w->id == cs::W_C4) w->count = 0;
		else *w = cs::Weapon();
		s.inv.active = nullptr;
		SelectWeapon(s, s.inv.Best(nullptr));
	}

	inline void PickupsStep(State& s)
	{
		auto& list = Dropped();
		for (DroppedWeapon& d : list)
		{
			if (!d.vob || s.clock < d.pickupAfter || !s.alive) continue;
			if (cs::Length(d.pos - s.pm.Feet()) > 48.0f) continue;
			if (d.w.id == cs::W_C4)
			{
				if (s.inv.ct || s.inv.c4.count) continue;
				s.inv.c4.count = 1;
				Toast("You picked up the bomb!");
			}
			else
			{
				cs::Weapon& slot = d.w.I().slot == cs::SLOT_PRIMARY ? s.inv.primary : s.inv.pistol;
				if (slot.Valid())
				{
					if (slot.id != d.w.id) continue;
					// same gun: its rounds become reserve ammo
					int& r = s.inv.ammo[d.w.I().ammo];
					r = (std::min)(cs::Ammo(d.w.I().ammo).maxCarry, r + d.w.clip);
				}
				else slot = d.w;
			}
			PlaySfx(SfxName("items/gunpickup2.wav"));
			RemoveModelVob(d.vob);
		}
		list.erase(std::remove_if(list.begin(), list.end(), [](const DroppedWeapon& d) { return d.vob == nullptr; }), list.end());
	}

	// ---- taking over the hero -------------------------------------------------------
	inline void TakeControl(State& s, bool on)
	{
		oCNpc* npc = on ? player : s.hero;
		if (!npc) return;
		npc->SetMovLock(on ? TRUE : FALSE);
		if (npc->human_ai) npc->human_ai->SetPhysicsEnabled(on ? FALSE : TRUE);
		npc->SetPhysicsEnabled(on ? FALSE : TRUE);
		if (on)
		{
			s.savedStat = npc->collDetectionStatic != 0;
			s.savedDyn = npc->collDetectionDynamic != 0;
			s.savedShowVisual = npc->showVisual != 0;
			npc->SetCollDetStat(FALSE);
			npc->SetCollDetDyn(FALSE);
			npc->showVisual = FALSE;   // first person: the hero's body would block the camera
		}
		else
		{
			npc->SetCollDetStat(s.savedStat ? TRUE : FALSE);
			npc->SetCollDetDyn(s.savedDyn ? TRUE : FALSE);
			npc->showVisual = s.savedShowVisual ? TRUE : FALSE;
		}
	}

	inline void Respawn(State& s, bool keepEquipment)
	{
		s.alive = true;
		if (keepEquipment) { s.inv.health = 100; s.inv.active = nullptr; }
		else s.inv.DefaultLoadout();
		if (!s.inv.ct && !s.inv.c4.count) s.inv.c4.count = 1;
		if (s.inv.ct) s.inv.c4.count = 0;
		s.pm = cs::PlayerMove{};
		s.pm.vars.airaccelerate = Cfg().airAccelerate;
		ApplyMoveCvars(s);
		s.pm.vars.maxvelocity = Cfg().maxVelocity;
		RebuildWorld(s, ToGothic(s.spawnFeet));
		s.pm.Spawn(s.world, s.spawnFeet);
		s.yaw = s.spawnYaw;
		s.pitch = 0;
		s.punch = cs::Vec3();
		s.fov = 90;
		s.flashStart = 0;
		s.prevEye = s.curEye = s.pm.Eye();
		cs::Weapon* w = keepEquipment && s.inv.primary.Valid() ? &s.inv.primary : &s.inv.pistol;
		s.inv.active = nullptr;
		Host().playerNextAttack = 0;
		SelectWeapon(s, w);
	}

	inline void Enter()
	{
		State& s = S();
		if (!player || player->IsDead()) return;
		s.hero = player;
		s.enterWorld = ogame->GetGameWorld();
		if (Cfg().godMode) SetGod(s, true);
		SpawnRanges(true);
		const zVEC3 heroPos = player->GetPositionWorld();
		bool hit = false;
		const zVEC3 feet = GroundBelow(heroPos + zVEC3(0, 50, 0), hit);
		s.hipHeight = hit ? heroPos[1] - feet[1] : 90.0f;

		RebuildWorld(s, heroPos);
		const cs::Vec3 feetCS = ToCS(hit ? feet : heroPos - zVEC3(0, 90, 0));
		DumpObj(s.world, "cs_world.obj", feetCS);
		LoadBombsites(s);

		TakeControl(s, true);
		s.pm = cs::PlayerMove{};
		s.pm.vars.airaccelerate = Cfg().airAccelerate;
		ApplyMoveCvars(s);
		s.pm.vars.maxvelocity = Cfg().maxVelocity;
		s.pm.Spawn(s.world, feetCS);
		const zVEC3 at = player->GetAtVectorWorld();
		s.yaw = std::atan2(at[2], at[0]) / cs::kDegToRad;   // Gothic z = CS y
		s.pitch = 0;
		s.spawnFeet = feetCS;
		s.spawnYaw = s.yaw;
		s.prevEye = s.curEye = s.pm.Eye();
		s.accumulator = 0;
		QueryPerformanceCounter(&s.lastTime);
		float dx, dy, dz;
		zinput->GetMousePos(dx, dy, dz);   // drop the mouse movement collected so far

		if (!s.everEntered)
		{
			s.everEntered = true;
			s.inv.ct = false;              // T: carries the C4
			s.inv.money = Cfg().startMoney;
			s.inv.DefaultLoadout();
			s.inv.c4.count = 1;
		}
		s.alive = s.inv.health > 0;
		if (!s.alive) { s.inv.health = 100; s.alive = true; }
		s.punch = cs::Vec3();
		s.fov = 90;
		s.frozen = false;
		s.buyMenu = 0;
		cs::Weapon* w = s.inv.active ? s.inv.active : &s.inv.pistol;
		s.inv.active = nullptr;
		Host().playerNextAttack = 0;
		SelectWeapon(s, w);

		// a new match: first round with freeze time
		Round() = RoundState();
		Round().phase = RP_FREEZE;
		Round().phaseStart = s.clock;
		s.pendingAward = 0;
		SetupSiteGuards(s);

		s.active = true;
		Toast(s.bombsites.empty() ? "CS MODE - B buy, END bombsite A/B, K spawn bot" : "CS MODE (INSERT - exit, B - buy)");
	}

	inline void RestoreCamera(State& s)
	{
		zCCamera* cam = ogame ? ogame->GetCamera() : nullptr;
		if (cam && s.savedFovH > 0) cam->SetFOV(s.savedFovH, s.savedFovV);
		s.savedFovH = s.savedFovV = 0;
	}

	inline void Leave()
	{
		State& s = S();
		if (!s.active) return;
		s.active = false;
		s.buyMenu = 0;
		RestoreCamera(s);
		// after a savegame load / level change the old world and its vobs are gone:
		// then only forget them, never touch the pointers
		const bool worldAlive = s.hero == player && ogame && ogame->GetGameWorld() == s.enterWorld;
		if (worldAlive) { SetGod(s, false); TakeControl(s, false); }
		SpawnRanges(false);
		if (worldAlive) RespawnNpcsNow();
		s.hero = nullptr;
		if (worldAlive)
		{
			RemoveAllBots();
			RemoveAllGrenades();
			RemoveBomb();
			for (DroppedWeapon& d : Dropped()) RemoveModelVob(d.vob);
		}
		else
		{
			for (Bot* b : Bots()) delete b;
			Bots().clear();
			Grenades().clear();
			SmokeClouds().clear();
			Bomb() = PlantedBomb();
		}
		Dropped().clear();
		WorldSprites().clear();
		Toast("CS mode disabled");
	}

	// ---- camera ----------------------------------------------------------------------
	// CS fov is horizontal at 4:3 (90 -> vertical 73.74); Hor+ for wider screens.
	inline float VerticalFov(int fov)
	{
		return 2.0f * std::atan(std::tan(fov * 0.5f * cs::kDegToRad) * 0.75f) / cs::kDegToRad;
	}

	inline void ApplyCamera(State& s, const cs::Vec3& eye)
	{
		zCVob* camVob = ogame->GetCameraVob();
		zCCamera* cam = ogame->GetCamera();
		if (!camVob || !cam) return;
		cs::Vec3 f, r, u;
		cs::AngleVectors(s.pitch + s.punch.x, s.yaw + s.punch.y, &f, &r, &u);
		const zVEC3 at = DirToGothic(f), right = DirToGothic(r), up = DirToGothic(u), pos = ToGothic(eye);
		zMAT4 m;
		m.v[0] = zVEC4(right[0], up[0], at[0], pos[0]);
		m.v[1] = zVEC4(right[1], up[1], at[1], pos[1]);
		m.v[2] = zVEC4(right[2], up[2], at[2], pos[2]);
		m.v[3] = zVEC4(0, 0, 0, 1);
		camVob->SetTrafoObjToWorld(m);

		if (s.savedFovH <= 0) cam->GetFOV(s.savedFovH, s.savedFovV);
		float aspect = 4.0f / 3.0f;
		if (zrenderer && zrenderer->vid_xdim > 0 && zrenderer->vid_ydim > 0)
			aspect = float(zrenderer->vid_xdim) / float(zrenderer->vid_ydim);
		const float v = VerticalFov(s.fov);
		const float h = 2.0f * std::atan(std::tan(v * 0.5f * cs::kDegToRad) * aspect) / cs::kDegToRad;
		cam->SetFOV(h, v);
	}

	inline ViewCamera CurrentViewCamera(State& s)
	{
		ViewCamera c;
		const float alpha = float(s.accumulator / kStep);
		c.eye = s.prevEye + (s.curEye - s.prevEye) * alpha;
		cs::AngleVectors(s.pitch + s.punch.x, s.yaw + s.punch.y, &c.f, &c.r, &c.u);
		c.ok = c.proj.Init(VerticalFov(s.fov));
		return c;
	}

	inline void Print(int line, const char* text)
	{
		if (screen && S().showDebug) screen->Print(100, 200 + line * 200, zSTRING(text));
	}

	// ---- footsteps (CS: only when running faster than 150 u/s) -----------------------
	inline void FootstepStep(State& s)
	{
		if (s.pm.onLadder)
		{
			if (cs::Length(s.pm.velocity) > 0 && s.clock >= s.nextStepSound)
			{
				s.nextStepSound = s.clock + 0.35;
				char p[48];
				sprintf_s(p, "player/pl_ladder%d.wav", s.rng.Long(1, 4));
				PlaySfx(SfxName(p));
			}
			return;
		}
		const float speed = cs::Length2D(s.pm.velocity);
		if (!s.pm.onGround || speed <= 150.0f || s.clock < s.nextStepSound) return;
		s.nextStepSound = s.clock + (speed >= 220.0f ? 0.3 : 0.4);
		const char* base = "player/pl_step";
		int n = 4;
		switch (s.pm.groundSurface)
		{
		case zMAT_GROUP_EARTH: base = "player/pl_dirt"; break;
		case zMAT_GROUP_METAL: base = "player/pl_metal"; break;
		case zMAT_GROUP_WATER: base = "player/pl_slosh"; n = 3; break;
		case zMAT_GROUP_WOOD: base = "player/pl_tile"; break;
		default: break;
		}
		char path[64];
		sprintf_s(path, "%s%d.wav", base, s.rng.Long(1, n));
		PlaySfx(SfxName(path));
	}

	// ---- one 100 Hz step --------------------------------------------------------------
	inline bool FreezePeriod();
	inline void RoundStep(State& s);

	// CS server cvars from GothicCS.ini (stock CS 1.6 defaults) + deathrun-style bhop.
	inline void ApplyMoveCvars(State& s)
	{
		cs::MoveVars& v = s.pm.vars;
		const Config& c = Cfg();
		v.gravity = c.gravity; v.accelerate = c.accelerate; v.airaccelerate = c.airAccelerate;
		v.friction = c.friction; v.stopspeed = c.stopspeed; v.maxspeed = c.maxspeedCvar;
		v.maxvelocity = c.maxVelocity; v.edgefriction = c.edgefriction; v.stepsize = c.stepsize;
		v.autoBunnyhop = c.autoBunnyhop; v.noJumpSlowdown = c.autoBunnyhop && c.bhopNoSlowdown;
	}

	// God mode (CS "god"): no CS damage, and Gothic's own immortality flag on the hero.
	inline void SetGod(State& s, bool on)
	{
		static bool weSetImmortal = false;
		s.god = on;
		const bool has = (player->variousFlags & NPC_FLAG_IMMORTAL) != 0;
		if (on && !has) { player->variousFlags |= NPC_FLAG_IMMORTAL; weSetImmortal = true; }
		if (!on && weSetImmortal) { player->variousFlags &= ~NPC_FLAG_IMMORTAL; weSetImmortal = false; }
	}

	inline void Step(State& s, const cs::MoveInput& inBase, bool attack, bool attack2, bool reload, bool use)
	{
		s.prevEye = s.pm.Eye();
		cs::MoveInput in = inBase;
		if (!s.alive) in = cs::MoveInput{};
		const bool frozen = s.frozen || FreezePeriod();
		s.pm.maxspeed = frozen ? 1.0f : MaxSpeedOf(s);
		if (frozen) { in.forward = in.side = 0; in.jump = false; }
		if (FreezePeriod()) attack = attack2 = false;   // no shooting during freeze time
		in.yaw = s.yaw; in.pitch = s.pitch;
		if (s.noclip) s.pm.NoClipMove(in, kStep);
		else s.pm.Step(s.world, in, kStep);
		s.curEye = s.pm.Eye();
		s.clock += kStep;

		// fall damage: (fallVelocity - 500) * 100 / 600 * 1.25
		if (s.pm.justLanded && s.alive)
		{
			if (s.pm.fallVelocity > 500.0f && !s.god)
			{
				const float dmg = (s.pm.fallVelocity - 500.0f) * 0.208333f;
				s.inv.health -= (int)dmg;
				static const char* kPain[] = { "player/pl_fallpain1.wav", "player/pl_fallpain2.wav", "player/pl_fallpain3.wav" };
				PlaySfx(SfxName(kPain[s.rng.Long(0, 2)]));
				if (s.inv.health <= 0) { s.inv.health = 0; PlayerDie(s, "World", "fall", false); }
			}
			if (s.pm.fallVelocity >= 250.0f) s.punch.z = s.pm.fallVelocity * 0.013f;
		}
		if (s.alive) FootstepStep(s);

		// weapon (CS timers, ItemPostFrame)
		cs::DropPunchAngle(s.punch, kStep);
		PlayerHost& host = Host();
		host.playerNextAttack = (std::max)(host.playerNextAttack - kStep, -0.001f);
		if (s.inv.active)
		{
			s.inv.active->Tick(kStep);
			if (s.alive) s.inv.active->PostFrame((float)s.clock, attack, attack2, reload, host);
		}
		// viewmodel animation and its studio events
		if (s.vm && s.vm->loaded)
		{
			std::vector<const cs::VmEvent*> fired;
			s.vm->anim.Advance(s.vm->data, kStep, fired);
			for (const cs::VmEvent* e : fired)
			{
				if (e->event == 5004) PlaySfx(e->options);
				else if (e->event == 5001)
				{
					const int type = atoi(e->options.c_str());
					s.muzzleSprite = type % 10;
					s.muzzleScale = (type / 10) * 0.1f;
					if (s.muzzleScale == 0) s.muzzleScale = 0.5f;
					s.muzzleFlashUntil = s.clock + 0.02;
					s.muzzleFrame = s.rng.Long(0, 9);
					s.muzzleRoll = (float)s.rng.Long(0, 359);
				}
			}
		}
		BombUse(s, use);
		BombStep(s);
		GrenadesStep(s, kStep);
		BotsStep(s, kStep);
		PickupsStep(s);
		RoundStep(s);
		SpawnTick(s);

		// without bots there is no round result to wait for: respawn after 3 s
		if (!s.alive && !RoundHasBots() && s.clock - s.deathTime > 3.0) Respawn(s, false);
	}

	inline int DigitPressed()
	{
		for (int k = 0; k <= 9; k++)
			if (PressedOnce('0' + k)) return k;
		return -1;
	}


	inline void Loop()
	{
		if (!ogame || !player || !zinput) return;
		State& s = S();

		if (PressedOnce(VK_INSERT)) s.active ? Leave() : Enter();
		if (PressedOnce(VK_HOME)) s.showDebug = !s.showDebug;
		if (!s.active)
		{
			if (s.toast[0] && GetTickCount() < s.toastUntil && screen) screen->PrintCXY(zSTRING(s.toast));
			return;
		}
		// the game took the hero away (death, savegame load, level change)
		if (player != s.hero || player->IsDead()) { Leave(); return; }

		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		double dt = double(now.QuadPart - s.lastTime.QuadPart) / double(freq.QuadPart);
		s.lastTime = now;
		s.frameMs = float(dt * 1000.0);
		if (dt > 0.1) dt = 0.1;

		// mouse look: CS m_yaw / m_pitch = 0.022 degrees per count; zoom scales it like CS (fov / 90)
		float dx = 0, dy = 0, dz = 0;
		zinput->GetMousePos(dx, dy, dz);
		s.lastMouseDx = dx; s.lastMouseDy = dy;
		if (dz != 0) s.wheelJump = true;   // CS: bind mwheelup / mwheeldown +jump
		const float k = 0.022f * Cfg().sensitivity * Cfg().mouseScale * (s.fov / 90.0f);
		s.yaw -= dx * k;
		s.pitch += dy * k;
		if (s.pitch > 89.0f) s.pitch = 89.0f;
		if (s.pitch < -89.0f) s.pitch = -89.0f;
		s.yaw = std::fmod(s.yaw, 360.0f);

		// keys
		if (PressedOnce('B'))
		{
			const char* why = nullptr;
			if (s.buyMenu) s.buyMenu = 0;
			else if (CanBuy(s, &why)) s.buyMenu = 9;
			else Toast(why);
		}
		if (s.buyMenu) { const char* why = nullptr; if (!CanBuy(s, &why)) s.buyMenu = 0; }
		// CS default binds: "," buyammo1 (primary), "." buyammo2 (secondary): one clip each
		for (int k = 0; k < 2; k++)
		{
			if (!PressedOnce(k == 0 ? VK_OEM_COMMA : VK_OEM_PERIOD)) continue;
			const char* why = nullptr;
			if (!CanBuy(s, &why)) { Toast(why); continue; }
			const char* msg = nullptr;
			if (s.inv.BuyAmmoClip(k == 0 ? s.inv.primary : s.inv.pistol, &msg)) PlaySfx(SfxName("items/9mmclip1.wav"));
			else if (msg) Toast(msg);
		}
		const int digit = DigitPressed();
		if (digit >= 0)
		{
			if (s.buyMenu) BuyMenuKeyPress(s, digit);
			else if (s.alive && !s.frozen)
			{
				if (digit == 1) SelectWeapon(s, s.inv.SlotWeapon(cs::SLOT_PRIMARY));
				else if (digit == 2) SelectWeapon(s, s.inv.SlotWeapon(cs::SLOT_PISTOL));
				else if (digit == 3) SelectWeapon(s, &s.inv.knife);
				else if (digit == 4) SelectWeapon(s, s.inv.NextGrenade());
				else if (digit == 5) SelectWeapon(s, s.inv.SlotWeapon(cs::SLOT_C4));
			}
		}
		if (PressedOnce('Q') && s.alive && !s.frozen && s.inv.last) SelectWeapon(s, s.inv.last);
		if (PressedOnce('G') && s.alive && !s.frozen) DropActive(s);
		if (PressedOnce(VK_NUMPAD8))
		{
			s.inv.AddMoney(1000);
		}
		// O remains an alias; plain Page Up toggles god mode.
		const bool pgUp = PressedOnce(VK_PRIOR), pgDn = PressedOnce(VK_NEXT);
		const bool shiftHeld = Pressed(VK_SHIFT), ctrlHeld = Pressed(VK_CONTROL);
		const bool godKey = PressedOnce('O');
		if (godKey || (pgUp && !shiftHeld && !ctrlHeld))
		{
			SetGod(s, !s.god);
			if (s.showDebug) CentrePrint(s.god ? "GODMODE ENABLED" : "GODMODE DISABLED");
		}

		// V: noclip on/off (fly through walls like CS "noclip"; off = normal CS movement)
		if (PressedOnce('V'))
		{
			s.noclip = !s.noclip;
			if (!s.noclip) RespawnNpcsNow();   // velocity is kept when leaving noclip, like CS (capped by sv_maxvelocity)
			Toast(s.noclip ? "noclip ON" : "noclip OFF");
		}

		if (PressedOnce('M'))
		{
			s.inv.ct = !s.inv.ct;
			if (s.inv.ct && s.inv.c4.count) { if (s.inv.active == &s.inv.c4) SelectWeapon(s, s.inv.Best(&s.inv.c4)); s.inv.c4.count = 0; }
			if (!s.inv.ct && !s.inv.c4.count && !Bomb().planted) s.inv.c4.count = 1;
			Toast(s.inv.ct ? "Team: Counter-Terrorists" : "Team: Terrorists (carrying C4)");
		}
		if (PressedOnce('K')) SpawnBot(s);
		if (PressedOnce(VK_END)) CycleBombsite(s);

		// Ctrl + PAGE UP / PAGE DOWN: mouse sensitivity +/- (saved to GothicCS.ini).
		// Shift + PAGE UP in debug mode: cycle the viewmodel draw mode.
		if ((pgUp && ctrlHeld && !shiftHeld) || pgDn)
		{
			float& sens = Cfg().sensitivity;
			sens += pgUp ? 0.5f : -0.5f;
			if (sens < 0.5f) sens = 0.5f;
			if (sens > 30.0f) sens = 30.0f;
			Cfg().SaveSensitivity();
			char t[64];
			sprintf_s(t, "Mouse sensitivity: %.1f", sens);
			Toast(t);
		}
		if (pgUp && shiftHeld && !ctrlHeld && s.showDebug)
		{
			for (auto& kv : s.viewmodels) kv.second->drawMode = (kv.second->drawMode + 1) & 7;
			char t[64];
			sprintf_s(t, "weapon draw mode %d", s.vm ? s.vm->drawMode : -1);
			Toast(t);
		}

		cs::MoveInput in;
		in.forward = (Pressed('W') ? 1.0f : 0.0f) - (Pressed('S') ? 1.0f : 0.0f);
		in.side = (Pressed('D') ? 1.0f : 0.0f) - (Pressed('A') ? 1.0f : 0.0f);
		in.jump = Pressed(VK_SPACE);
		in.duck = Pressed(VK_LCONTROL);
		in.walk = Pressed(VK_LSHIFT);
		const bool attack = Pressed(VK_LBUTTON) && !s.buyMenu;
		const bool attack2 = Pressed(VK_RBUTTON) && !s.buyMenu;
		const bool reload = Pressed('R');
		const bool use = Pressed('E');

		s.accumulator += dt;
		int steps = 0;
		while (s.accumulator >= kStep && steps < 10)
		{
			cs::MoveInput stepIn = in;
			if (s.wheelJump) { stepIn.jump = true; s.wheelJump = false; }   // one step of +jump, then -jump
			Step(s, stepIn, attack, attack2, reload, use);
			s.accumulator -= kStep;
			steps++;
		}
		if (steps == 10) s.accumulator = 0;

		const zVEC3 heroFeet = ToGothic(s.pm.Feet());
		if ((heroFeet - s.worldCenter).Length() > kWorldRebuildCm) RebuildWorld(s, heroFeet);

		// the Gothic hero follows (AI, perception and saves see this position)
		player->SetPositionWorld(heroFeet + zVEC3(0, s.hipHeight, 0));
		cs::Vec3 f;
		cs::AngleVectors(0, s.yaw, &f, nullptr, nullptr);
		player->SetHeadingAtWorld(DirToGothic(f));

		const float alpha = float(s.accumulator / kStep);
		ApplyCamera(s, s.prevEye + (s.curEye - s.prevEye) * alpha);
		GrenadesFrame();
		for (DroppedWeapon& d : Dropped()) PlaceModelVob(d.vob, d.pos, d.yaw);
		if (Bomb().vob) PlaceModelVob(Bomb().vob, Bomb().pos, Bomb().yaw);

		DrawHudText(s);
		if (s.showDebug)
		{
			char buf[256];
			const cs::PlayerMove& pm = s.pm;
			Print(0, "[GothicCS] CS MODE (INSERT exit, HOME debug)");
			sprintf_s(buf, "pos CS %.1f %.1f %.1f  | Gothic %.0f %.0f %.0f cm  bombsites %d", pm.origin.x, pm.origin.y, pm.origin.z,
				heroFeet[0], heroFeet[1], heroFeet[2], (int)s.bombsites.size());
			Print(1, buf);
			sprintf_s(buf, "speed %.1f u/s  vz %.1f  %s%s  stamina %.0f  ground mat %d  max %.0f", cs::Length2D(pm.velocity),
				pm.velocity.z, pm.onGround ? "GROUND " : "AIR ", pm.ducked ? "DUCK " : "", pm.stamina, pm.groundSurface, pm.maxspeed);
			Print(2, buf);
			sprintf_s(buf, "yaw %.1f pitch %.1f fov %d  mouse %.1f %.1f  world %d tris %.1f ms  frame %.1f ms",
				s.yaw, s.pitch, s.fov, s.lastMouseDx, s.lastMouseDy, s.world.TriCount(), s.worldBuildMs, s.frameMs);
			Print(3, buf);
			const cs::Weapon* w = s.inv.active;
			sprintf_s(buf, "weapon %s clip %d acc %.3f shots %d next %.2f/%.2f idle %.2f natt %.2f  punch %.2f %.2f  anim %s",
				w ? w->I().model : "-", w ? w->clip : 0, w ? w->accuracy : 0, w ? w->shotsFired : 0, w ? w->nextPrimary : 0,
				w ? w->nextSecondary : 0, w ? w->timeIdle : 0, Host().playerNextAttack, s.punch.x, s.punch.y,
				s.vm ? s.vm->SequenceName() : "-");
			Print(4, buf);
			sprintf_s(buf, "vm %s tris %d %.2f ms mode %d | bots %d grenades %d bomb %s", s.vm && s.vm->loaded ? "ok" : (s.vm ? s.vm->error.c_str() : "-"),
				s.vm ? s.vm->drawnTris : 0, s.vm ? s.vm->drawMs : 0.0f, s.vm ? s.vm->drawMode : 0, (int)Bots().size(),
				(int)Grenades().size(), Bomb().planted ? "PLANTED" : "-");
			Print(5, buf);
			{
				oCSpawnManager* sm = ogame->GetSpawnManager();
				oCWorld* w = ogame->GetGameWorld();
				const zVEC3 cam = ogame->GetCameraVob() ? ogame->GetCameraVob()->GetPositionWorld() : zVEC3(0, 0, 0);
				sprintf_s(buf, "spawn: enabled %d  waiting %d  npcs %d  insert %.0f remove %.0f time %.1f",
					sm ? sm->spawningEnabled : -1, sm ? sm->spawnList.GetNumInList() : -1,
					w && w->voblist_npcs ? w->voblist_npcs->GetNumInList() : -1,
					oCSpawnManager::GetInsertRange(), oCSpawnManager::GetRemoveRange(), oCSpawnManager::GetSpawnTime());
				Print(7, buf);
				sprintf_s(buf, "spawn camPos %.0f %.0f %.0f | camera %.0f %.0f %.0f",
					sm ? sm->camPos[0] : 0, sm ? sm->camPos[1] : 0, sm ? sm->camPos[2] : 0, cam[0], cam[1], cam[2]);
				Print(8, buf);
			}
			Print(6, s.lastShot);
		}
	}

	// While CS mode runs, Gothic must not react to the keyboard (B opened a
	// Gothic screen, E picked up items...): our own input uses GetAsyncKeyState
	// and the mouse deltas. Esc (game menu) and any open Gothic menu keep working.
	inline bool BlockGothicInput()
	{
		State& s = S();
		if (!s.active || !ogame || ogame->IsOnPause()) return false;
		if (zCMenu::GetActive()) return false;
		return true;
	}
	inline bool BlockGothicKey(int key) { return key != KEY_ESCAPE && BlockGothicInput(); }

	// Called right after the game world was rendered (zCWorld::Render hook).
	inline void AfterWorldRender() {}

	// Called when the screen view draws its items, before them: world
	// sprites, bots' poses, the viewmodel, muzzle flash and HUD polygons.
	inline void BeforeScreenItems()
	{
		State& s = S();
		if (!s.active || !player || player != s.hero) return;
		const ViewCamera cam = CurrentViewCamera(s);
		BotsFrame(s, cam);
		const zTRnd_PolySortMode oldSort = zrenderer->GetPolySortMode();
		zrenderer->SetPolySortMode(zRND_SORT_NONE);
		zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);
		DrawWorldSprites(cam, (float)s.clock);
		zrenderer->FlushPolys();
		zrenderer->SetPolySortMode(oldSort);
		zrenderer->SetAlphaBlendFunc(zRND_ALPHA_FUNC_NONE);

		const bool scoped = s.inv.active && cs::IsSniper(s.inv.active->id) && s.fov != 90;
		if (s.vm && s.alive && !scoped)
		{
			DrawViewModel(*s.vm, 73.7398f);
			if (s.clock < s.muzzleFlashUntil)
			{
				char name[32];
				sprintf_s(name, "muzzleflash%d", s.muzzleSprite + 1);
				DrawMuzzleFlash(*s.vm, name, s.muzzleScale, s.muzzleFrame, s.muzzleRoll, 73.7398f);
			}
			if (s.showDebug) DrawTestQuads(*s.vm);
		}
		const zTRnd_PolySortMode oldSort2 = zrenderer->GetPolySortMode();
		zrenderer->SetPolySortMode(zRND_SORT_NONE);
		zrenderer->SetZBufferCompare(zRND_ZBUFFER_CMP_ALWAYS);
		DrawHudPolys(s);
		zrenderer->SetPolySortMode(oldSort2);
	}

	// Gothic damage on the hero while in CS mode (monsters, NPCs, traps): Gothic
	// computes its damage as usual on a temporarily huge HP pool (so it can
	// never kill the hero); the HP it took becomes CS damage - proportional to
	// the hero's max HP like the NPC mapping (the hero counts as a 100 HP CS
	// player), armour as for generic hits.
	inline bool PlayerDamageBegin(oCNpc* self, int& savedHp)
	{
		State& s = S();
		if (!s.active || self != player || self != s.hero || !s.alive) return false;
		savedHp = self->GetAttribute(NPC_ATR_HITPOINTS);
		self->SetAttribute(NPC_ATR_HITPOINTS, 1000000);
		return true;
	}

	inline void PlayerDamageEnd(oCNpc* self, int savedHp, const oCNpc::oSDamageDescriptor& d)
	{
		State& s = S();
		const int loss = 1000000 - self->GetAttribute(NPC_ATR_HITPOINTS);
		self->SetAttribute(NPC_ATR_HITPOINTS, savedHp);
		if (loss <= 0) return;
		const int hpMax = self->GetAttribute(NPC_ATR_HITPOINTSMAX);
		const float csDamage = Cfg().damageCS ? loss * 100.0f / float(hpMax > 0 ? hpMax : 100) : (float)loss;
		const char* who = d.pNpcAttacker ? d.pNpcAttacker->GetName(0).ToChar() : "World";
		DamagePlayer(s, csDamage, cs::HitGroup::Chest, 1.0f, who, "gothic");
		sprintf_s(s.lastShot, "damage from %s: Gothic %d HP -> CS %.1f (HP %d, armor %d)", who, loss, csDamage,
			s.inv.health, s.inv.armor);
	}
}
