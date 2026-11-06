// GothicCS - Counter-Strike 1.6 style player movement (own implementation).
//
// Written from the publicly documented Quake/Half-Life movement model
// (ground friction with stopspeed, Accelerate / AirAccelerate with the 30 u/s
// air wish cap, half-step gravity, 18-unit step-up, slide along planes) and
// the default CS 1.6 cvar values. No Half-Life SDK (pm_shared) code is used.
// Engine-independent: collision goes through cs::TriWorld only.
#pragma once
#include "Vec.h"
#include "TriWorld.h"
#include <cmath>
#include <algorithm>

namespace cs
{
	// Default CS 1.6 server / client settings (units/s, units/s^2).
	struct MoveVars
	{
		float gravity = 800.0f;        // sv_gravity
		float stopspeed = 75.0f;       // sv_stopspeed
		float maxspeed = 320.0f;       // sv_maxspeed
		float accelerate = 5.0f;       // sv_accelerate
		float airaccelerate = 10.0f;   // sv_airaccelerate
		float friction = 4.0f;         // sv_friction
		float edgefriction = 2.0f;     // sv_edgefriction
		float stepsize = 18.0f;        // sv_stepsize
		float maxvelocity = 2000.0f;   // sv_maxvelocity
		float forwardspeed = 400.0f;   // cl_forwardspeed
		float sidespeed = 400.0f;      // cl_sidespeed
		float movespeedkey = 0.52f;    // cl_movespeedkey (walk key)
		bool autoBunnyhop = false;     // holding jump re-jumps on landing (AMXX bhop plugins: clear IN_JUMP in oldbuttons)
		bool noJumpSlowdown = false;   // AMXX bhop plugins: fuser2 = 0 every frame (no jump stamina)
	};

	// Player hull (CS / HL): 32x32x72 standing, 32x32x36 ducked, origin at
	// the hull centre; eye 17 units above it standing, 12 ducked.
	inline Vec3 HullMins(bool ducked) { return ducked ? Vec3(-16, -16, -18) : Vec3(-16, -16, -36); }
	inline Vec3 HullMaxs(bool ducked) { return ducked ? Vec3(16, 16, 18) : Vec3(16, 16, 36); }
	constexpr float kViewStand = 17.0f, kViewDuck = 12.0f;
	constexpr float kDuckHullDelta = 18.0f;   // origin shift between the two hulls
	constexpr float kTimeToDuck = 0.4f;
	constexpr float kDuckSpeedMul = 0.333f;   // move speed while ducked
	constexpr float kJumpHeight = 45.0f;
	constexpr float kAirWishCap = 30.0f;
	// CS jump stamina: a jump sets it, it drains 1000/s, and while it is up
	// horizontal ground speed and the next jump are scaled down.
	constexpr float kJumpStamina = 1315.789429f;

	// debug counters (offline diagnostics)
	struct MoveDebug { int startsolid = 0, planes = 0, crease = 0, primal = 0; };
	inline MoveDebug& Dbg() { static MoveDebug d; return d; }

	struct MoveInput
	{
		float forward = 0, side = 0;   // -1..1 (W/S, D/A)
		bool jump = false, duck = false, walk = false;
		float pitch = 0, yaw = 0;      // view angles, degrees
	};

	struct PlayerMove
	{
		Vec3 origin;          // hull centre
		Vec3 velocity;
		bool onGround = false;
		Vec3 groundNormal{ 0, 0, 1 };
		int groundSurface = -1;
		bool ducked = false;      // using the duck hull
		bool inDuck = false;      // duck transition running
		float duckTime = 0;       // seconds into the transition
		float viewHeight = kViewStand;
		bool oldJump = false;
		float stamina = 0;
		float maxspeed = 250.0f;  // weapon speed (knife 250)
		bool justJumped = false, justLanded = false;
		bool onLadder = false;
		float fallVelocity = 0;   // downward speed at the last landing
		MoveVars vars;

		Vec3 Mins() const { return HullMins(ducked); }
		Vec3 Maxs() const { return HullMaxs(ducked); }
		Vec3 Eye() const { return origin + Vec3(0, 0, viewHeight); }
		Vec3 Feet() const { return origin + Vec3(0, 0, Mins().z); }

		// noclip (CS console "noclip"): fly along the view direction through
		// everything, no gravity, no ground, no fall damage. Speed like
		// 1200 u/s, Shift (walk key) boosts to 3000 u/s; jump/duck move straight up/down.
		void NoClipMove(const MoveInput& in, float dt)
		{
			justJumped = justLanded = false;
			fallVelocity = 0;
			onGround = false;
			Vec3 f, r;
			AngleVectors(in.pitch, in.yaw, &f, &r, nullptr);
			const float speed = in.walk ? 3000.0f : 1200.0f;   // Shift = boost
			Vec3 wish = f * (in.forward * speed) + r * (in.side * speed);
			if (in.jump) wish.z += speed;
			if (in.duck) wish.z -= speed;
			velocity = wish;
			origin += velocity * dt;
		}

		void Step(const TriWorld& w, const MoveInput& in, float dt)
		{
			justJumped = justLanded = false;
			if (dt <= 0) return;
			if (stamina > 0) { stamina -= dt * 1000.0f; if (stamina < 0) stamina = 0; }
			if (vars.noJumpSlowdown) stamina = 0;

			Duck(w, in, dt);
			CategorizePosition(w);

			// ladders (CS func_ladder): no gravity, climb along the ladder
			Vec3 ladderNormal;
			onLadder = w.LadderAt(origin + Mins(), origin + Maxs(), origin, ladderNormal);
			if (onLadder)
			{
				LadderMove(w, in, ladderNormal, dt);
				oldJump = in.jump;
				CategorizePosition(w);
				return;
			}

			// wish velocity in the horizontal plane
			Vec3 fwd, right;
			AngleVectors(0, in.yaw, &fwd, &right, nullptr);
			float fmove = in.forward * vars.forwardspeed, smove = in.side * vars.sidespeed;
			if (in.walk) { fmove *= vars.movespeedkey; smove *= vars.movespeedkey; }
			if (ducked) { fmove *= kDuckSpeedMul; smove *= kDuckSpeedMul; }
			Vec3 wishvel = fwd * fmove + right * smove;
			wishvel.z = 0;
			float wishspeed = Length(wishvel);
			Vec3 wishdir = wishspeed > 0 ? wishvel * (1.0f / wishspeed) : Vec3();
			const float cap = (std::min)(maxspeed, vars.maxspeed);
			if (wishspeed > cap) wishspeed = cap;

			// half of this frame's gravity before the move, half after
			if (!onGround) velocity.z -= vars.gravity * 0.5f * dt;

			if (in.jump && (!oldJump || vars.autoBunnyhop) && onGround) Jump();
			oldJump = in.jump;

			if (onGround)
			{
				velocity.z = 0;
				Friction(w, dt);
				Accelerate(wishdir, wishspeed, vars.accelerate, dt);
				if (stamina > 0)
				{
					const float ratio = (100.0f - stamina * 0.001f * 19.0f) * 0.01f;
					velocity.x *= ratio;
					velocity.y *= ratio;
				}
				ClampVelocity();
				WalkMove(w, dt);
			}
			else
			{
				AirAccelerate(wishdir, wishspeed, vars.airaccelerate, dt);
				ClampVelocity();
				FlyMove(w, dt, nullptr);
			}

			const bool wasOnGround = onGround;
			const float vzBefore = velocity.z;
			CategorizePosition(w);
			if (!onGround) velocity.z -= vars.gravity * 0.5f * dt;
			else velocity.z = 0;
			if (onGround && !wasOnGround) { justLanded = true; fallVelocity = -vzBefore; }
		}

		// Snap the hull onto the ground below `pos` (used when spawning).
		void Spawn(const TriWorld& w, const Vec3& feet)
		{
			ducked = inDuck = false;
			viewHeight = kViewStand;
			velocity = Vec3();
			stamina = 0;
			origin = feet + Vec3(0, 0, 36.0f + 1.0f);
			TraceResult tr = w.TraceBox(origin + Vec3(0, 0, 36), origin - Vec3(0, 0, 64), Mins(), Maxs());
			if (!tr.startsolid && tr.fraction < 1.0f) origin = tr.endpos;
			CategorizePosition(w);
		}

	private:
		// Ladder movement as in Half-Life / CS: forward / side along the view
		// (pitch included) at 200 u/s; the part pushing into the ladder becomes
		// climbing; jump pushes off the ladder at 270 u/s.
		void LadderMove(const TriWorld& w, const MoveInput& in, const Vec3& n, float dt)
		{
			const float kClimbSpeed = 200.0f;
			if (in.jump && !oldJump)
			{
				velocity = n * 270.0f;
				FlyMove(w, dt, nullptr);
				return;
			}
			float speed = kClimbSpeed;
			if (ducked) speed *= kDuckSpeedMul;
			Vec3 f, r;
			AngleVectors(in.pitch, in.yaw, &f, &r, nullptr);
			const float fmove = in.forward * speed, smove = in.side * speed;
			if (fmove == 0 && smove == 0) { velocity = Vec3(); return; }
			const Vec3 vel = f * fmove + r * smove;
			const float normalComp = Dot(vel, n);
			const Vec3 lateral = vel - n * normalComp;
			const Vec3 perp = Normalized(Cross(Vec3(0, 0, 1), n));
			velocity = lateral - Cross(n, perp) * normalComp;   // into the ladder = up
			if (onGround && normalComp > 0) velocity += n * kClimbSpeed;   // step off at the bottom
			FlyMove(w, dt, nullptr);
		}

		void ClampVelocity()
		{
			for (int i = 0; i < 3; i++)
				velocity[i] = (std::max)(-vars.maxvelocity, (std::min)(vars.maxvelocity, velocity[i]));
		}

		void CategorizePosition(const TriWorld& w)
		{
			// moving up fast = not on the ground (lets jumps and ramps launch)
			if (velocity.z > 180.0f) { onGround = false; return; }
			TraceResult tr = w.TraceBox(origin, origin - Vec3(0, 0, 2.0f), Mins(), Maxs());
			if (tr.fraction < 1.0f && tr.normal.z >= 0.7f)
			{
				onGround = true;
				groundNormal = tr.normal;
				groundSurface = tr.surface;
				// stick to the ground when it is within the probe distance
				if (!tr.startsolid) origin = tr.endpos;
			}
			else
			{
				onGround = false;
				groundSurface = -1;
			}
		}

		void Jump()
		{
			velocity.z = std::sqrt(2.0f * vars.gravity * kJumpHeight);
			if (stamina > 0)
				velocity.z *= (100.0f - stamina * 0.001f * 19.0f) * 0.01f;
			stamina = kJumpStamina;
			onGround = false;
			justJumped = true;
		}

		void Friction(const TriWorld& w, float dt)
		{
			const float speed = Length(velocity);
			if (speed < 0.1f) return;
			// edge friction: no ground 16 units ahead (34 units down) = an edge
			float friction = vars.friction;
			Vec3 ahead = origin + Vec3(velocity.x / speed * 16.0f, velocity.y / speed * 16.0f, 0);
			ahead.z = origin.z + Mins().z;
			TraceResult tr = w.TraceBox(ahead, ahead - Vec3(0, 0, 34.0f), Vec3(), Vec3());
			if (tr.fraction == 1.0f) friction *= vars.edgefriction;

			const float control = speed < vars.stopspeed ? vars.stopspeed : speed;
			float newspeed = speed - dt * control * friction;
			if (newspeed < 0) newspeed = 0;
			velocity *= newspeed / speed;
		}

		void Accelerate(const Vec3& wishdir, float wishspeed, float accel, float dt)
		{
			const float current = Dot(velocity, wishdir);
			const float add = wishspeed - current;
			if (add <= 0) return;
			float accelspeed = accel * dt * wishspeed;
			if (accelspeed > add) accelspeed = add;
			velocity += wishdir * accelspeed;
		}

		void AirAccelerate(const Vec3& wishdir, float wishspeed, float accel, float dt)
		{
			if (wishspeed <= 0) return;
			const float wishspd = wishspeed > kAirWishCap ? kAirWishCap : wishspeed;
			const float current = Dot(velocity, wishdir);
			const float add = wishspd - current;
			if (add <= 0) return;
			float accelspeed = accel * wishspeed * dt;
			if (accelspeed > add) accelspeed = add;
			velocity += wishdir * accelspeed;
		}

		static Vec3 ClipVelocity(const Vec3& in, const Vec3& normal, float overbounce)
		{
			const float backoff = Dot(in, normal) * overbounce;
			Vec3 out = in - normal * backoff;
			for (int i = 0; i < 3; i++)
				if (out[i] > -0.1f && out[i] < 0.1f) out[i] = 0;
			return out;
		}

		// Move along velocity for dt, sliding along up to 4 planes per step.
		// Returns the hit normals' combined state through `blocked` (1 floor, 2 wall).
		void FlyMove(const TriWorld& w, float dt, int* blocked)
		{
			const int kMaxPlanes = 5;
			Vec3 planes[kMaxPlanes];
			int numplanes = 0;
			Vec3 original = velocity;
			Vec3 primal = velocity;
			float timeLeft = dt;
			int blk = 0;
			for (int bump = 0; bump < 4; bump++)
			{
				if (Length(velocity) < 1e-4f) break;
				const Vec3 end = origin + velocity * timeLeft;
				TraceResult tr = w.TraceBox(origin, end, Mins(), Maxs());
				if (tr.startsolid && tr.fraction == 0.0f)
				{
					// stuck inside geometry: stop rather than tunnel through it
					Dbg().startsolid++;
					velocity = Vec3();
					blk |= 3;
					break;
				}
				if (tr.fraction > 0)
				{
					// moved part of the way: clip against the new planes from here
					origin = tr.endpos;
					original = velocity;
					numplanes = 0;
				}
				if (tr.fraction == 1.0f) break;
				if (tr.normal.z > 0.7f) blk |= 1;
				if (tr.normal.z == 0.0f || std::fabs(tr.normal.z) < 0.7f) blk |= 2;
				timeLeft -= timeLeft * tr.fraction;
				// the same plane again (rounding after a clip): push off it a little instead of
				// treating it as a crease, which would zero the velocity
				bool duplicate = false;
				for (int k = 0; k < numplanes && !duplicate; k++)
					if (Dot(tr.normal, planes[k]) > 0.99f) duplicate = true;
				if (duplicate) { velocity += tr.normal; continue; }
				if (numplanes >= kMaxPlanes) { Dbg().planes++; velocity = Vec3(); break; }
				planes[numplanes++] = tr.normal;

				// on the ground (or a single plane): plain slide
				if (numplanes == 1 && onGround)
				{
					velocity = ClipVelocity(velocity, planes[0], 1.0f);
					primal = velocity;
					continue;
				}
				// find a velocity that leaves every touched plane
				int i;
				for (i = 0; i < numplanes; i++)
				{
					velocity = ClipVelocity(original, planes[i], 1.0f);
					int j;
					for (j = 0; j < numplanes; j++)
						if (j != i && Dot(velocity, planes[j]) < 0) break;
					if (j == numplanes) break;
				}
				if (i == numplanes)
				{
					// two planes: slide along their crease
					if (numplanes != 2) { Dbg().crease++; velocity = Vec3(); break; }
					Vec3 dir = Normalized(Cross(planes[0], planes[1]));
					velocity = dir * Dot(dir, velocity);
				}
				// never bounce back against the original direction
				if (Dot(velocity, primal) <= 0)
				{
					Dbg().primal++;
#ifdef CS_MOVE_TRACE
					if (Dbg().primal <= 8)
					{
						printf("  primal stop: onGround %d planes %d primal (%.0f %.0f %.0f) vel (%.0f %.0f %.0f)\n", onGround, numplanes,
							primal.x, primal.y, primal.z, velocity.x, velocity.y, velocity.z);
						for (int k = 0; k < numplanes; k++) printf("    plane %.2f %.2f %.2f\n", planes[k].x, planes[k].y, planes[k].z);
					}
#endif
					velocity = Vec3();
					break;
				}
			}
			if (blocked) *blocked = blk;
		}

		// Ground move: try a plain slide and a stepped one (up 18, move, down),
		// keep whichever got further horizontally.
		void WalkMove(const TriWorld& w, float dt)
		{
			velocity.z = 0;
			if (Length2D(velocity) < 1.0f) { velocity = Vec3(); return; }

			// try reaching the destination directly first
			const Vec3 dest = origin + velocity * dt;
			TraceResult direct = w.TraceBox(origin, Vec3(dest.x, dest.y, origin.z), Mins(), Maxs());
			if (direct.fraction == 1.0f && !direct.startsolid)
			{
				origin = direct.endpos;
				StayOnGround(w);
				return;
			}

			const Vec3 startPos = origin, startVel = velocity;
			FlyMove(w, dt, nullptr);
			const Vec3 downPos = origin, downVel = velocity;

			// stepped attempt
			origin = startPos;
			velocity = startVel;
			TraceResult up = w.TraceBox(origin, origin + Vec3(0, 0, vars.stepsize), Mins(), Maxs());
			if (!up.startsolid) origin = up.endpos;
			FlyMove(w, dt, nullptr);
			TraceResult down = w.TraceBox(origin, origin - Vec3(0, 0, vars.stepsize), Mins(), Maxs());
			if (down.normal.z < 0.7f && down.fraction < 1.0f)
			{
				// stepped onto something too steep: use the plain slide
				origin = downPos;
				velocity = downVel;
				return;
			}
			if (!down.startsolid) origin = down.endpos;
			const Vec3 upPos = origin;

			const float downDist = (downPos.x - startPos.x) * (downPos.x - startPos.x) + (downPos.y - startPos.y) * (downPos.y - startPos.y);
			const float upDist = (upPos.x - startPos.x) * (upPos.x - startPos.x) + (upPos.y - startPos.y) * (upPos.y - startPos.y);
			if (downDist > upDist)
			{
				origin = downPos;
				velocity = downVel;
			}
			else
				velocity.z = downVel.z;
		}

		// Follow the ground down small slopes / steps while walking.
		void StayOnGround(const TriWorld& w)
		{
			TraceResult tr = w.TraceBox(origin, origin - Vec3(0, 0, vars.stepsize), Mins(), Maxs());
			if (tr.fraction < 1.0f && !tr.startsolid && tr.normal.z >= 0.7f)
				origin = tr.endpos;
		}

		void Duck(const TriWorld& w, const MoveInput& in, float dt)
		{
			if (in.duck)
			{
				if (!ducked && !inDuck) { inDuck = true; duckTime = 0; }
				if (inDuck && !ducked)
				{
					duckTime += dt;
					if (duckTime >= kTimeToDuck || !onGround)
					{
						// switch to the duck hull; in the air the feet come up
						ducked = true;
						inDuck = false;
						if (onGround) origin.z -= kDuckHullDelta;
						viewHeight = kViewDuck;
					}
					else
					{
						const float f = duckTime / kTimeToDuck;
						viewHeight = kViewStand + (kViewDuck - kDuckHullDelta - kViewStand) * f;
					}
				}
			}
			else
			{
				inDuck = false;
				if (ducked)
				{
					// stand up only when the standing hull fits
					Vec3 newOrigin = origin;
					if (onGround) newOrigin.z += kDuckHullDelta;
					TraceResult tr = w.TraceBox(newOrigin, newOrigin + Vec3(0, 0, 0.01f), HullMins(false), HullMaxs(false));
					if (!tr.startsolid)
					{
						ducked = false;
						origin = newOrigin;
						viewHeight = kViewStand;
					}
				}
				else
					viewHeight = kViewStand;
			}
		}
	};
}
