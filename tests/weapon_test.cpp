// Offline checks: .csvm loading / posing, CS weapon timings / recoil /
// modes, grenades, C4 arming, armour formula and buying.
// Build/run: tests\run_tests.ps1
#define _CRT_SECURE_NO_WARNINGS
#include "../plugin/src/cs/ViewModelData.h"
#include "../plugin/src/cs/Player.h"
#include <cstdio>
#include <cfloat>
#include <algorithm>
#include <string>

using namespace cs;

static int g_failed = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct MockHost : WeaponHost
{
	Inventory inv;
	Vec3 punch;
	int fov = 90;
	float nextAttack = 0, speed = 0;
	bool ground = true, duck = false, zone = true;
	Rng rng;
	int shots = 0, pellets = 0, knifeCalls = 0, grenades = 0, plants = 0, lastAnim = -1;
	float lastSpread = 0, lastDamage = 0;
	bool frozen = false;
	float Speed2D() override { return speed; }
	bool OnGround() override { return ground; }
	bool Ducking() override { return duck; }
	Vec3& Punch() override { return punch; }
	int& Fov() override { return fov; }
	int& AmmoReserve(AmmoType t) override { return inv.ammo[t]; }
	float& NextAttack() override { return nextAttack; }
	Rng& Random() override { return rng; }
	void Anim(int s) override { lastAnim = s; }
	void Sound(const char*, float) override {}
	void FireBullets3(float spread, float, int, BulletType, float damage, float, const Vec3&, float) override { shots++; lastSpread = spread; lastDamage = damage; }
	void FireBuckshot(int n, float, float, const Vec3&) override { pellets += n; shots++; }
	KnifeResult KnifeAttack(float, float, bool) override { knifeCalls++; return KNIFE_MISS; }
	void ThrowGrenade(GrenadeKind, const Vec3&) override { grenades++; }
	bool InBombZone() override { return zone; }
	void PlantBomb() override { plants++; }
	void Message(const char*) override {}
	void Progress(float) override {}
	void Freeze(bool f) override { frozen = f; }
	void Retire() override {}
	void MuzzleFlash() override {}
};

// runs the weapon like the player loop: 100 Hz, timers, ItemPostFrame
static void Run(Weapon& w, MockHost& h, float& t, float seconds, bool attack, bool attack2 = false, bool reload = false)
{
	for (float end = t + seconds; t < end; t += 0.01f)
	{
		h.nextAttack = (std::max)(h.nextAttack - 0.01f, -0.001f);
		w.Tick(0.01f);
		DropPunchAngle(h.punch, 0.01f);
		w.PostFrame(t, attack, attack2, reload, h);
	}
}

int main(int argc, char** argv)
{
	const char* csvm = argc > 1 ? argv[1] : "";
	ViewModelData vm;
	std::string err;
	if (vm.Load(csvm, &err))
	{
		CHECK(vm.sequences.size() == 6 && vm.FindSequence("reload") == 1, "AK47.csvm: %zu sequences, %zu bones, %zu tris",
			vm.sequences.size(), vm.parent.size(), vm.tris.size());
		std::vector<Mat34> bones;
		std::vector<Vec3> v, n;
		vm.Pose(0, 0, bones);
		vm.Skin(bones, v, n);
		float cy = 0;
		for (const Vec3& p : v) cy += p.y;
		CHECK(cy / v.size() < 0, "mirrored to the right hand: mean y %.2f < 0", cy / v.size());
		ViewModelAnim anim;
		anim.Start(vm.FindSequence("reload"));
		std::vector<const VmEvent*> fired;
		for (int i = 0; i < 300; i++) anim.Advance(vm, 0.01f, fired);
		CHECK(fired.size() == 2 && fired[0]->options == "CS_WEAPONS_AK47_CLIPOUT.WAV", "reload sound events: %zu", fired.size());
	}
	else
		printf("skip viewmodel checks: %s (run tools\\convert_cs.py)\n", err.c_str());

	// --- AK-47 full auto ----------------------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_AK47; w.clip = 30; h.inv.ammo[A_762] = 90;
		float t = 0;
		w.Deploy(t, h);
		int before = 0;
		Run(w, h, t, 0.74f, true);
		CHECK(h.shots == 0, "no shot during the 0.75 s draw (%d)", h.shots);
		before = h.shots;
		Run(w, h, t, 4.0f, true);
		CHECK(h.shots == 30 && w.clip == 0, "magazine: %d shots, clip %d", h.shots, w.clip);
		Run(w, h, t, 0.3f, false);
		CHECK(w.inReload, "auto reload after release");
		Run(w, h, t, 2.5f, false);
		CHECK(w.clip == 30 && h.inv.ammo[A_762] == 60, "reloaded: clip %d reserve %d", w.clip, h.inv.ammo[A_762]);
		(void)before;
	}

	// --- AK timing: 30 shots in 29 x 0.0955 ------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_AK47; w.clip = 30;
		float t = 0, first = -1, last = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		for (; t < 5; t += 0.001f)
		{
			h.nextAttack = (std::max)(h.nextAttack - 0.001f, -0.001f);
			w.Tick(0.001f);
			const int s0 = h.shots;
			w.PostFrame(t, true, false, false, h);
			if (h.shots != s0) { if (first < 0) first = t; last = t; }
		}
		CHECK(std::fabs((last - first) - 29 * 0.0955f) < 0.03f, "30 shots in %.3f s (2.770)", last - first);
		CHECK(std::fabs(h.punch.x + 5.75f) < 0.6f, "standing recoil near the 5.75 deg cap (punch %.2f)", h.punch.x);
	}

	// --- Glock burst: 3 shots per click -------------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_GLOCK18; w.clip = 20;
		float t = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		Run(w, h, t, 0.05f, false, true);   // RMB tap: burst mode (held >0.3 s would toggle again)
		Run(w, h, t, 0.3f, false);
		CHECK(w.burst, "glock burst mode on");
		Run(w, h, t, 0.05f, true);
		Run(w, h, t, 0.4f, false);
		CHECK(h.shots == 3 && w.clip == 17, "one click = 3 shots (%d), clip %d", h.shots, w.clip);
	}

	// --- pistol is semi-automatic -----------------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_DEAGLE; w.clip = 7;
		float t = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		Run(w, h, t, 2.0f, true);
		CHECK(h.shots == 1, "deagle held = 1 shot (%d)", h.shots);
	}

	// --- M3 pellets and shell reload --------------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_M3; w.clip = 8; h.inv.ammo[A_BUCK] = 32;
		float t = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		Run(w, h, t, 0.05f, true);
		CHECK(h.pellets == 9, "M3: 9 pellets (%d)", h.pellets);
		Run(w, h, t, 3.0f, false);
		Run(w, h, t, 3.0f, false, false, true);
		CHECK(w.clip == 8 && h.inv.ammo[A_BUCK] == 31, "shell-by-shell reload: clip %d reserve %d", w.clip, h.inv.ammo[A_BUCK]);
	}

	// --- grenade: thrown on release, not before 0.5 s -----------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_HEGRENADE; w.count = 1;
		float t = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		Run(w, h, t, 0.1f, true);
		Run(w, h, t, 0.3f, false);
		CHECK(h.grenades == 0, "not thrown before 0.5 s after the pin pull");
		Run(w, h, t, 0.3f, false);
		CHECK(h.grenades == 1 && w.count == 0, "thrown on release (%d)", h.grenades);
	}

	// --- C4: 3 s arming, cancelled on release ---------------------------------------------
	{
		MockHost h;
		Weapon w; w.id = W_C4; w.count = 1;
		float t = 0;
		w.Deploy(t, h);
		Run(w, h, t, 0.8f, false);
		Run(w, h, t, 1.5f, true);
		CHECK(h.frozen && h.plants == 0, "arming freezes the player");
		Run(w, h, t, 0.1f, false);
		CHECK(!h.frozen && h.plants == 0, "release cancels");
		Run(w, h, t, 1.1f, false);
		Run(w, h, t, 3.4f, true);
		CHECK(h.plants == 1, "planted after holding 3 s (%d)", h.plants);
		h.zone = false;
	}

	// --- armour ------------------------------------------------------------------
	{
		int hp = 100, armor = 100;
		ApplyCsDamage(hp, armor, false, 36 * 4.0f, HitGroup::Head, 1.55f);
		CHECK(hp == 100 - 144, "AK headshot without helmet: hp %d (-44)", hp);
		hp = 100; armor = 100;
		ApplyCsDamage(hp, armor, true, 36 * 4.0f, HitGroup::Head, 1.55f);
		// ratio 0.775: new = 111.6, armor taken (144-111.6)*0.5 = 16.2
		CHECK(hp == 100 - 111 && armor == 84, "AK headshot with helmet: hp %d (-11) armor %d (84)", hp, armor);
		hp = 100; armor = 100;
		ApplyCsDamage(hp, armor, true, 36, HitGroup::Leg, 1.55f);
		CHECK(hp == 64 && armor == 100, "legs are never armoured: hp %d armor %d", hp, armor);
		CHECK(std::fabs(RadiusDamageAt(100, 350, 175) - 50) < 0.01f, "HE at half radius: %.1f", RadiusDamageAt(100, 350, 175));
	}

	// --- buying -------------------------------------------------------------------
	{
		Inventory inv;
		inv.ct = false;
		inv.money = 800;
		WeaponId dropped;
		CHECK(inv.BuyWeapon(W_AK47, dropped) != nullptr, "AK costs 2500: not with $800");
		inv.money = 16000;
		CHECK(inv.BuyWeapon(W_M4A1, dropped) != nullptr, "T cannot buy the M4A1");
		CHECK(inv.BuyWeapon(W_AK47, dropped) == nullptr && inv.money == 13500 && inv.primary.clip == 30, "bought AK: $%d", inv.money);
		CHECK(inv.BuyEquipment(4) == nullptr && inv.BuyEquipment(4) != nullptr, "only one HE grenade");
		CHECK(inv.BuyEquipment(3) == nullptr && inv.BuyEquipment(3) == nullptr && inv.BuyEquipment(3) != nullptr, "two flashbangs max");
		const int m = inv.money;
		CHECK(inv.BuyEquipment(2) == nullptr && inv.money == m - 1000 && inv.helmet, "kevlar + helmet 1000");
		CHECK(inv.BuyFullAmmo(true) == nullptr && inv.ammo[A_762] == 90, "primary ammo to 90 (%d)", inv.ammo[A_762]);
	}

	printf(g_failed ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
