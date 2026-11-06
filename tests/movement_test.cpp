// Offline checks of cs::PlayerMove against known CS 1.6 movement numbers.
// Build/run: tests\run_tests.ps1
// Optional: movement_test.exe <world.obj>  - spawn in a world dump from the
// game (cs_world.obj: HL units, Z-up) and walk a square, printing positions.
#define _CRT_SECURE_NO_WARNINGS
#include "../plugin/src/cs/Movement.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace cs;

static int g_failed = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void Quad(TriWorld& w, Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
	w.AddTri(a, b, c, 0);
	w.AddTri(a, c, d, 0);
}

// axis-aligned solid box made of 12 triangles
static void Box(TriWorld& w, Vec3 mn, Vec3 mx)
{
	Vec3 p[8];
	for (int i = 0; i < 8; i++) p[i] = Vec3(i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z);
	// counter-clockwise seen from outside (one-sided triangles face outwards)
	Quad(w, p[0], p[2], p[3], p[1]); Quad(w, p[4], p[5], p[7], p[6]);
	Quad(w, p[0], p[1], p[5], p[4]); Quad(w, p[2], p[6], p[7], p[3]);
	Quad(w, p[0], p[4], p[6], p[2]); Quad(w, p[1], p[3], p[7], p[5]);
}

static void Floor(TriWorld& w, float size)
{
	Quad(w, Vec3(-size, -size, 0), Vec3(size, -size, 0), Vec3(size, size, 0), Vec3(-size, size, 0));
}

static void Run(PlayerMove& pm, const TriWorld& w, MoveInput in, float seconds, float dt = 0.01f)
{
	for (float t = 0; t < seconds; t += dt) pm.Step(w, in, dt);
}

static bool LoadObj(const char* path, TriWorld& w, Vec3& spawn, bool& hasSpawn)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	std::vector<Vec3> v;
	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		float x, y, z;
		unsigned a, b, c;
		if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) v.push_back(Vec3(x, y, z));
		else if (sscanf(line, "f %u %u %u", &a, &b, &c) == 3 && a && b && c && a <= v.size() && b <= v.size() && c <= v.size())
			w.AddTri(v[a - 1], v[b - 1], v[c - 1], 0);
		else if (sscanf(line, "# spawn %f %f %f", &x, &y, &z) == 3) { spawn = Vec3(x, y, z); hasSpawn = true; }
	}
	fclose(f);
	return true;
}

static int ObjWalk(const char* path)
{
	TriWorld w;
	Vec3 spawn;
	bool hasSpawn = false;
	if (!LoadObj(path, w, spawn, hasSpawn)) { printf("cannot read %s\n", path); return 1; }
	w.Build();
	printf("%s: %d triangles, spawn %s (%.1f %.1f %.1f)\n", path, w.TriCount(), hasSpawn ? "yes" : "no", spawn.x, spawn.y, spawn.z);
	PlayerMove pm;
	pm.Spawn(w, spawn);
	printf("spawned: origin %.1f %.1f %.1f onGround %d\n", pm.origin.x, pm.origin.y, pm.origin.z, pm.onGround);
	MoveInput in;
	in.forward = 1;
	for (int leg = 0; leg < 4; leg++)
	{
		in.yaw = leg * 90.0f;
		Run(pm, w, in, 1.5f);
		printf("leg %d: origin %.1f %.1f %.1f  speed %.1f  onGround %d\n", leg, pm.origin.x, pm.origin.y, pm.origin.z,
			Length2D(pm.velocity), pm.onGround);
	}
	return 0;
}

int main(int argc, char** argv)
{
	if (argc > 1) return ObjWalk(argv[1]);

	// --- flat ground: spawn, run speed, jump height -------------------------
	{
		TriWorld w;
		Floor(w, 4000);
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		CHECK(pm.onGround && std::fabs(pm.origin.z - 36.0f) < 0.1f, "spawn on floor: origin z %.3f (36)", pm.origin.z);

		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 2.0f);
		float speed = Length2D(pm.velocity);
		CHECK(std::fabs(speed - 250.0f) < 1.0f, "knife run speed %.1f (250)", speed);
		CHECK(std::fabs(pm.origin.z - 36.0f) < 0.1f, "stays on floor while running: z %.3f", pm.origin.z);

		// stop: friction 4, stopspeed 75 -> 250 u/s: ln(250/75)/4 + 75/300 = 0.55 s
		in.forward = 0;
		Run(pm, w, in, 0.6f);
		CHECK(Length2D(pm.velocity) < 1.0f, "stops after release: speed %.2f", Length2D(pm.velocity));

		// walk key and duck speed
		in.forward = 1; in.walk = true;
		Run(pm, w, in, 2.0f);
		CHECK(std::fabs(Length2D(pm.velocity) - 208.0f) < 1.5f, "walk key speed %.1f (400*0.52=208)", Length2D(pm.velocity));
		in.walk = false; in.duck = true;
		Run(pm, w, in, 2.0f);
		CHECK(pm.ducked && std::fabs(pm.origin.z - 18.0f) < 0.1f, "ducked hull: origin z %.3f (18)", pm.origin.z);
		CHECK(std::fabs(Length2D(pm.velocity) - 133.2f) < 1.5f, "duck speed %.1f (400*0.333=133.2)", Length2D(pm.velocity));
		in.duck = false;
		Run(pm, w, in, 0.1f);
		CHECK(!pm.ducked && std::fabs(pm.origin.z - 36.0f) < 0.1f, "unduck: origin z %.3f", pm.origin.z);

		// jump from standstill
		pm.velocity = Vec3();
		in = MoveInput{};
		Run(pm, w, in, 1.0f);
		in.jump = true;
		float top = pm.origin.z;
		bool left = false;
		for (int i = 0; i < 200; i++)
		{
			pm.Step(w, in, 0.01f);
			in.jump = false;
			top = std::max(top, pm.origin.z);
			left |= !pm.onGround;
		}
		// 45 units continuous; a 100 Hz step adds v0*dt/2 = 1.34
		CHECK(left && std::fabs((top - pm.Mins().z * -1.0f - 0.031f) - 46.34f) < 0.2f, "jump height %.2f (45 + 1.34 at 100 Hz)", top - 36.031f);
		CHECK(pm.onGround, "landed after jump");

		// holding jump does not re-jump (no autobhop)
		in.jump = true;
		pm.Step(w, in, 0.01f);
		int jumps = 0;
		for (int i = 0; i < 300; i++) { pm.Step(w, in, 0.01f); jumps += pm.justJumped; }
		CHECK(jumps == 0, "held jump key: %d extra jumps (0)", jumps);
	}

	// --- wall ------------------------------------------------------------------
	{
		TriWorld w;
		Floor(w, 4000);
		Box(w, Vec3(500, -1000, 0), Vec3(600, 1000, 300));
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 4.0f);
		CHECK(pm.origin.x <= 500.0f - 16.0f + 0.01f && pm.origin.x > 500.0f - 16.0f - 1.0f, "blocked by wall: x %.3f (484)", pm.origin.x);
		// sliding along it at 45 degrees
		in.yaw = 45;
		float y0 = pm.origin.y;
		Run(pm, w, in, 1.0f);
		CHECK(pm.origin.y - y0 > 100.0f && pm.origin.x <= 484.01f, "slides along wall: dy %.1f x %.3f", pm.origin.y - y0, pm.origin.x);
	}

	// --- steps ---------------------------------------------------------------
	for (float h : { 8.0f, 16.0f, 18.0f, 20.0f, 30.0f })
	{
		TriWorld w;
		Floor(w, 4000);
		Box(w, Vec3(200, -500, 0), Vec3(1000, 500, h));
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 2.0f);
		const bool up = pm.origin.x > 300.0f && std::fabs(pm.origin.z - (36.0f + h)) < 0.2f;
		CHECK(up == (h <= 18.0f), "step %.0f units: %s (x %.1f z %.2f)", h, up ? "climbed" : "blocked", pm.origin.x, pm.origin.z);
	}

	// --- ramps ---------------------------------------------------------------
	for (float deg : { 20.0f, 40.0f, 50.0f })
	{
		TriWorld w;
		Floor(w, 4000);
		const float len = 2000.0f, rise = len * std::tan(deg * kDegToRad);
		Quad(w, Vec3(200, -500, 0), Vec3(200 + len, -500, rise), Vec3(200 + len, 500, rise), Vec3(200, 500, 0));
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 3.0f);
		const bool climbed = pm.origin.z > 136.0f;
		CHECK(climbed == (deg < 45.0f), "ramp %.0f deg: %s (x %.1f z %.1f onGround %d)", deg, climbed ? "walkable" : "too steep",
			pm.origin.x, pm.origin.z, pm.onGround);
	}

	// --- air strafing gains speed --------------------------------------------
	{
		TriWorld w;
		Floor(w, 40000);
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 2.0f);
		in.forward = 0;
		in.jump = true;
		pm.Step(w, in, 0.01f);
		in.jump = false;
		// strafe right while turning right, like a mouse strafe
		in.side = 1;
		float v0 = Length2D(pm.velocity);
		for (int i = 0; i < 50 && !pm.onGround; i++) { in.yaw -= 1.5f; pm.Step(w, in, 0.01f); }
		float v1 = Length2D(pm.velocity);
		CHECK(v1 > v0 + 20.0f, "air strafe: %.1f -> %.1f u/s", v0, v1);
	}

	// --- ladder: pushing into it climbs, jump pushes off ------------------------------
	{
		TriWorld w;
		Floor(w, 4000);
		Ladder l;
		l.mins = Vec3(100, -20, 0); l.maxs = Vec3(108, 20, 1000); l.axis = Vec3(1, 0, 0);
		w.ladders.push_back(l);
		w.Build();
		PlayerMove pm;
		pm.Spawn(w, Vec3(0, 0, 0));
		MoveInput in;
		in.forward = 1;
		Run(pm, w, in, 2.0f);
		CHECK(pm.onLadder && pm.origin.z > 36.0f + 150.0f, "ladder climb: z %.1f onLadder %d", pm.origin.z, pm.onLadder);
		in.forward = 0;
		const float z = pm.origin.z;
		Run(pm, w, in, 0.5f);
		CHECK(std::fabs(pm.origin.z - z) < 0.5f, "hangs on the ladder without input (dz %.2f)", pm.origin.z - z);
		in.jump = true;
		pm.Step(w, in, 0.01f);
		CHECK(pm.velocity.x < -200.0f, "jump pushes off the ladder (vx %.0f)", pm.velocity.x);
	}

	// --- standing on a box corner / trace sanity --------------------------------
	{
		TriWorld w;
		Box(w, Vec3(-100, -100, -100), Vec3(100, 100, 0));
		w.Build();
		TraceResult tr = w.TraceBox(Vec3(0, 0, 100), Vec3(0, 0, -100), HullMins(false), HullMaxs(false));
		CHECK(tr.fraction < 1 && std::fabs(tr.endpos.z - 36.0f) < 0.05f && tr.normal.z > 0.99f, "box trace down: end z %.4f normal z %.2f",
			tr.endpos.z, tr.normal.z);
		tr = w.TraceBox(Vec3(-300, 0, 20), Vec3(300, 0, 20), HullMins(false), HullMaxs(false));
		CHECK(tr.fraction < 1 && std::fabs(tr.endpos.x + 116.0f) < 0.05f && tr.normal.x < -0.99f, "box trace side: end x %.4f", tr.endpos.x);
	}

	printf(g_failed ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
