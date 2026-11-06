// GothicCS - CS player inventory, money and buying (engine-independent).
// Rules from THIRD_PARTY_NOTICES.md (ReGameDLL_CS, original CS 1.6).
#pragma once
#include "Weapons.h"
#include <cstdio>

namespace cs
{
	struct Inventory
	{
		Weapon primary, pistol, knife, he, flash, smoke, c4;
		int ammo[A_COUNT] = {};
		int money = START_MONEY;
		int health = 100, armor = 0;
		bool helmet = false, defuser = false, nvg = false;
		Weapon* active = nullptr;
		Weapon* last = nullptr;
		bool ct = true;           // team for the buy menu

		Inventory()
		{
			knife.id = W_KNIFE;
			he.id = W_HEGRENADE; flash.id = W_FLASHBANG; smoke.id = W_SMOKEGRENADE; c4.id = W_C4;
		}

		Weapon* SlotWeapon(Slot s)
		{
			switch (s)
			{
			case SLOT_PRIMARY: return primary.Valid() ? &primary : nullptr;
			case SLOT_PISTOL: return pistol.Valid() ? &pistol : nullptr;
			case SLOT_KNIFE: return &knife;
			case SLOT_GRENADE: return he.count ? &he : (flash.count ? &flash : (smoke.count ? &smoke : nullptr));
			case SLOT_C4: return c4.count ? &c4 : nullptr;
			}
			return nullptr;
		}

		// Next grenade after the active one (key 4 pressed again cycles them).
		Weapon* NextGrenade()
		{
			Weapon* order[3] = { &he, &flash, &smoke };
			int start = 0;
			for (int i = 0; i < 3; i++) if (active == order[i]) start = i + 1;
			for (int k = 0; k < 3; k++)
			{
				Weapon* w = order[(start + k) % 3];
				if (w->count > 0) return w;
			}
			return nullptr;
		}

		// Best weapon to switch to (RetireWeapon): primary, pistol, grenade, knife.
		Weapon* Best(Weapon* except)
		{
			Weapon* order[] = { &primary, &pistol, &he, &flash, &smoke, &knife };
			for (Weapon* w : order)
			{
				if (w == except) continue;
				if (!w->Valid()) continue;
				if (IsGrenade(w->id) && w->count <= 0) continue;
				return w;
			}
			return &knife;
		}

		// A new gun: full clip, no reserve (BuyWeaponByWeaponID / GiveNamedItem).
		void Give(WeaponId id)
		{
			Weapon w;
			w.id = id;
			w.clip = Info(id).maxClip;
			if (Info(id).slot == SLOT_PRIMARY) primary = w;
			else if (Info(id).slot == SLOT_PISTOL) pistol = w;
		}

		// Default round equipment: knife + pistol by team (CT USP, T Glock18), 24/40 reserve as in CS.
		void DefaultLoadout()
		{
			primary = Weapon();
			Give(ct ? W_USP : W_GLOCK18);
			ammo[Info(pistol.id).ammo] = ct ? 24 : 40;
			he.count = flash.count = smoke.count = 0;
			c4.count = 0;
			armor = 0; helmet = false; defuser = false;
			health = 100;
			active = &pistol;
			last = &knife;
		}

		// ---- buying ----------------------------------------------------------------
		// Returns a message (CS titles.txt meaning) or nullptr on success; `bought`
		// is set when something was bought; `dropped` = the weapon that went to the floor.
		const char* BuyWeapon(WeaponId id, WeaponId& dropped)
		{
			dropped = W_NONE;
			const WeaponInfo& in = Info(id);
			if (!(in.team & (ct ? TEAM_CT : TEAM_T))) return "You cannot buy this item";
			if ((in.slot == SLOT_PRIMARY && primary.id == id) || (in.slot == SLOT_PISTOL && pistol.id == id))
				return "You already own this weapon!";
			if (money < in.price) return "Not enough money!";
			if (in.slot == SLOT_PRIMARY && primary.Valid()) dropped = primary.id;
			if (in.slot == SLOT_PISTOL && pistol.Valid()) dropped = pistol.id;
			Give(id);
			money -= in.price;
			return nullptr;
		}

		// One "clip" of ammo (buyammo1/2); false when it could not buy.
		bool BuyAmmoClip(Weapon& w, const char** msg)
		{
			if (!w.Valid() || w.I().ammo == A_NONE) return false;
			const AmmoInfo& a = Ammo(w.I().ammo);
			int& reserve = ammo[w.I().ammo];
			if (reserve >= a.maxCarry) return false;
			if (money < a.price) { *msg = "Not enough money!"; return false; }
			reserve += a.buyAmount;
			if (reserve > a.maxCarry) reserve = a.maxCarry;
			money -= a.price;
			return true;
		}

		// Menu 6 / 7: clips until full.
		const char* BuyFullAmmo(bool primarySlot)
		{
			Weapon& w = primarySlot ? primary : pistol;
			const char* msg = nullptr;
			bool any = false;
			while (BuyAmmoClip(w, &msg)) any = true;
			if (!any && msg) return msg;
			return any ? nullptr : "You cannot carry more ammunition";
		}

		// Equipment menu key (titles.txt CT/T_BuyItem): 1 kevlar, 2 kevlar+helmet,
		// 3 flash, 4 HE, 5 smoke, 6 NVG, 7 defuse kit (CT only).
		const char* BuyEquipment(int key)
		{
			const bool full = armor >= 100;
			switch (key)
			{
			case 1:
				if (full) return "You already have armor!";
				if (money < PRICE_KEVLAR) return "Not enough money!";
				money -= PRICE_KEVLAR; armor = 100;
				return helmet ? "Helmet already owned - purchased armor" : nullptr;
			case 2:
				if (full && helmet) return "You already have armor and a helmet!";
				if (full && !helmet)
				{
					if (money < PRICE_HELMET) return "Not enough money!";
					money -= PRICE_HELMET; helmet = true; return "Armor already owned - purchased helmet";
				}
				{
					const int cost = helmet ? PRICE_KEVLAR : PRICE_ASSAULTSUIT;
					if (money < cost) return "Not enough money!";
					money -= cost; armor = 100; helmet = true;
				}
				return nullptr;
			case 3: return BuyGrenade(flash, MAX_FLASH, PRICE_FLASH);
			case 4: return BuyGrenade(he, MAX_HE, PRICE_HE);
			case 5: return BuyGrenade(smoke, MAX_SMOKE, PRICE_SMOKE);
			case 6:
				if (nvg) return "You already own this!";
				if (money < PRICE_NVG) return "Not enough money!";
				money -= PRICE_NVG; nvg = true; return nullptr;
			case 7:
				if (!ct) return "You cannot buy this item";
				if (defuser) return "You already own this!";
				if (money < PRICE_DEFUSEKIT) return "Not enough money!";
				money -= PRICE_DEFUSEKIT; defuser = true; return nullptr;
			}
			return "You cannot buy this item";
		}

		const char* BuyGrenade(Weapon& g, int max, int price)
		{
			if (g.count >= max) return "You cannot carry more!";
			if (money < price) return "Not enough money!";
			money -= price;
			g.count++;
			return nullptr;
		}

		void AddMoney(int amount)
		{
			money += amount;
			if (money > MAX_MONEY) money = MAX_MONEY;
			if (money < 0) money = 0;
		}
	};
}
