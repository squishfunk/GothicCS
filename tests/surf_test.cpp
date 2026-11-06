// Offline: movement over a real Gothic world dump (cs_world.obj written on
// entering CS mode). Counts sudden stops (speed drops > 60 % in one 100 Hz
// step) while running, jumping and air strafing in many directions.
// Usage: surf_test.exe <cs_world.obj>
#define _CRT_SECURE_NO_WARNINGS
#define CS_MOVE_TRACE
#include <cstdio>
#include "../plugin/src/cs/Movement.h"
#include <cstdio>
#include <vector>

using namespace cs;

int main(int argc, char** argv)
{
	if (argc < 2) { printf("usage: surf_test <obj>\n"); return 1; }
	FILE* f = fopen(argv[1], "rb");
	if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
	TriWorld w;
	std::vector<Vec3> v;
	Vec3 spawn;
	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		float x, y, z;
		unsigned a, b, c;
		if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) v.push_back(Vec3(x, y, z));
		else if (sscanf(line, "f %u %u %u", &a, &b, &c) == 3) w.AddTri(v[a - 1], v[b - 1], v[c - 1], 0);
		else if (sscanf(line, "# spawn %f %f %f", &x, &y, &z) == 3) spawn = Vec3(x, y, z);
	}
	fclose(f);
	w.Build();
	int up = 0, down = 0;
	for (int t = 0; t < w.TriCount(); t++)
	{
		const Vec3* p = &w.verts[t * 3];
		const Vec3 n = Normalized(Cross(p[1] - p[0], p[2] - p[0]));
		if (n.z > 0.7f) up++;
		if (n.z < -0.7f) down++;
	}
	printf("%d tris: %d facing up, %d facing down\n", w.TriCount(), up, down);

	int stops = 0, steps = 0;
	double dist = 0;
	for (int run = 0; run < 64; run++)
	{
		PlayerMove pm;
		pm.vars.airaccelerate = argc > 2 ? (float)atof(argv[2]) : 100.0f;
		pm.vars.maxvelocity = 3500;
		pm.Spawn(w, spawn);
		MoveInput in;
		in.forward = 1;
		in.yaw = run * (360.0f / 64);
		float prev = 0;
		for (int i = 0; i < 400; i++)
		{
			// jump + air strafe like a player: alternate strafe / turn every 0.4 s while in the air
			in.jump = argc > 3 ? false : (i % 50) == 0;
			const bool air = !pm.onGround;
			const int phase = (i / 40) % 2;
			in.side = air ? (phase ? 1.0f : -1.0f) : 0;
			if (air) in.yaw += phase ? -1.2f : 1.2f;
			const Vec3 before = pm.origin;
			pm.Step(w, in, 0.01f);
			const float sp = Length2D(pm.velocity);
			if (prev > 150 && sp < prev * 0.4f) stops++;
			prev = sp;
			dist += Length2D(pm.origin - before);
			steps++;
		}
	}
	printf("zeroed: startsolid %d, planes %d, crease %d, primal %d\n", Dbg().startsolid, Dbg().planes, Dbg().crease, Dbg().primal);
	printf("airaccelerate %.0f: %d sudden stops in %d steps, %.0f units travelled\n",
		argc > 2 ? atof(argv[2]) : 100.0, stops, steps, dist);
	return 0;
}
