// GothicCS - Counter-Strike 1.6 weapon behaviour (engine-independent).
//
// Numbers and rules: ReGameDLL_CS (MIT, see NOTICE), original CS 1.6
// branches, collected in THIRD_PARTY_NOTICES.md. The code is our own: it
// follows the observable CS rules (ItemPostFrame order, relative weapon
// timers, per-weapon spread / accuracy / recoil, burst, silencer, zoom,
// shotgun shell reload, knife, grenade throw on release, C4 arming).
// The host (Gothic side) does traces, damage, sounds and animations through
// WeaponHost.
#pragma once
#include "Vec.h"
#include "WeaponData.h"
#include <cmath>
#include <cstdint>

namespace cs
{
	// Small deterministic RNG (xorshift), our own - not the GoldSrc seed table.
	struct Rng
	{
		uint32_t s = 0x9E3779B9u;
		uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
		float Float(float lo, float hi) { return lo + (hi - lo) * (Next() & 0xFFFFFF) / float(0x1000000); }
		int Long(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
	};

	inline void BulletPenetration(BulletType t, int& power, float& distance)
	{
		switch (t)
		{
		case BulletType::Mm9: power = 21; distance = 800; break;
		case BulletType::Acp45: power = 15; distance = 500; break;
		case BulletType::Ae50: power = 30; distance = 1000; break;
		case BulletType::Mm762: power = 39; distance = 5000; break;
		case BulletType::Mm556: power = 35; distance = 4000; break;
		case BulletType::Mag338: power = 45; distance = 8000; break;
		case BulletType::Mm57: power = 30; distance = 2000; break;
		case BulletType::Sig357: power = 25; distance = 800; break;
		default: power = 0; distance = 0; break;
		}
	}

	struct Kick { float upBase, latBase, upMod, latMod, upMax, latMax; int dirChange; };

	enum GrenadeKind { GREN_HE, GREN_FLASH, GREN_SMOKE };
	enum KnifeResult { KNIFE_MISS = 0, KNIFE_WORLD = 1, KNIFE_BODY = 2 };

	// The player / bot that holds the weapon.
	struct WeaponHost
	{
		virtual ~WeaponHost() {}
		virtual float Speed2D() = 0;
		virtual bool OnGround() = 0;
		virtual bool Ducking() = 0;
		virtual Vec3& Punch() = 0;
		virtual int& Fov() = 0;                   // 90 = not zoomed
		virtual int& AmmoReserve(AmmoType t) = 0;
		virtual float& NextAttack() = 0;          // player m_flNextAttack (relative)
		virtual Rng& Random() = 0;
		virtual void Anim(int sequence) = 0;
		virtual void Sound(const char* csPath, float volume = 1.0f) = 0;
		// one FireBullets3 call; the host aims with view + `punch`, offsets the
		// source sideways by `sideOffset` units (dual elites)
		virtual void FireBullets3(float spread, float distance, int penetration, BulletType bullet, float damage,
			float rangeMod, const Vec3& punch, float sideOffset) = 0;
		virtual void FireBuckshot(int pellets, float cone, float distance, const Vec3& punch) = 0;
		virtual KnifeResult KnifeAttack(float range, float damage, bool stab) = 0;
		virtual void ThrowGrenade(GrenadeKind kind, const Vec3& punch) = 0;
		virtual bool InBombZone() = 0;
		virtual void PlantBomb() = 0;
		virtual void Message(const char* text) = 0;
		virtual void Progress(float seconds) = 0;  // 0 hides the bar
		virtual void Freeze(bool frozen) = 0;      // C4 arming: SET_CLIENT_MAXSPEED 1
		virtual void Retire() = 0;                 // switch to the best other weapon
		virtual void MuzzleFlash() = 0;
	};

	struct Weapon
	{
		WeaponId id = W_NONE;
		int clip = 0;
		int count = 0;            // grenades / C4 carried
		float accuracy = 0;
		int shotsFired = 0;
		int direction = 0;
		bool delayFire = false, inReload = false, fireOnEmpty = false;
		float nextPrimary = 0, nextSecondary = 0, timeIdle = 0;   // relative (CS CLIENT_WEAPONS timers)
		float decreaseShotsFired = 0, lastFire = 0;              // absolute
		bool silenced = false, burst = false, eliteLeft = false;
		float famasShoot = 0, glockShoot = 0, burstSpread = 0;  // absolute / spread
		int burstShots = 0;
		int specialReload = 0;    // shotguns: 0 idle, 1 start done, 2 inserting
		float startThrow = 0, releaseThrow = -1;
		int swing = 0;
		bool startedArming = false, placedAnim = false;
		float armedTime = 0;
		bool resumeZoom = false;
		int lastZoom = 90;

		const WeaponInfo& I() const { return Info(id); }
		bool Valid() const { return id != W_NONE; }

		// ---- generic timers ------------------------------------------------------
		void Tick(float dt)
		{
			nextPrimary = (std::max)(nextPrimary - dt, -1.0f);
			nextSecondary = (std::max)(nextSecondary - dt, -0.001f);
			if (timeIdle != 1000.0f) timeIdle = (std::max)(timeIdle - dt, -0.001f);
		}

		// ---- deploy / holster ----------------------------------------------------
		void Deploy(float now, WeaponHost& h)
		{
			h.NextAttack() = 0.75f;
			timeIdle = 1.5f;
			decreaseShotsFired = now;
			h.Fov() = 90; lastZoom = 90; resumeZoom = false;
			inReload = false;
			switch (id)
			{
			case W_AK47: case W_SG552: case W_GALIL: case W_P90: case W_TMP: case W_M249: case W_G3SG1:
				accuracy = 0.2f; shotsFired = 0; break;
			case W_M4A1: accuracy = 0.2f; shotsFired = 0; delayFire = true; break;
			case W_AUG: accuracy = 0.2f; shotsFired = 0; break;
			case W_FAMAS: accuracy = 0.2f; shotsFired = 0; burstShots = 0; famasShoot = 0; break;
			case W_MAC10: accuracy = 0.15f; break;
			case W_MP5N: case W_UMP45: accuracy = 0; break;
			case W_GLOCK18: accuracy = 0.9f; burstShots = 0; glockShoot = 0; break;
			case W_USP: case W_FIVESEVEN: accuracy = 0.92f; break;
			case W_P228: case W_DEAGLE: accuracy = 0.9f; break;
			case W_ELITE: accuracy = 0.88f; if (!(clip & 1)) eliteLeft = true; break;
			case W_AWP: h.NextAttack() = 1.45f; nextPrimary = 1.45f; nextSecondary = 1.0f; break;
			case W_SCOUT: h.NextAttack() = 1.25f; nextPrimary = 1.25f; nextSecondary = 1.0f; break;
			case W_KNIFE: swing = 0; h.Sound("weapons/knife_deploy1.wav", 0.3f); break;
			case W_HEGRENADE: case W_FLASHBANG: case W_SMOKEGRENADE: releaseThrow = -1; startThrow = 0; break;
			case W_C4: startedArming = false; armedTime = 0; break;
			default: break;
			}
			h.Anim(DrawAnim(h));
		}

		void Holster(WeaponHost& h)
		{
			inReload = false;
			specialReload = 0;
			if (id == W_KNIFE || IsGrenade(id) || id == W_C4) h.NextAttack() = 0.5f;
			if (IsGrenade(id)) { startThrow = 0; releaseThrow = -1; }
			if (id == W_C4 && startedArming) { startedArming = false; h.Freeze(false); h.Progress(0); }
			if (h.Fov() != 90) h.Fov() = 90;
		}

		int DrawAnim(WeaponHost& h) const
		{
			switch (id)
			{
			case W_USP: return silenced ? 6 : 14;
			case W_M4A1: return silenced ? 5 : 12;
			case W_GLOCK18: return h.Random().Long(0, 1) ? 8 : 11;
			case W_AWP: case W_DEAGLE: case W_FIVESEVEN: return 5;
			case W_SCOUT: case W_G3SG1: case W_SG550: case W_M249: return 4;
			case W_M3: case W_XM1014: case W_P228: return 6;
			case W_ELITE: return 15;
			case W_KNIFE: return 3;
			case W_HEGRENADE: case W_FLASHBANG: case W_SMOKEGRENADE: return 3;
			case W_C4: return 1;
			default: return 2;   // rifles / SMGs: IDLE1, RELOAD, DRAW, SHOOT1-3
			}
		}

		// ---- the CS ItemPostFrame --------------------------------------------------
		void PostFrame(float now, bool attack, bool attack2, bool reloadKey, WeaponHost& h)
		{
			if (h.NextAttack() > 0) return;   // CBasePlayer::ItemPostFrame

			// bursts in progress
			if (glockShoot != 0) FireRemaining(now, h, true);
			else if (now > famasShoot && famasShoot != 0) FireRemaining(now, h, false);

			if (nextPrimary <= 0 && resumeZoom)
			{
				h.Fov() = lastZoom;
				resumeZoom = false;
			}

			// finish a magazine reload
			if (inReload && h.NextAttack() <= 0)
			{
				int& reserve = h.AmmoReserve(I().ammo);
				int j = I().maxClip - clip;
				if (j > reserve) j = reserve;
				clip += j;
				reserve -= j;
				inReload = false;
			}

			const bool hasSecondary = HasSecondary();
			if (attack2 && hasSecondary && nextSecondary <= 0)
			{
				SecondaryAttack(now, h);
			}
			else if (attack && nextPrimary <= 0)
			{
				if (I().maxClip > 0 && clip == 0) fireOnEmpty = true;
				PrimaryAttack(now, h);
			}
			else if (reloadKey && I().maxClip > 0 && !inReload && nextPrimary < 0)
			{
				if (famasShoot == 0 && glockShoot == 0) Reload(h);
			}
			else if (!attack && !(attack2 && hasSecondary))
			{
				if (delayFire)
				{
					delayFire = false;
					if (shotsFired > 15) shotsFired = 15;
					decreaseShotsFired = now + 0.4f;
				}
				fireOnEmpty = false;
				if (IsPistol(id)) shotsFired = 0;
				else if (shotsFired > 0 && decreaseShotsFired < now)
				{
					decreaseShotsFired = now + 0.0225f;
					shotsFired--;
				}
				if (I().maxClip > 0 && clip == 0 && nextPrimary < 0 && famasShoot == 0 && glockShoot == 0 &&
					h.AmmoReserve(I().ammo) > 0)
				{
					Reload(h);
					return;
				}
				WeaponIdle(now, h);
			}
		}

		bool HasSecondary() const
		{
			switch (id)
			{
			case W_AK47: case W_XM1014: case W_MAC10: case W_ELITE: case W_FIVESEVEN: case W_MP5N: case W_M249:
			case W_M3: case W_TMP: case W_DEAGLE: case W_P228: case W_P90: case W_C4: case W_GALIL:
			case W_HEGRENADE: case W_FLASHBANG: case W_SMOKEGRENADE: case W_UMP45:
				return false;
			default: return true;
			}
		}

	private:
		// ---- helpers ---------------------------------------------------------------
		void KickBack(const Kick& k, WeaponHost& h)
		{
			Vec3& punch = h.Punch();
			float up, lat;
			if (shotsFired == 1) { up = k.upBase; lat = k.latBase; }
			else { up = shotsFired * k.upMod + k.upBase; lat = shotsFired * k.latMod + k.latBase; }
			punch.x -= up;
			if (punch.x < -k.upMax) punch.x = -k.upMax;
			if (direction == 1) { punch.y += lat; if (punch.y > k.latMax) punch.y = k.latMax; }
			else { punch.y -= lat; if (punch.y < -k.latMax) punch.y = -k.latMax; }
			if (h.Random().Long(0, k.dirChange) == 0) direction = !direction;
		}

		void PlayEmpty(WeaponHost& h)
		{
			h.Sound(IsPistol(id) ? "weapons/dryfire_pistol.wav" : "weapons/dryfire_rifle.wav", 0.8f);
		}

		const char* FireSound(WeaponHost& h) const
		{
			const WeaponInfo& in = I();
			if (id == W_M4A1 && silenced) return "weapons/m4a1-1.wav";
			if (id == W_USP && silenced) return h.Random().Long(0, 1) ? "weapons/usp1.wav" : "weapons/usp2.wav";
			if (id == W_GLOCK18 && burst) return "weapons/glock18-1.wav";
			if (id == W_FAMAS && burst) return "weapons/famas-burst.wav";
			if (in.fire[1] && h.Random().Long(0, 1)) return in.fire[1];
			return in.fire[0];
		}

		// fire anim chosen like the CS client events (random SHOOT, SHOOT_EMPTY on the last round)
		int ShootAnim(WeaponHost& h) const
		{
			Rng& r = h.Random();
			const bool last = clip == 0;
			switch (id)
			{
			case W_USP: return silenced ? (last ? 4 : r.Long(1, 3)) : (last ? 12 : r.Long(9, 11));
			case W_M4A1: return silenced ? r.Long(1, 3) : r.Long(8, 10);
			case W_GLOCK18: return last ? 6 : r.Long(3, 5);
			case W_P228: return last ? 4 : r.Long(1, 3);
			case W_DEAGLE: case W_FIVESEVEN: return last ? 3 : r.Long(1, 2);
			case W_ELITE: return eliteLeft ? (clip <= 1 ? 7 : r.Long(2, 6)) : (clip <= 1 ? 13 : r.Long(8, 12));
			case W_AWP: return r.Long(1, 3);
			case W_SCOUT: case W_G3SG1: case W_SG550: case W_M249: return r.Long(1, 2);
			case W_M3: case W_XM1014: return r.Long(1, 2);
			default: return r.Long(3, 5);
			}
		}

		bool Shoot(float now, WeaponHost& h, float spread, float cycle, float damage, float rangeMod, float sideOffset = 0)
		{
			if (clip <= 0)
			{
				if (fireOnEmpty) { PlayEmpty(h); nextPrimary = 0.2f; }
				return false;
			}
			clip--;
			const Vec3 punch = h.Punch();   // aim with the punch before this shot's kick
			h.FireBullets3(spread, I().distance, I().penetration, I().bullet, damage, rangeMod, punch, sideOffset);
			h.Anim(ShootAnim(h));
			h.Sound(FireSound(h));
			if (!(silenced && (id == W_USP || id == W_M4A1)) && id != W_TMP && id != W_SCOUT) h.MuzzleFlash();
			nextPrimary = nextSecondary = cycle;
			(void)now;
			return true;
		}

		// rifles / SMGs: shotsFired-based accuracy (integer division in the original)
		void RifleAccuracy(int divisor, float base, float cap, bool cubic)
		{
			const int n = shotsFired;
			accuracy = float(cubic ? (n * n * n) / divisor : (n * n) / divisor) + base;
			if (accuracy > cap) accuracy = cap;
		}

		// pistols: time since the last shot
		void PistolAccuracy(float now, float window, float factor, float lo, float hi)
		{
			if (lastFire != 0)
			{
				accuracy -= (window - (now - lastFire)) * factor;
				if (accuracy > hi) accuracy = hi;
				else if (accuracy < lo) accuracy = lo;
			}
			lastFire = now;
		}

		// spread selection (air / moving over `runSpeed` / ducking / standing)
		float Spread4(WeaponHost& h, float air, float run, float duck, float stand, float runSpeed = 140.0f, bool hasDuck = true)
		{
			if (!h.OnGround()) return air;
			if (h.Speed2D() > runSpeed) return run;
			if (hasDuck && h.Ducking()) return duck;
			return stand;
		}

		// ---- primary attacks -----------------------------------------------------------
		void PrimaryAttack(float now, WeaponHost& h)
		{
			const float a = accuracy;
			switch (id)
			{
			case W_AK47:
			{
				const float s = Spread4(h, 0.04f + 0.4f * a, 0.04f + 0.07f * a, 0.0275f * a, 0.0275f * a);
				delayFire = true; shotsFired++; RifleAccuracy(200, 0.35f, 1.25f, true);
				if (!Shoot(now, h, s, 0.0955f, 36, 0.98f)) return;
				timeIdle = 1.9f;
				RifleKick(h, { 1.5f, 0.45f, 0.225f, 0.05f, 6.5f, 2.5f, 7 }, { 2.0f, 1.0f, 0.5f, 0.35f, 9.0f, 6.0f, 5 },
					{ 0.9f, 0.35f, 0.15f, 0.025f, 5.5f, 1.5f, 9 }, { 1.0f, 0.375f, 0.175f, 0.0375f, 5.75f, 1.75f, 8 });
				break;
			}
			case W_M4A1:
			{
				const float st = silenced ? 0.025f : 0.02f;
				const float s = Spread4(h, 0.035f + 0.4f * a, 0.035f + 0.07f * a, st * a, st * a);
				delayFire = true; shotsFired++; RifleAccuracy(220, 0.3f, 1.0f, true);
				if (!Shoot(now, h, s, 0.0875f, silenced ? 33.0f : 32.0f, silenced ? 0.95f : 0.97f)) return;
				timeIdle = 1.5f;
				RifleKick(h, { 1.0f, 0.45f, 0.28f, 0.045f, 3.75f, 3.0f, 7 }, { 1.2f, 0.5f, 0.23f, 0.15f, 5.5f, 3.5f, 6 },
					{ 0.6f, 0.3f, 0.2f, 0.0125f, 3.25f, 2.0f, 7 }, { 0.65f, 0.35f, 0.25f, 0.015f, 3.5f, 2.25f, 7 });
				break;
			}
			case W_AUG: case W_SG552:
			{
				const bool aug = id == W_AUG;
				const float s = aug ? Spread4(h, 0.035f + 0.4f * a, 0.035f + 0.07f * a, 0.02f * a, 0.02f * a)
					: Spread4(h, 0.035f + 0.45f * a, 0.035f + 0.075f * a, 0.02f * a, 0.02f * a);
				delayFire = true; shotsFired++; RifleAccuracy(aug ? 215 : 220, 0.3f, 1.0f, true);
				if (!Shoot(now, h, s, h.Fov() == 90 ? 0.0825f : 0.135f, aug ? 32.0f : 33.0f, aug ? 0.96f : 0.955f)) return;
				timeIdle = aug ? 1.9f : 2.0f;
				if (aug)
					RifleKick(h, { 1.0f, 0.45f, 0.275f, 0.05f, 4.0f, 2.5f, 7 }, { 1.25f, 0.45f, 0.22f, 0.18f, 5.5f, 4.0f, 5 },
						{ 0.575f, 0.325f, 0.2f, 0.011f, 3.25f, 2.0f, 8 }, { 0.625f, 0.375f, 0.25f, 0.0125f, 3.5f, 2.25f, 8 });
				else
					RifleKick(h, { 1.0f, 0.45f, 0.28f, 0.04f, 4.25f, 2.5f, 7 }, { 1.25f, 0.45f, 0.22f, 0.18f, 6.0f, 4.0f, 5 },
						{ 0.6f, 0.35f, 0.2f, 0.0125f, 3.7f, 2.0f, 10 }, { 0.625f, 0.375f, 0.25f, 0.0125f, 4.0f, 2.25f, 9 });
				break;
			}
			case W_GALIL:
			{
				const float s = Spread4(h, 0.04f + 0.3f * a, 0.04f + 0.07f * a, 0.0375f * a, 0.0375f * a);
				delayFire = true; shotsFired++; RifleAccuracy(200, 0.35f, 1.25f, true);
				if (!Shoot(now, h, s, 0.0875f, 30, 0.98f)) return;
				timeIdle = 1.28f;
				RifleKick(h, { 1.0f, 0.45f, 0.28f, 0.045f, 3.75f, 3.0f, 7 }, { 1.2f, 0.5f, 0.23f, 0.15f, 5.5f, 3.5f, 6 },
					{ 0.6f, 0.3f, 0.2f, 0.0125f, 3.25f, 2.0f, 7 }, { 0.65f, 0.35f, 0.25f, 0.015f, 3.5f, 2.25f, 7 });
				break;
			}
			case W_FAMAS:
			{
				float s = Spread4(h, 0.030f + 0.3f * a, 0.030f + 0.07f * a, 0.02f * a, 0.02f * a);
				if (!burst) s += 0.01f;
				delayFire = true; shotsFired++; RifleAccuracy(215, 0.3f, 1.0f, true);
				if (burst) burstShots = 0;
				if (!Shoot(now, h, s, burst ? 0.55f : 0.0825f, burst ? 34.0f : 30.0f, 0.96f)) return;
				timeIdle = 1.1f;
				RifleKick(h, { 1.0f, 0.45f, 0.275f, 0.05f, 4.0f, 2.5f, 7 }, { 1.25f, 0.45f, 0.22f, 0.18f, 5.5f, 4.0f, 5 },
					{ 0.575f, 0.325f, 0.2f, 0.011f, 3.25f, 2.0f, 8 }, { 0.625f, 0.375f, 0.25f, 0.0125f, 3.5f, 2.25f, 8 });
				if (burst) { burstShots = 1; burstSpread = s; famasShoot = now + 0.05f; }
				break;
			}
			case W_MAC10: SmgFire(now, h, 0.375f * a, 0.03f * a, 0.07f, 29, 0.82f, 200, 0.6f, 1.65f, true, 2.0f,
				{ 1.3f, 0.55f, 0.4f, 0.05f, 4.75f, 3.75f, 5 }, { 0.9f, 0.45f, 0.25f, 0.035f, 3.5f, 2.75f, 7 },
				{ 0.75f, 0.4f, 0.175f, 0.03f, 2.75f, 2.5f, 10 }, { 0.775f, 0.425f, 0.2f, 0.03f, 3.0f, 2.75f, 9 }); break;
			case W_TMP: SmgFire(now, h, 0.25f * a, 0.03f * a, 0.07f, 20, 0.85f, 200, 0.55f, 1.4f, true, 2.0f,
				{ 1.1f, 0.5f, 0.35f, 0.045f, 4.5f, 3.5f, 6 }, { 0.8f, 0.4f, 0.2f, 0.03f, 3.0f, 2.5f, 7 },
				{ 0.7f, 0.35f, 0.125f, 0.025f, 2.5f, 2.0f, 10 }, { 0.725f, 0.375f, 0.15f, 0.025f, 2.75f, 2.25f, 9 }); break;
			case W_MP5N:
			{
				const float s = !h.OnGround() ? 0.2f * a : 0.04f * a;
				delayFire = true; shotsFired++;
				accuracy = float(shotsFired * shotsFired) / 220.1f + 0.45f;   // real division
				if (accuracy > 0.75f) accuracy = 0.75f;
				if (!Shoot(now, h, s, 0.075f, 26, 0.84f)) return;
				timeIdle = 2.0f;
				SmgKick(h, { 0.9f, 0.475f, 0.35f, 0.0425f, 5.0f, 3.0f, 6 }, { 0.5f, 0.275f, 0.2f, 0.03f, 3.0f, 2.0f, 10 },
					{ 0.225f, 0.15f, 0.1f, 0.015f, 2.0f, 1.0f, 10 }, { 0.25f, 0.175f, 0.125f, 0.02f, 2.25f, 1.25f, 10 });
				break;
			}
			case W_UMP45: SmgFire(now, h, 0.24f * a, 0.04f * a, 0.1f, 30, 0.82f, 210, 0.5f, 1.0f, false, 2.0f,
				{ 0.125f, 0.65f, 0.55f, 0.0475f, 5.5f, 4.0f, 10 }, { 0.55f, 0.3f, 0.225f, 0.03f, 3.5f, 2.5f, 10 },
				{ 0.25f, 0.175f, 0.125f, 0.02f, 2.25f, 1.25f, 10 }, { 0.275f, 0.2f, 0.15f, 0.0225f, 2.5f, 1.5f, 10 }); break;
			case W_P90:
			{
				const float s = !h.OnGround() ? 0.3f * a : (h.Speed2D() > 170 ? 0.115f * a : 0.045f * a);
				delayFire = true; shotsFired++; RifleAccuracy(175, 0.45f, 1.0f, false);
				if (!Shoot(now, h, s, 0.066f, 21, 0.885f)) return;
				timeIdle = 2.0f;
				SmgKick(h, { 0.9f, 0.45f, 0.35f, 0.04f, 5.25f, 3.5f, 4 }, { 0.45f, 0.3f, 0.2f, 0.0275f, 4.0f, 2.25f, 7 },
					{ 0.275f, 0.2f, 0.125f, 0.02f, 3.0f, 1.0f, 9 }, { 0.3f, 0.225f, 0.125f, 0.02f, 3.25f, 1.25f, 8 });
				break;
			}
			case W_M249:
			{
				const float s = Spread4(h, 0.045f + 0.5f * a, 0.045f + 0.095f * a, 0.03f * a, 0.03f * a);
				delayFire = true; shotsFired++; RifleAccuracy(175, 0.4f, 0.9f, true);
				if (!Shoot(now, h, s, 0.1f, 32, 0.97f)) return;
				timeIdle = 1.6f;
				SmgKick(h, { 1.8f, 0.65f, 0.45f, 0.125f, 5.0f, 3.5f, 8 }, { 1.1f, 0.5f, 0.3f, 0.06f, 4.0f, 3.0f, 8 },
					{ 0.75f, 0.325f, 0.25f, 0.025f, 3.5f, 2.5f, 9 }, { 0.8f, 0.35f, 0.3f, 0.03f, 3.75f, 3.0f, 9 });
				break;
			}
			case W_AWP: case W_SCOUT:
			{
				const bool awp = id == W_AWP;
				float s;
				if (!h.OnGround()) s = awp ? 0.85f : 0.2f;
				else if (h.Speed2D() > (awp ? 140.0f : 170.0f)) s = awp ? 0.25f : 0.075f;
				else if (awp && h.Speed2D() > 10) s = 0.1f;
				else if (h.Ducking()) s = 0.0f;
				else s = awp ? 0.001f : 0.007f;
				if (h.Fov() == 90) s += awp ? 0.08f : 0.025f;
				if (clip > 0 && h.Fov() != 90) { resumeZoom = true; lastZoom = h.Fov(); h.Fov() = 90; }
				if (!Shoot(now, h, s, I().cycle, I().damage, I().rangeMod)) return;
				timeIdle = awp ? 2.0f : 1.8f;
				h.Punch().x -= 2.0f;
				break;
			}
			case W_G3SG1: case W_SG550:
			{
				const bool g3 = id == W_G3SG1;
				float s;
				if (g3)
				{
					s = !h.OnGround() ? 0.45f : (h.Speed2D() > 0 ? 0.15f : (h.Ducking() ? 0.035f : 0.055f));
					if (h.Fov() == 90) s += 0.025f;
					// time-based accuracy, applied to this shot (after the update)
					if (lastFire != 0) { accuracy = (now - lastFire) * 0.3f + 0.55f; if (accuracy > 0.98f) accuracy = 0.98f; }
					else accuracy = 0.98f;
					lastFire = now;
					s *= (1.0f - accuracy);
				}
				else
				{
					const float k = 1.0f - accuracy;
					s = !h.OnGround() ? 0.45f * k : (h.Speed2D() > 0 ? 0.15f : (h.Ducking() ? 0.04f * k : 0.05f * k));
					if (h.Fov() == 90) s += 0.025f;
					if (lastFire != 0) { accuracy = (now - lastFire) * 0.35f + 0.65f; if (accuracy > 0.98f) accuracy = 0.98f; }
					lastFire = now;
				}
				if (!Shoot(now, h, s, 0.25f, I().damage, 0.98f)) return;
				timeIdle = 1.8f;
				Vec3& p = h.Punch();
				p.x -= h.Random().Float(0.75f, g3 ? 1.75f : 1.25f) + p.x * 0.25f;
				p.y += h.Random().Float(-0.75f, 0.75f);
				break;
			}
			case W_M3: case W_XM1014: ShotgunFire(now, h); break;
			case W_GLOCK18:
			{
				float s;
				if (burst)
					s = !h.OnGround() ? 1.2f * (1 - a) : (h.Speed2D() > 0 ? 0.185f * (1 - a) : (h.Ducking() ? 0.095f * (1 - a) : 0.3f * (1 - a)));
				else
					s = !h.OnGround() ? 1.0f * (1 - a) : (h.Speed2D() > 0 ? 0.165f * (1 - a) : (h.Ducking() ? 0.075f * (1 - a) : 0.1f * (1 - a)));
				const float cycle = burst ? 0.5f : 0.15f;
				if (!burst) { if (++shotsFired > 1) return; }
				else burstShots = 0;
				PistolAccuracy(now, 0.325f, 0.275f, 0.6f, 0.9f);
				if (!Shoot(now, h, s, cycle, 25, 0.75f)) return;
				timeIdle = 2.5f;
				if (burst) { burstShots = 1; glockShoot = now + 0.1f; }
				break;
			}
			case W_USP:
			{
				float s;
				if (silenced) s = PistolSpread(h, 1.3f, 0.25f, 0.125f, 0.15f);
				else s = PistolSpread(h, 1.2f, 0.225f, 0.08f, 0.1f);
				if (++shotsFired > 1) return;
				PistolAccuracy(now, 0.3f, 0.275f, 0.6f, 0.92f);
				if (!Shoot(now, h, s, 0.15f, silenced ? 30.0f : 34.0f, 0.79f)) return;
				timeIdle = 2.0f;
				h.Punch().x -= 2.0f;
				break;
			}
			case W_P228: PistolFire(now, h, PistolSpread(h, 1.5f, 0.255f, 0.075f, 0.15f), 0.325f, 0.3f, 0.6f, 0.9f, 32, 0.8f, 2.0f); break;
			case W_DEAGLE: PistolFire(now, h, PistolSpread(h, 1.5f, 0.25f, 0.115f, 0.13f), 0.4f, 0.35f, 0.55f, 0.9f, 54, 0.81f, 1.8f, 0.225f); break;
			case W_FIVESEVEN: PistolFire(now, h, PistolSpread(h, 1.5f, 0.255f, 0.075f, 0.15f), 0.275f, 0.25f, 0.725f, 0.92f, 20, 0.885f, 2.0f); break;
			case W_ELITE:
			{
				const float s = PistolSpread(h, 1.3f, 0.175f, 0.08f, 0.1f);
				if (++shotsFired > 1) return;
				PistolAccuracy(now, 0.325f, 0.275f, 0.55f, 0.88f);
				const float side = eliteLeft ? -5.0f : 5.0f;
				if (!Shoot(now, h, s, 0.075f, 36, 0.75f, side)) return;
				eliteLeft = !eliteLeft;
				timeIdle = 2.0f;
				h.Punch().x -= 2.0f;
				break;
			}
			case W_KNIFE: KnifeSwing(h); break;
			case W_HEGRENADE: case W_FLASHBANG: case W_SMOKEGRENADE:
				if (startThrow == 0 && count > 0)
				{
					releaseThrow = 0;
					startThrow = now;
					h.Anim(1);   // PULLPIN
					timeIdle = 0.5f;
				}
				break;
			case W_C4: C4Primary(now, h); break;
			default: break;
			}
		}

		float PistolSpread(WeaponHost& h, float air, float move, float duck, float stand)
		{
			const float k = 1.0f - accuracy;
			if (!h.OnGround()) return air * k;
			if (h.Speed2D() > 0) return move * k;
			if (h.Ducking()) return duck * k;
			return stand * k;
		}

		void PistolFire(float now, WeaponHost& h, float spread, float window, float factor, float lo, float hi, float damage,
			float rangeMod, float idle, float cycle = 0.15f)
		{
			if (++shotsFired > 1) return;
			PistolAccuracy(now, window, factor, lo, hi);
			if (!Shoot(now, h, spread, cycle, damage, rangeMod)) return;
			timeIdle = idle;
			h.Punch().x -= 2.0f;
		}

		// assault rifles: moving first, then air, duck, stand
		void RifleKick(WeaponHost& h, const Kick& move, const Kick& air, const Kick& duck, const Kick& stand)
		{
			if (h.Speed2D() > 0) KickBack(move, h);
			else if (!h.OnGround()) KickBack(air, h);
			else if (h.Ducking()) KickBack(duck, h);
			else KickBack(stand, h);
		}

		// SMGs / M249: air first, then moving, duck, stand
		void SmgKick(WeaponHost& h, const Kick& air, const Kick& move, const Kick& duck, const Kick& stand)
		{
			if (!h.OnGround()) KickBack(air, h);
			else if (h.Speed2D() > 0) KickBack(move, h);
			else if (h.Ducking()) KickBack(duck, h);
			else KickBack(stand, h);
		}

		void SmgFire(float now, WeaponHost& h, float airSpread, float spread, float cycle, float damage, float rangeMod, int div,
			float base, float cap, bool cubic, float idle, const Kick& air, const Kick& move, const Kick& duck, const Kick& stand)
		{
			const float s = !h.OnGround() ? airSpread : spread;
			delayFire = true; shotsFired++; RifleAccuracy(div, base, cap, cubic);
			if (!Shoot(now, h, s, cycle, damage, rangeMod)) return;
			timeIdle = idle;
			SmgKick(h, air, move, duck, stand);
		}

		void ShotgunFire(float now, WeaponHost& h)
		{
			const bool m3 = id == W_M3;
			if (clip <= 0)
			{
				Reload(h);
				if (!clip) PlayEmpty(h);
				nextPrimary = 1.0f;
				return;
			}
			clip--;
			const Vec3 punch = h.Punch();
			h.FireBuckshot(m3 ? 9 : 6, m3 ? 0.0675f : 0.0725f, m3 ? 3000.0f : 3048.0f, punch);
			h.Anim(ShootAnim(h));
			h.Sound(FireSound(h));
			h.MuzzleFlash();
			if (m3) { nextPrimary = nextSecondary = 0.875f; timeIdle = clip ? 2.5f : 0.875f; }
			else { nextPrimary = nextSecondary = 0.25f; timeIdle = clip ? 2.25f : 0.75f; }
			specialReload = 0;
			if (h.OnGround()) h.Punch().x -= (float)h.Random().Long(m3 ? 4 : 3, m3 ? 6 : 5);
			else h.Punch().x -= (float)h.Random().Long(m3 ? 8 : 7, m3 ? 11 : 10);
			(void)now;
		}

		void FireRemaining(float now, WeaponHost& h, bool glock)
		{
			if (--clip < 0)
			{
				clip = 0;
				burstShots = 3;
				if (glock) glockShoot = 0; else famasShoot = 0;
				return;
			}
			const Vec3 punch = h.Punch();
			if (glock) h.FireBullets3(0.05f, 8192, 1, BulletType::Mm9, 25, 0.9f, punch, 0);
			else h.FireBullets3(burstSpread, 8192, 2, BulletType::Mm556, 30, 0.96f, punch, 0);
			h.Anim(ShootAnim(h));
			h.MuzzleFlash();
			if (++burstShots != 3)
			{
				if (glock) glockShoot = now + 0.1f; else famasShoot = now + 0.1f;
			}
			else
			{
				if (glock) glockShoot = 0; else famasShoot = 0;
			}
		}

		// ---- secondary attacks -------------------------------------------------------
		void SecondaryAttack(float now, WeaponHost& h)
		{
			(void)now;
			switch (id)
			{
			case W_M4A1:
				silenced = !silenced;
				h.Anim(silenced ? 6 : 13);
				h.Sound(silenced ? "weapons/m4a1_silencer_on.wav" : "weapons/m4a1_silencer_off.wav");
				timeIdle = nextSecondary = 2.0f;
				nextPrimary = 2.0f;
				break;
			case W_USP:
				silenced = !silenced;
				h.Anim(silenced ? 7 : 15);
				h.Sound(silenced ? "weapons/usp_silencer_on.wav" : "weapons/usp_silencer_off.wav");
				nextSecondary = timeIdle = 3.0f;
				nextPrimary = 3.0f;
				break;
			case W_FAMAS: case W_GLOCK18:
				burst = !burst;
				h.Message(burst ? "Mode: burst" : (id == W_FAMAS ? "Mode: automatic" : "Mode: semi-automatic"));
				nextSecondary = 0.3f;
				break;
			case W_AUG: case W_SG552:
				h.Fov() = h.Fov() == 90 ? 55 : 90;
				nextSecondary = 0.3f;
				break;
			case W_AWP:
				h.Fov() = h.Fov() == 90 ? 40 : (h.Fov() == 40 ? 10 : 90);
				h.Sound("weapons/zoom.wav", 0.2f);
				nextSecondary = 0.3f;
				break;
			case W_SCOUT: case W_G3SG1: case W_SG550:
				h.Fov() = h.Fov() == 90 ? 40 : (h.Fov() == 40 ? 15 : 90);
				h.Sound("weapons/zoom.wav", 0.2f);
				nextSecondary = 0.3f;
				break;
			case W_KNIFE: KnifeStab(h); break;
			default: break;
			}
		}

		// ---- reload ------------------------------------------------------------------
		void Reload(WeaponHost& h)
		{
			if (IsShotgun(id)) { ShotgunReload(h); return; }
			int& reserve = h.AmmoReserve(I().ammo);
			if (reserve <= 0) return;
			int j = I().maxClip - clip;
			if (j > reserve) j = reserve;
			if (j <= 0) return;
			if (HasZoom(id) && h.Fov() != 90) h.Fov() = 90;
			h.NextAttack() = I().reload;
			int anim = 1;
			switch (id)
			{
			case W_USP: anim = silenced ? 5 : 13; break;
			case W_M4A1: anim = silenced ? 4 : 11; break;
			case W_GLOCK18: anim = h.Random().Long(0, 1) ? 7 : 12; break;
			case W_AWP: anim = 4; break;
			case W_SCOUT: case W_G3SG1: case W_SG550: case W_M249: anim = 3; break;
			case W_DEAGLE: case W_FIVESEVEN: anim = 4; break;
			case W_P228: anim = 5; break;
			case W_ELITE: anim = 14; break;
			default: anim = 1; break;
			}
			h.Anim(anim);
			inReload = true;
			timeIdle = I().reload + 0.5f;
			switch (id)
			{
			case W_AK47: case W_M4A1: case W_SG552: case W_GALIL: case W_P90: case W_TMP: case W_M249: case W_G3SG1:
				accuracy = 0.2f; break;
			case W_AUG: case W_FAMAS: case W_MAC10: case W_MP5N: case W_UMP45: accuracy = 0; break;
			case W_GLOCK18: case W_P228: case W_DEAGLE: accuracy = 0.9f; break;
			case W_USP: case W_FIVESEVEN: accuracy = 0.92f; break;
			case W_ELITE: accuracy = 0.88f; break;
			default: break;
			}
			shotsFired = 0;
			delayFire = false;
		}

		void ShotgunReload(WeaponHost& h)
		{
			const bool m3 = id == W_M3;
			int& reserve = h.AmmoReserve(I().ammo);
			if (reserve <= 0 || clip == I().maxClip) return;
			if (nextPrimary > 0) return;
			if (specialReload == 0)
			{
				h.Anim(5);   // START_RELOAD
				specialReload = 1;
				nextSecondary = timeIdle = h.NextAttack() = 0.55f;
				nextPrimary = 0.55f;
			}
			else if (specialReload == 1)
			{
				if (timeIdle > 0) return;
				specialReload = 2;
				if (m3) h.Sound("weapons/m3_insertshell.wav");
				else h.Sound(h.Random().Long(0, 1) ? "weapons/reload1.wav" : "weapons/reload3.wav");
				h.Anim(3);   // RELOAD (one shell)
				timeIdle = m3 ? 0.45f : 0.3f;
			}
			else
			{
				clip++;
				reserve--;
				specialReload = 1;
			}
		}

		// ---- idle -----------------------------------------------------------------
		void WeaponIdle(float now, WeaponHost& h)
		{
			if (IsGrenade(id)) { GrenadeIdle(now, h); return; }
			if (id == W_C4) { C4Idle(h); return; }
			if (IsShotgun(id))
			{
				if (timeIdle < 0)
				{
					if (clip == 0 && specialReload == 0 && h.AmmoReserve(I().ammo)) Reload(h);
					else if (specialReload != 0)
					{
						if (clip != I().maxClip && h.AmmoReserve(I().ammo)) Reload(h);
						else { h.Anim(4); specialReload = 0; timeIdle = 1.5f; }   // PUMP
					}
					else { h.Anim(0); timeIdle = 20.0f; }
				}
				return;
			}
			if (timeIdle > 0) return;
			switch (id)
			{
			case W_GLOCK18:
			{
				if (!clip) return;
				const float r = h.Random().Float(0, 1);
				if (r <= 0.3f) { h.Anim(2); timeIdle = 3.0625f; }
				else if (r <= 0.6f) { h.Anim(0); timeIdle = 3.75f; }
				else { h.Anim(1); timeIdle = 2.5f; }
				return;
			}
			case W_USP: if (clip) { h.Anim(silenced ? 0 : 8); timeIdle = 60.0f; } return;
			case W_M4A1: h.Anim(silenced ? 0 : 7); timeIdle = 20.0f; return;
			case W_AWP: case W_SCOUT: case W_G3SG1: case W_SG550: if (clip) { h.Anim(0); timeIdle = 60.0f; } return;
			case W_DEAGLE: timeIdle = 20.0f; return;   // original never sends DEAGLE_IDLE1
			case W_P228: case W_FIVESEVEN: if (clip) { h.Anim(0); timeIdle = 3.0625f; } return;
			case W_ELITE: if (clip) { h.Anim(clip == 1 ? 1 : 0); timeIdle = 60.0f; } return;
			default: h.Anim(0); timeIdle = 20.0f; return;
			}
		}

		// ---- knife -----------------------------------------------------------------
		void KnifeSwing(WeaponHost& h)
		{
			const KnifeResult r = h.KnifeAttack(48.0f, 15.0f, false);
			h.Anim((swing++ % 2) ? 7 : 6);   // MIDATTACK2HIT / MIDATTACK1HIT
			if (r == KNIFE_MISS)
			{
				nextPrimary = 0.35f; nextSecondary = 0.5f;
				h.Sound(h.Random().Long(0, 1) ? "weapons/knife_slash1.wav" : "weapons/knife_slash2.wav");
			}
			else
			{
				nextPrimary = 0.4f; nextSecondary = 0.5f;
				if (r == KNIFE_BODY)
				{
					static const char* kHit[] = { "weapons/knife_hit1.wav", "weapons/knife_hit2.wav", "weapons/knife_hit3.wav", "weapons/knife_hit4.wav" };
					h.Sound(kHit[h.Random().Long(0, 3)]);
				}
				else h.Sound("weapons/knife_hitwall1.wav");
			}
			timeIdle = 2.0f;
		}

		void KnifeStab(WeaponHost& h)
		{
			const KnifeResult r = h.KnifeAttack(32.0f, 65.0f, true);
			if (r == KNIFE_MISS)
			{
				h.Anim(5);   // STABMISS
				nextPrimary = nextSecondary = 1.0f;
				h.Sound(h.Random().Long(0, 1) ? "weapons/knife_slash1.wav" : "weapons/knife_slash2.wav");
			}
			else
			{
				h.Anim(4);   // STABHIT
				nextPrimary = nextSecondary = 1.1f;
				h.Sound(r == KNIFE_BODY ? "weapons/knife_stab.wav" : "weapons/knife_hitwall1.wav");
			}
		}

		// ---- grenades --------------------------------------------------------------
		void GrenadeIdle(float now, WeaponHost& h)
		{
			if (id == W_SMOKEGRENADE) { if (releaseThrow == 0) releaseThrow = now; }
			else if (releaseThrow == 0 && startThrow != 0) releaseThrow = now;
			if (timeIdle > 0) return;
			if (startThrow != 0)
			{
				h.Sound("radio/ct_fireinhole.wav");
				h.ThrowGrenade(id == W_HEGRENADE ? GREN_HE : (id == W_FLASHBANG ? GREN_FLASH : GREN_SMOKE), h.Punch());
				h.Anim(2);   // THROW
				startThrow = 0;
				nextPrimary = 0.5f;
				timeIdle = 0.75f;
				if (--count <= 0) timeIdle = nextSecondary = nextPrimary = 0.5f;
			}
			else if (releaseThrow > 0)
			{
				startThrow = 0;
				if (id != W_FLASHBANG && count > 0)
				{
					h.Anim(3);   // DRAW
					timeIdle = h.Random().Float(10, 15);
					releaseThrow = -1;
				}
				else
				{
					releaseThrow = -1;
					h.Retire();
				}
			}
			else if (count > 0)
			{
				h.Anim(0);
				timeIdle = h.Random().Float(10, 15);
			}
		}

		// ---- C4 --------------------------------------------------------------------
		void C4Primary(float now, WeaponHost& h)
		{
			const bool zone = h.InBombZone(), ground = h.OnGround();
			if (!startedArming)
			{
				if (!zone) { h.Message("Plant the bomb at a bombsite (A/B)"); nextPrimary = 1.0f; return; }
				if (!ground) { h.Message("You must be on the ground to plant"); nextPrimary = 1.0f; return; }
				startedArming = true;
				placedAnim = false;
				armedTime = now + 3.0f;
				h.Anim(3);   // ARM
				h.Freeze(true);
				h.Progress(3.0f);
			}
			else if (zone && ground)
			{
				if (armedTime <= now)
				{
					startedArming = false;
					armedTime = 0;
					h.Freeze(false);
					h.Progress(0);
					h.PlantBomb();
					count = 0;
					h.Retire();
					return;
				}
				if (armedTime - 0.75f <= now && !placedAnim) { placedAnim = true; h.Anim(2); }   // DROP
			}
			else
			{
				h.Message(zone ? "You must be on the ground to plant" : "Planting cancelled");
				startedArming = false;
				nextPrimary = 1.5f;
				h.Freeze(false);
				h.Progress(0);
				h.Anim(placedAnim ? 1 : 0);
				return;
			}
			nextPrimary = 0.3f;
			timeIdle = h.Random().Float(10, 15);
		}

		void C4Idle(WeaponHost& h)
		{
			if (startedArming)
			{
				startedArming = false;
				h.Freeze(false);
				nextPrimary = 1.0f;
				h.Progress(0);
				h.Anim(placedAnim ? 1 : 0);
				return;
			}
			if (timeIdle <= 0)
			{
				if (count <= 0) { h.Retire(); return; }
				h.Anim(0);
				timeIdle = 20.0f;
			}
		}
	};

	// Punch angle decay (the view returns to the aim point after recoil).
	inline void DropPunchAngle(Vec3& punch, float dt)
	{
		float len = Length(punch);
		if (len <= 0) return;
		const Vec3 dir = punch * (1.0f / len);
		len -= (10.0f + len * 0.5f) * dt;
		if (len < 0) len = 0;
		punch = dir * len;
	}

	// Shot direction: forward of (view + punch) plus the spread offset along right/up.
	inline Vec3 ShotDirection(float pitch, float yaw, const Vec3& punch, float spread, Rng& rng)
	{
		Vec3 f, r, u;
		AngleVectors(pitch + punch.x, yaw + punch.y, &f, &r, &u);
		const float x = rng.Float(-0.5f, 0.5f) + rng.Float(-0.5f, 0.5f);
		const float y = rng.Float(-0.5f, 0.5f) + rng.Float(-0.5f, 0.5f);
		return f + r * (x * spread) + u * (y * spread);
	}

	// Shotgun pellet: circular rejection sampling (FireBullets).
	inline Vec3 PelletDirection(float pitch, float yaw, const Vec3& punch, float cone, Rng& rng)
	{
		Vec3 f, r, u;
		AngleVectors(pitch + punch.x, yaw + punch.y, &f, &r, &u);
		float x, y;
		do
		{
			x = rng.Float(-0.5f, 0.5f) + rng.Float(-0.5f, 0.5f);
			y = rng.Float(-0.5f, 0.5f) + rng.Float(-0.5f, 0.5f);
		} while (x * x + y * y > 1);
		return f + r * (x * cone) + u * (y * cone);
	}

	inline float DamageAtDistance(float damage, float rangeModifier, float distance)
	{
		return damage * std::pow(rangeModifier, distance / 500.0f);
	}

	enum class HitGroup { Generic, Head, Chest, Stomach, Arm, Leg };
	inline float HitGroupMultiplier(HitGroup g)
	{
		switch (g)
		{
		case HitGroup::Head: return 4.0f;
		case HitGroup::Stomach: return 1.25f;
		case HitGroup::Leg: return 0.75f;
		default: return 1.0f;
		}
	}
	inline const char* HitGroupName(HitGroup g)
	{
		switch (g)
		{
		case HitGroup::Head: return "glowa";
		case HitGroup::Chest: return "klatka";
		case HitGroup::Stomach: return "brzuch";
		case HitGroup::Arm: return "reka";
		case HitGroup::Leg: return "noga";
		default: return "ogolne";
		}
	}

	// CS TakeDamage armour rule (bullets / knife): ratio 0.5 x weapon armour
	// ratio, bonus 0.5; helmet protects the head, legs are never armoured.
	inline void ApplyCsDamage(int& health, int& armor, bool helmet, float damage, HitGroup group, float weaponArmorRatio,
		bool blast = false)
	{
		const bool armored = armor > 0 && group != HitGroup::Leg && (group != HitGroup::Head || helmet);
		if (armored)
		{
			if (blast)
			{
				// explosion branch: hitgroup generic, bonus 0.5
				float fNew = 0.5f * damage, fArmor = (damage - fNew) * 0.5f;
				if (fArmor > armor) { fNew = damage - armor; armor = 0; }
				else { if (fArmor < 0) fArmor = 1; armor -= (int)fArmor; }
				damage = fNew;
			}
			else
			{
				const float ratio = 0.5f * weaponArmorRatio, bonus = 0.5f;
				float fNew = ratio * damage, fArmor = (damage - fNew) * bonus;
				if (fArmor > armor) { fArmor = armor * (1.0f / bonus); fNew = damage - fArmor; armor = 0; }
				else { if (fArmor < 0) fArmor = 1; armor -= (int)fArmor; }
				damage = fNew;
			}
		}
		health -= (int)damage;
	}

	// Explosion damage (RadiusDamage): linear falloff, no line-of-sight check.
	inline float RadiusDamageAt(float damage, float radius, float distance)
	{
		const float d = damage - distance * (damage / radius);
		return d > 0 ? d : 0;
	}
}
