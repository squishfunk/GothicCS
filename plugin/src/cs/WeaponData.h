// GothicCS - Counter-Strike 1.6 weapon table (engine-independent data).
// Values from ReGameDLL_CS (MIT, see NOTICE), original CS 1.6 branches;
// extracted in THIRD_PARTY_NOTICES.md and THIRD_PARTY_NOTICES.md.
#pragma once

namespace cs
{
	enum WeaponId
	{
		W_NONE = 0, W_P228 = 1, W_SCOUT = 3, W_HEGRENADE = 4, W_XM1014 = 5, W_C4 = 6, W_MAC10 = 7, W_AUG = 8,
		W_SMOKEGRENADE = 9, W_ELITE = 10, W_FIVESEVEN = 11, W_UMP45 = 12, W_SG550 = 13, W_GALIL = 14, W_FAMAS = 15,
		W_USP = 16, W_GLOCK18 = 17, W_AWP = 18, W_MP5N = 19, W_M249 = 20, W_M3 = 21, W_M4A1 = 22, W_TMP = 23,
		W_G3SG1 = 24, W_FLASHBANG = 25, W_DEAGLE = 26, W_SG552 = 27, W_AK47 = 28, W_KNIFE = 29, W_P90 = 30,
		W_COUNT = 31
	};

	enum AmmoType { A_NONE = 0, A_338MAG, A_762, A_556BOX, A_556, A_BUCK, A_45ACP, A_57, A_50AE, A_357SIG, A_9MM, A_COUNT };

	struct AmmoInfo { const char* name; int maxCarry, price, buyAmount; };
	inline const AmmoInfo& Ammo(AmmoType t)
	{
		static const AmmoInfo k[A_COUNT] = {
			{ "", 0, 0, 0 }, { "338Magnum", 30, 125, 10 }, { "762Nato", 90, 80, 30 }, { "556NatoBox", 200, 60, 30 },
			{ "556Nato", 90, 60, 30 }, { "buckshot", 32, 65, 8 }, { "45acp", 100, 25, 12 }, { "57mm", 100, 50, 50 },
			{ "50AE", 35, 40, 7 }, { "357SIG", 52, 50, 13 }, { "9mm", 120, 20, 30 },
		};
		return k[t];
	}

	enum Slot { SLOT_PRIMARY = 1, SLOT_PISTOL = 2, SLOT_KNIFE = 3, SLOT_GRENADE = 4, SLOT_C4 = 5 };
	enum class BulletType { None, Mm9, Acp45, Ae50, Mm762, Mm556, Mag338, Mm57, Sig357, Buckshot };
	enum TeamMask { TEAM_T = 1, TEAM_CT = 2, TEAM_BOTH = 3 };

	struct WeaponInfo
	{
		WeaponId id;
		const char* name;        // display name
		const char* model;       // viewmodel / p_ / w_ suffix, e.g. "AK47" (v_ak47.mdl -> AK47.csvm, CSP_AK47.3DS)
		Slot slot;
		int price, maxClip;
		AmmoType ammo;
		float maxSpeed, zoomSpeed;
		float damage, rangeMod;
		BulletType bullet;
		int penetration;
		float distance;
		float armorRatio;
		float cycle, reload;
		int team;                // who can buy it (TeamMask)
		const char* fire[2];     // fire sounds (client event), random of the non-null ones
	};

	inline const WeaponInfo& Info(WeaponId id)
	{
		using B = BulletType;
		static const WeaponInfo k[W_COUNT] = {
			{ W_NONE, "", "", SLOT_KNIFE, 0, 0, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, 0, { nullptr, nullptr } },
			{ W_P228, "228 Compact", "P228", SLOT_PISTOL, 600, 13, A_357SIG, 250, 250, 32, 0.8f, B::Sig357, 1, 4096, 1.25f, 0.15f, 2.7f, TEAM_BOTH, { "weapons/p228-1.wav", nullptr } },
			{ W_NONE, "", "", SLOT_KNIFE, 0, 0, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, 0, { nullptr, nullptr } },
			{ W_SCOUT, "Schmidt Scout", "SCOUT", SLOT_PRIMARY, 2750, 10, A_762, 260, 220, 75, 0.98f, B::Mm762, 3, 8192, 1.7f, 1.25f, 2.0f, TEAM_BOTH, { "weapons/scout_fire-1.wav", nullptr } },
			{ W_HEGRENADE, "HE Grenade", "HEGRENADE", SLOT_GRENADE, 300, -1, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, TEAM_BOTH, { nullptr, nullptr } },
			{ W_XM1014, "Leone YG1265 Auto Shotgun", "XM1014", SLOT_PRIMARY, 3000, 7, A_BUCK, 240, 240, 20, 1, B::Buckshot, 0, 3048, 1.0f, 0.25f, 0, TEAM_BOTH, { "weapons/xm1014-1.wav", nullptr } },
			{ W_C4, "C4", "C4", SLOT_C4, 0, -1, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, TEAM_T, { nullptr, nullptr } },
			{ W_MAC10, "Ingram MAC-10", "MAC10", SLOT_PRIMARY, 1400, 30, A_45ACP, 250, 250, 29, 0.82f, B::Acp45, 1, 8192, 0.95f, 0.07f, 3.15f, TEAM_T, { "weapons/mac10-1.wav", nullptr } },
			{ W_AUG, "Bullpup", "AUG", SLOT_PRIMARY, 3500, 30, A_556, 240, 240, 32, 0.96f, B::Mm556, 2, 8192, 1.4f, 0.0825f, 3.3f, TEAM_CT, { "weapons/aug-1.wav", nullptr } },
			{ W_SMOKEGRENADE, "Smoke Grenade", "SMOKEGRENADE", SLOT_GRENADE, 300, -1, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, TEAM_BOTH, { nullptr, nullptr } },
			{ W_ELITE, ".40 Dual Elites", "ELITE", SLOT_PISTOL, 800, 30, A_9MM, 250, 250, 36, 0.75f, B::Mm9, 1, 8192, 1.05f, 0.075f, 4.5f, TEAM_T, { "weapons/elite_fire.wav", nullptr } },
			{ W_FIVESEVEN, "ES Five-Seven", "FIVESEVEN", SLOT_PISTOL, 750, 20, A_57, 250, 250, 20, 0.885f, B::Mm57, 1, 4096, 1.5f, 0.15f, 2.7f, TEAM_CT, { "weapons/fiveseven-1.wav", nullptr } },
			{ W_UMP45, "KM UMP45", "UMP45", SLOT_PRIMARY, 1700, 25, A_45ACP, 250, 250, 30, 0.82f, B::Acp45, 1, 8192, 1.0f, 0.1f, 3.5f, TEAM_BOTH, { "weapons/ump45-1.wav", nullptr } },
			{ W_SG550, "Krieg 550 Commando", "SG550", SLOT_PRIMARY, 4200, 30, A_556, 210, 150, 70, 0.98f, B::Mm556, 2, 8192, 1.45f, 0.25f, 3.35f, TEAM_CT, { "weapons/sg550-1.wav", nullptr } },
			{ W_GALIL, "IDF Defender", "GALIL", SLOT_PRIMARY, 2000, 35, A_556, 240, 240, 30, 0.98f, B::Mm556, 2, 8192, 1.55f, 0.0875f, 2.45f, TEAM_T, { "weapons/galil-1.wav", "weapons/galil-2.wav" } },
			{ W_FAMAS, "Clarion 5.56", "FAMAS", SLOT_PRIMARY, 2250, 25, A_556, 240, 240, 30, 0.96f, B::Mm556, 2, 8192, 1.4f, 0.0825f, 3.3f, TEAM_CT, { "weapons/famas-1.wav", "weapons/famas-2.wav" } },
			{ W_USP, "K&M .45 Tactical", "USP", SLOT_PISTOL, 500, 12, A_45ACP, 250, 250, 34, 0.79f, B::Acp45, 1, 4096, 1.0f, 0.15f, 2.7f, TEAM_BOTH, { "weapons/usp_unsil-1.wav", nullptr } },
			{ W_GLOCK18, "9X19mm Sidearm", "GLOCK18", SLOT_PISTOL, 400, 20, A_9MM, 250, 250, 25, 0.75f, B::Mm9, 1, 8192, 1.05f, 0.15f, 2.2f, TEAM_BOTH, { "weapons/glock18-2.wav", nullptr } },
			{ W_AWP, "Magnum Sniper Rifle", "AWP", SLOT_PRIMARY, 4750, 10, A_338MAG, 210, 150, 115, 0.99f, B::Mag338, 3, 8192, 1.95f, 1.45f, 2.5f, TEAM_BOTH, { "weapons/awp1.wav", nullptr } },
			{ W_MP5N, "KM Sub-Machine Gun", "MP5", SLOT_PRIMARY, 1500, 30, A_9MM, 250, 250, 26, 0.84f, B::Mm9, 1, 8192, 1.0f, 0.075f, 2.63f, TEAM_BOTH, { "weapons/mp5-1.wav", "weapons/mp5-2.wav" } },
			{ W_M249, "M249", "M249", SLOT_PRIMARY, 5750, 100, A_556BOX, 220, 220, 32, 0.97f, B::Mm556, 2, 8192, 1.5f, 0.1f, 4.7f, TEAM_BOTH, { "weapons/m249-1.wav", "weapons/m249-2.wav" } },
			{ W_M3, "Leone 12 Gauge Super", "M3", SLOT_PRIMARY, 1700, 8, A_BUCK, 230, 230, 20, 1, B::Buckshot, 0, 3000, 1.0f, 0.875f, 0, TEAM_BOTH, { "weapons/m3-1.wav", nullptr } },
			{ W_M4A1, "Maverick M4A1 Carbine", "M4A1", SLOT_PRIMARY, 3100, 30, A_556, 230, 230, 32, 0.97f, B::Mm556, 2, 8192, 1.4f, 0.0875f, 3.05f, TEAM_CT, { "weapons/m4a1_unsil-1.wav", "weapons/m4a1_unsil-2.wav" } },
			{ W_TMP, "Schmidt Machine Pistol", "TMP", SLOT_PRIMARY, 1250, 30, A_9MM, 250, 250, 20, 0.85f, B::Mm9, 1, 8192, 1.0f, 0.07f, 2.12f, TEAM_CT, { "weapons/tmp-1.wav", "weapons/tmp-2.wav" } },
			{ W_G3SG1, "D3/AU-1", "G3SG1", SLOT_PRIMARY, 5000, 20, A_762, 210, 150, 80, 0.98f, B::Mm762, 3, 8192, 1.65f, 0.25f, 3.5f, TEAM_T, { "weapons/g3sg1-1.wav", nullptr } },
			{ W_FLASHBANG, "Flashbang", "FLASHBANG", SLOT_GRENADE, 200, -1, A_NONE, 250, 250, 0, 1, B::None, 0, 0, 1, 0, 0, TEAM_BOTH, { nullptr, nullptr } },
			{ W_DEAGLE, "Night Hawk .50C", "DEAGLE", SLOT_PISTOL, 650, 7, A_50AE, 250, 250, 54, 0.81f, B::Ae50, 2, 4096, 1.5f, 0.225f, 2.2f, TEAM_BOTH, { "weapons/deagle-1.wav", "weapons/deagle-2.wav" } },
			{ W_SG552, "Krieg 552", "SG552", SLOT_PRIMARY, 3500, 30, A_556, 235, 200, 33, 0.955f, B::Mm556, 2, 8192, 1.4f, 0.0825f, 3.0f, TEAM_T, { "weapons/sg552-1.wav", "weapons/sg552-2.wav" } },
			{ W_AK47, "CV-47", "AK47", SLOT_PRIMARY, 2500, 30, A_762, 221, 221, 36, 0.98f, B::Mm762, 2, 8192, 1.55f, 0.0955f, 2.45f, TEAM_T, { "weapons/ak47-1.wav", "weapons/ak47-2.wav" } },
			{ W_KNIFE, "Knife", "KNIFE", SLOT_KNIFE, 0, -1, A_NONE, 250, 250, 15, 1, B::None, 0, 48, 1.7f, 0, 0, 0, { nullptr, nullptr } },
			{ W_P90, "ES C90", "P90", SLOT_PRIMARY, 2350, 50, A_57, 245, 245, 21, 0.885f, B::Mm57, 1, 8192, 1.5f, 0.066f, 3.4f, TEAM_BOTH, { "weapons/p90-1.wav", nullptr } },
		};
		return k[(id >= 0 && id < W_COUNT) ? id : 0];
	}

	inline bool IsGrenade(WeaponId id) { return id == W_HEGRENADE || id == W_FLASHBANG || id == W_SMOKEGRENADE; }
	inline bool IsPistol(WeaponId id) { return Info(id).slot == SLOT_PISTOL; }
	inline bool IsShotgun(WeaponId id) { return id == W_M3 || id == W_XM1014; }
	inline bool IsSniper(WeaponId id) { return id == W_AWP || id == W_SCOUT || id == W_G3SG1 || id == W_SG550; }
	inline bool HasZoom(WeaponId id) { return IsSniper(id) || id == W_AUG || id == W_SG552; }

	// Old-style buy menu (titles.txt "Buy" tree): key -> weapon per team.
	inline WeaponId BuyMenuWeapon(int menu, int key, bool ct)
	{
		switch (menu)
		{
		case 1: { static const WeaponId c[] = { W_GLOCK18, W_USP, W_P228, W_DEAGLE, W_FIVESEVEN }, t[] = { W_GLOCK18, W_USP, W_P228, W_DEAGLE, W_ELITE };
			return key >= 1 && key <= 5 ? (ct ? c : t)[key - 1] : W_NONE; }
		case 2: return key == 1 ? W_M3 : (key == 2 ? W_XM1014 : W_NONE);
		case 3: { static const WeaponId c[] = { W_TMP, W_MP5N, W_UMP45, W_P90 }, t[] = { W_MAC10, W_MP5N, W_UMP45, W_P90 };
			return key >= 1 && key <= 4 ? (ct ? c : t)[key - 1] : W_NONE; }
		case 4: { static const WeaponId c[] = { W_FAMAS, W_SCOUT, W_M4A1, W_AUG, W_SG550, W_AWP }, t[] = { W_GALIL, W_AK47, W_SCOUT, W_SG552, W_AWP, W_G3SG1 };
			return key >= 1 && key <= 6 ? (ct ? c : t)[key - 1] : W_NONE; }
		case 5: return key == 1 ? W_M249 : W_NONE;
		}
		return W_NONE;
	}

	// Equipment prices (weapontype.h)
	constexpr int PRICE_KEVLAR = 650, PRICE_ASSAULTSUIT = 1000, PRICE_HELMET = 350, PRICE_NVG = 1250, PRICE_DEFUSEKIT = 200;
	constexpr int PRICE_FLASH = 200, PRICE_HE = 300, PRICE_SMOKE = 300;
	constexpr int MAX_FLASH = 2, MAX_HE = 1, MAX_SMOKE = 1;
	constexpr int START_MONEY = 800, MAX_MONEY = 16000, REWARD_KILL = 300;
}
