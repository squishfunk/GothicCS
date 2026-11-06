// GothicCS - Union plugin side, core: CS mode state, Gothic <-> CS space,
// collision world, sounds. Included from Plugin.ipp inside GOTHIC_NAMESPACE
// (compiled for G1 = Gothic 1.08k_mod).
//
// Keys (CS mode): INSERT toggles CS mode, HOME debug overlay.
//   W/A/S/D, mouse, SPACE jump, LCTRL duck, LSHIFT walk
//   LMB attack, RMB secondary (silencer / burst / zoom / knife stab), R reload
//   1 primary, 2 pistol, 3 knife, 4 grenades (again = next), 5 C4, Q last weapon, G drop
//   B buy menu (digits choose, 0 back/close), E defuse (use), M change team (T / CT)
//   K spawn an enemy bot, END set a bombsite (A, then B, then clear)
//   PAGE UP (with HOME) cycles the viewmodel draw mode (debug)
// Entering CS mode writes cs_world.obj (collision triangles around the hero,
// HL units, Z-up, right-handed; "# spawn x y z" = feet) to the System folder.

namespace GothicCS
{
	// ---- Gothic <-> CS space -----------------------------------------------
	// Gothic: centimetres, Y-up, left-handed. CS: inches, Z-up, right-handed.
	constexpr float kCmPerUnit = 2.54f;
	inline cs::Vec3 ToCS(const zVEC3& g) { return { g[0] / kCmPerUnit, g[2] / kCmPerUnit, g[1] / kCmPerUnit }; }
	inline zVEC3 ToGothic(const cs::Vec3& c) { return zVEC3(c.x * kCmPerUnit, c.z * kCmPerUnit, c.y * kCmPerUnit); }
	inline zVEC3 DirToGothic(const cs::Vec3& c) { return zVEC3(c.x, c.z, c.y); }
	inline cs::Vec3 DirToCS(const zVEC3& g) { return { g[0], g[2], g[1] }; }

	inline std::string SystemDir()
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);   // ...\System\GOTHIC.EXE
		std::string p = exe;
		return p.substr(0, p.find_last_of("\\/"));
	}

	// ---- configuration: <Gothic>\System\GothicCS.ini -----------------------
	//   [GothicCS]
	//   Sensitivity=3.0     CS "sensitivity" (degrees per mouse count = 0.022 * this)
	//   MouseScale=1.0      extra factor for Gothic's mouse deltas
	//   DamageMode=cs       cs: every NPC counts as a 100 HP CS player; gothic: raw damage
	//   StartMoney=4000    money on entering CS mode (CS default 800)
	//   Server cvars, names and defaults as in CS 1.6 (stock = 1:1):
	//     sv_gravity=800 sv_accelerate=5 sv_airaccelerate=10 sv_friction=4 sv_stopspeed=75
	//     sv_maxspeed=320 sv_maxvelocity=2000 sv_edgefriction=2 sv_stepsize=18
	//   AutoBunnyhop=1      hold jump = bhop like deathrun/bhop servers (AMXX: oldbuttons cleared)
	//   BhopNoSlowdown=1    no jump stamina (AMXX: fuser2 = 0), as those servers did
	//   GodMode=0           start with god mode (O toggles in game)
	//   SpawnRangeScale=1.0 Gothic spawn manager insert/remove range x this in CS mode (1 = Gothic default;
	//                       >1 makes far NPCs appear frozen: Gothic only runs NPC AI near the camera)
	//   SpawnCheckTime=0    seconds between Gothic's NPC insert/remove checks (0 = Gothic default)
	//   BombAnywhere=1      plant the C4 anywhere (0 = only inside bombsite A/B, as in CS)
	//   SpawnInView=1       in CS mode also insert NPCs whose spawn point is in view (Gothic waits
	//                       until you look away, which hides NPCs at noclip/bhop speed)
	struct Config
	{
		float sensitivity = 3.0f, mouseScale = 1.0f;
		bool damageCS = true;
		int startMoney = 4000;
		float airAccelerate = 100.0f, maxVelocity = 3500.0f;
		bool autoBunnyhop = true, bhopNoSlowdown = true, godMode = false;
		float spawnRangeScale = 1.0f, spawnCheckTime = 0.0f;
		bool bombAnywhere = false, spawnInView = true;
		float gravity = 800, accelerate = 5, friction = 4, stopspeed = 75, maxspeedCvar = 320, edgefriction = 2, stepsize = 18;
		void Load()
		{
			const std::string ini = SystemDir() + "\\GothicCS.ini";
			auto get = [&](const char* key, const char* def) {
				char v[64] = {};
				GetPrivateProfileStringA("GothicCS", key, def, v, sizeof(v), ini.c_str());
				return std::string(v);
			};
			sensitivity = (float)atof(get("Sensitivity", "3.0").c_str());
			mouseScale = (float)atof(get("MouseScale", "1.0").c_str());
			damageCS = _stricmp(get("DamageMode", "cs").c_str(), "gothic") != 0;
			startMoney = atoi(get("StartMoney", "4000").c_str());
			autoBunnyhop = atoi(get("AutoBunnyhop", "1").c_str()) != 0;
			bhopNoSlowdown = atoi(get("BhopNoSlowdown", "1").c_str()) != 0;
			godMode = atoi(get("GodMode", "0").c_str()) != 0;
			spawnRangeScale = (float)atof(get("SpawnRangeScale", "1.0").c_str());
			spawnCheckTime = (float)atof(get("SpawnCheckTime", "0").c_str());
			bombAnywhere = atoi(get("BombAnywhere", "0").c_str()) != 0;
			spawnInView = atoi(get("SpawnInView", "1").c_str()) != 0;
			auto f = [&](const char* key, const char* def) { return (float)atof(get(key, def).c_str()); };
			gravity = f("sv_gravity", "800"); accelerate = f("sv_accelerate", "5"); friction = f("sv_friction", "4");
			stopspeed = f("sv_stopspeed", "75"); maxspeedCvar = f("sv_maxspeed", "320");
			edgefriction = f("sv_edgefriction", "2"); stepsize = f("sv_stepsize", "18");
			airAccelerate = f("sv_airaccelerate", get("AirAccelerate", "10").c_str());
			maxVelocity = f("sv_maxvelocity", get("MaxVelocity", "2000").c_str());
		}
		// PAGE UP / PAGE DOWN change the sensitivity in game; keep it in the ini.
		void SaveSensitivity() const
		{
			char v[32];
			sprintf_s(v, "%.2f", sensitivity);
			WritePrivateProfileStringA("GothicCS", "Sensitivity", v, (SystemDir() + "\\GothicCS.ini").c_str());
		}
	};
	inline Config& Cfg() { static Config c; static bool loaded = false; if (!loaded) { c.Load(); loaded = true; } return c; }

	// ---- world extraction (same filters as GothicSkate) ---------------------
	inline bool SkipMaterial(const zCMaterial* mat)
	{
		if (!mat) return false;
		if (mat->noCollDet) return true;
		const int f = mat->rndAlphaBlendFunc;
		return f != zRND_ALPHA_FUNC_MAT_DEFAULT && f != zRND_ALPHA_FUNC_NONE;
	}

	inline bool IsVegetation(zCVob* vob)
	{
		if (!vob->visual) return false;
		std::string name = vob->visual->GetVisualName().ToChar();
		for (char& c : name) c = (char)toupper((unsigned char)c);
		for (const char* key : { "TREE", "BAUM", "BUSH", "PLANT", "GRASS", "FARN", "STRAUCH", "NATURE", "PILZ", "BLUME" })
			if (name.find(key) != std::string::npos) return true;
		return false;
	}

	inline uint8_t SurfaceOf(const zCMaterial* mat) { return mat ? (uint8_t)mat->matGroup : 0; }

	// Static world polygons and collidable vob meshes inside a cube around
	// `center` (cm). Gothic polygons are clockwise in a left-handed space;
	// ToCS mirrors the space, so the original vertex order stays correct.
	inline void CollectWorld(const zVEC3& center, float halfExtentCm, cs::TriWorld& out)
	{
		out.Clear();
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world || !world->bspTree.bspRoot) return;

		zTBBox3D box;
		box.mins = center - zVEC3(halfExtentCm, halfExtentCm, halfExtentCm);
		box.maxs = center + zVEC3(halfExtentCm, halfExtentCm, halfExtentCm);

		zCPolygon** polys = nullptr;
		int count = 0;
		world->bspTree.bspRoot->CollectPolysInBBox3D(box, polys, count);
		std::vector<zCPolygon*> unique(polys, polys + count);   // one entry per BSP leaf
		std::sort(unique.begin(), unique.end());
		unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
		for (zCPolygon* poly : unique)
		{
			if (!poly || poly->polyNumVert < 3) continue;
			if (poly->flags.portalPoly || poly->flags.ghostOccluder) continue;
			if (SkipMaterial(poly->material)) continue;
			const uint8_t surf = SurfaceOf(poly->material);
			const cs::Vec3 a = ToCS(poly->vertex[0]->position);
			for (int v = 1; v + 1 < poly->polyNumVert; v++)
				out.AddTri(a, ToCS(poly->vertex[v]->position), ToCS(poly->vertex[v + 1]->position), surf);
		}

		zCArray<zCVob*> vobs;
		world->bspTree.bspRoot->CollectVobsInBBox3D(vobs, box);
		std::vector<zCVob*> uv;
		for (int i = 0; i < vobs.GetNumInList(); i++) uv.push_back(vobs[i]);
		std::sort(uv.begin(), uv.end());
		uv.erase(std::unique(uv.begin(), uv.end()), uv.end());
		for (zCVob* vob : uv)
		{
			if (!vob || vob == player || !vob->collDetectionDynamic || vob->ignoredByTraceRay) continue;
			if (vob->CastTo<oCNpc>() || vob->CastTo<oCItem>()) continue;
			if (vob->CastTo<oCMobLadder>())
			{
				// CS func_ladder: a climbable volume; the thin horizontal side is its normal
				cs::Ladder l;
				const cs::Vec3 a = ToCS(vob->bbox3D.mins), b = ToCS(vob->bbox3D.maxs);
				l.mins = cs::Vec3((std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z));
				l.maxs = cs::Vec3((std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z));
				l.axis = (l.maxs.x - l.mins.x) < (l.maxs.y - l.mins.y) ? cs::Vec3(1, 0, 0) : cs::Vec3(0, 1, 0);
				out.ladders.push_back(l);
				continue;
			}
			zCProgMeshProto* mesh = zDYNAMIC_CAST<zCProgMeshProto>(vob->visual);
			if (!mesh || mesh->posList.numInArray <= 0) continue;
			const bool vegetation = IsVegetation(vob);
			const float trunkTopY = vob->trafoObjToWorld.GetTranslation()[1] + 250.0f;   // trunk only, 2.5 m
			const zMAT4& toWorld = vob->trafoObjToWorld;
			std::vector<zVEC3> pos(mesh->posList.numInArray);
			for (int p = 0; p < mesh->posList.numInArray; p++) pos[p] = toWorld * mesh->posList.array[p];
			for (int sm = 0; sm < mesh->numSubMeshes; sm++)
			{
				const zCProgMeshProto::zCSubMesh& sub = mesh->subMeshList[sm];
				if (SkipMaterial(sub.material)) continue;
				const uint8_t surf = SurfaceOf(sub.material);
				for (int t = 0; t < sub.triList.numInArray; t++)
				{
					const zTPMTriangle& tri = sub.triList.array[t];
					const zVEC3& a = pos[sub.wedgeList.array[tri.wedge[0]].position];
					const zVEC3& b = pos[sub.wedgeList.array[tri.wedge[1]].position];
					const zVEC3& c = pos[sub.wedgeList.array[tri.wedge[2]].position];
					if (vegetation && (a[1] > trunkTopY || b[1] > trunkTopY || c[1] > trunkTopY)) continue;
					out.AddTri(ToCS(a), ToCS(b), ToCS(c), surf);
				}
			}
		}
		out.Build();
	}

	inline bool DumpObj(const cs::TriWorld& w, const char* path, const cs::Vec3& spawnFeet)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") != 0 || !f) return false;
		fprintf(f, "# GothicCS world dump: HL units (inch), Z-up, right-handed (Gothic x, z, y / 2.54)\n");
		fprintf(f, "# spawn %.3f %.3f %.3f\n", spawnFeet.x, spawnFeet.y, spawnFeet.z);
		for (const cs::Vec3& p : w.verts) fprintf(f, "v %.3f %.3f %.3f\n", p.x, p.y, p.z);
		for (int t = 0; t < w.TriCount(); t++) fprintf(f, "f %d %d %d\n", t * 3 + 1, t * 3 + 2, t * 3 + 3);
		fclose(f);
		return true;
	}

	// ---- state ---------------------------------------------------------------
	constexpr float kStep = 0.01f;            // movement + weapons at 100 Hz (typical CS client)
	constexpr float kWorldHalfCm = 2000.0f;   // collision window: 40 m cube
	constexpr float kWorldRebuildCm = 800.0f; // rebuilt after moving 8 m from its centre

	struct KillFeedLine { std::string text; DWORD until; };
	struct Bombsite { cs::Vec3 pos; char name; };

	struct State
	{
		bool active = false;
		bool showDebug = false;
		cs::TriWorld world;
		cs::PlayerMove pm;
		float yaw = 0, pitch = 0;
		cs::Vec3 prevEye, curEye;          // render interpolation between 100 Hz steps
		double accumulator = 0;
		LARGE_INTEGER lastTime{};
		zVEC3 worldCenter{ 0, 0, 0 };
		float worldBuildMs = 0;
		float hipHeight = 90.0f;           // Gothic hero origin above his feet (cm)
		float savedFovH = 0, savedFovV = 0;
		bool savedStat = true, savedDyn = true, savedShowVisual = true;
		oCNpc* hero = nullptr;             // the NPC we took over
		oCWorld* enterWorld = nullptr;     // world at entering CS mode
		bool keyPrev[256] = {};
		float lastMouseDx = 0, lastMouseDy = 0;
		float frameMs = 0;
		char toast[160] = "";
		DWORD toastUntil = 0;
		double clock = 0;                  // CS time, advanced by the 100 Hz steps
		cs::Rng rng;

		// CS player
		cs::Inventory inv;
		cs::Vec3 punch;
		int fov = 90;
		bool frozen = false;               // C4 arming / defusing
		bool noclip = false;               // V: fly through the world
		bool god = false;                  // O: god mode (CS "god")
		bool alive = true;
		double deathTime = 0;
		bool everEntered = false;
		cs::Vec3 spawnFeet;
		float spawnYaw = 0;
		float progressTotal = 0;           // progress bar (seconds), 0 = hidden
		double progressStart = 0;
		double flashStart = 0;             // flashbang white-out (UTIL_ScreenFade)
		float flashHold = 0, flashFade = 0, flashAlpha = 0;
		double nextStepSound = 0;
		int stepLeft = 0;

		// viewmodels (one per weapon model, loaded on demand)
		std::map<std::string, ViewModel*> viewmodels;
		ViewModel* vm = nullptr;
		double muzzleFlashUntil = 0;
		int muzzleSprite = 2, muzzleFrame = 0;
		float muzzleScale = 0.2f, muzzleRoll = 0;

		// buy menu: 0 closed, 9 main, 1..5 weapon sub menus, 8 equipment
		int buyMenu = 0;

		std::vector<KillFeedLine> killFeed;
		std::vector<Bombsite> bombsites;
		char lastShot[220] = "";
		int shotsTotal = 0;
		int pendingAward = 0;              // team reward / loss bonus paid at the next round start
		bool wheelJump = false;            // mouse wheel = +jump for one step (CS bind)
		char centre[160] = "";             // CS centre-print message
		DWORD centreUntil = 0;
	};
	inline State& S() { static State s; return s; }

	inline void Toast(const char* text)
	{
		sprintf_s(S().toast, "%s", text);
		S().toastUntil = GetTickCount() + 2000;
	}

	// CS centre print (round / bomb messages).
	inline void CentrePrint(const char* text)
	{
		sprintf_s(S().centre, "%s", text);
		S().centreUntil = GetTickCount() + 4000;
	}

	// Own edge detection on the raw key state (Gothic eats KeyToggled for some keys).
	inline bool Pressed(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
	inline bool PressedOnce(int vk)
	{
		const bool now = Pressed(vk);
		const bool was = S().keyPrev[vk & 0xFF];
		S().keyPrev[vk & 0xFF] = now;
		return now && !was;
	}

	inline zVEC3 GroundBelow(const zVEC3& from, bool& hit)
	{
		oCWorld* world = ogame->GetGameWorld();
		const int flags = zTRACERAY_STAT_POLY | zTRACERAY_POLY_NORMAL | zTRACERAY_VOB_IGNORE_CHARACTER;
		hit = world && world->TraceRayNearestHit(from, zVEC3(0, -400.0f, 0), player, flags);
		return hit ? world->traceRayReport.foundIntersection : from;
	}

	inline void RebuildWorld(State& s, const zVEC3& center)
	{
		LARGE_INTEGER a, b, f;
		QueryPerformanceCounter(&a);
		CollectWorld(center, kWorldHalfCm, s.world);
		QueryPerformanceCounter(&b);
		QueryPerformanceFrequency(&f);
		s.worldBuildMs = float(double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart));
		s.worldCenter = center;
	}

	// ---- sounds: CS .wav copied by convert_cs.py into _work\Data\Sound\SFX ----
	inline std::string SfxName(const char* csPath)
	{
		std::string n = "CS_";
		for (const char* c = csPath; *c; c++) n += (*c == '/' || *c == '\\') ? '_' : (char)toupper((unsigned char)*c);
		return n;
	}

	inline zCSoundFX* LoadSfx(const std::string& gothicName)
	{
		if (!zsound || gothicName.empty()) return nullptr;
		static std::map<std::string, zCSoundFX*> cache;
		auto it = cache.find(gothicName);
		if (it != cache.end()) return it->second;
		zCSoundFX* fx = zsound->LoadSoundFX(zSTRING(gothicName.c_str()));
		cache[gothicName] = fx;
		return fx;
	}

	inline void PlaySfx(const std::string& gothicName)
	{
		if (zCSoundFX* fx = LoadSfx(gothicName)) zsound->PlaySound(fx, 0);
	}

	inline void PlaySfx3D(const std::string& gothicName, zCVob* source)
	{
		zCSoundFX* fx = LoadSfx(gothicName);
		if (!fx || !source) return;
		zCSoundSystem::zTSound3DParams params;
		params.SetDefaults();
		zsound->PlaySound3D(fx, source, 0, &params);
	}

	// Positional sound without a vob of its own: a small pool of invisible emitter vobs.
	inline void PlaySfxAt(const std::string& gothicName, const zVEC3& where)
	{
		static zCVob* pool[12] = {};
		static oCWorld* poolWorld = nullptr;
		static int next = 0;
		oCWorld* world = ogame ? ogame->GetGameWorld() : nullptr;
		if (!world) return;
		if (poolWorld != world)
		{
			for (zCVob*& v : pool) v = nullptr;   // vobs of an unloaded world are gone
			poolWorld = world;
		}
		zCVob*& v = pool[next];
		next = (next + 1) % 12;
		if (!v)
		{
			v = new zCVob();
			v->SetCollDetStat(FALSE);
			v->SetCollDetDyn(FALSE);
			v->ignoredByTraceRay = TRUE;
			v->dontWriteIntoArchive = TRUE;
			world->AddVob(v);
		}
		v->SetPositionWorld(where);
		PlaySfx3D(gothicName, v);
	}

	// Forward declarations (defined in the later .inl files).
	struct WorldHit;
	inline void Impact(State& s, const WorldHit& h);
	inline void OnKill(State& s, const char* victim, const char* weapon, bool headshot);
	inline void DamagePlayer(State& s, float damage, cs::HitGroup group, float armorRatio, const char* attacker,
		const char* weapon, bool blast = false);
}
