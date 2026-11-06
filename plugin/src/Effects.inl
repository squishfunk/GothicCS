// GothicCS - world-space CS sprite effects (bullet impact puffs, explosions,
// smoke clouds, muzzle flashes of bots). Each effect is a camera-facing
// sprite at a world position, projected with the current CS camera;
// visibility = a line-of-sight test from the eye (sprites drawn after the
// world do not use the world depth buffer).
// Included inside GOTHIC_NAMESPACE after Sprites.inl and Hits.inl.

namespace GothicCS
{
	struct WorldSprite
	{
		Sprite* sprite = nullptr;
		cs::Vec3 pos;            // CS space
		float born = 0, life = 0.3f;
		float size = 1.0f;       // world units per sprite pixel (sprite scale)
		float fps = 15.0f;       // animation speed (frames per second), 0 = random fixed frame
		int frame = 0;
		float roll = 0;
		zCOLOR color;
		float fadeIn = 0, fadeOut = 0;   // seconds
		bool visible = true;
		float visCheckAt = 0;
		cs::Vec3 velocity;       // drift (smoke)
	};

	inline std::vector<WorldSprite>& WorldSprites() { static std::vector<WorldSprite> v; return v; }

	inline WorldSprite& AddWorldSprite(const char* name, const cs::Vec3& pos, float now, float life, float scale, float fps)
	{
		WorldSprite w;
		w.sprite = GetSprite(name);
		w.pos = pos;
		w.born = now;
		w.life = life;
		w.size = scale;
		w.fps = fps;
		w.color = Rgba(255, 255, 255, 255);
		WorldSprites().push_back(w);
		return WorldSprites().back();
	}

	struct ViewCamera
	{
		cs::Vec3 eye, f, r, u;
		ScreenProjector proj;
		bool ok = false;
		cs::Vec3 ToView(const cs::Vec3& p) const
		{
			const cs::Vec3 d = p - eye;
			return { cs::Dot(d, f), -cs::Dot(d, r), cs::Dot(d, u) };
		}
	};

	inline void DrawWorldSprites(const ViewCamera& cam, float now)
	{
		auto& list = WorldSprites();
		list.erase(std::remove_if(list.begin(), list.end(), [&](const WorldSprite& w) { return now > w.born + w.life; }), list.end());
		if (!cam.ok) return;
		// far to near
		std::vector<std::pair<float, WorldSprite*>> order;
		for (WorldSprite& w : list)
		{
			if (now < w.born) continue;   // delayed effect (e.g. smoke after an explosion)
			const cs::Vec3 v = cam.ToView(w.pos + w.velocity * (now - w.born));
			if (v.x < 4.0f) continue;
			order.push_back({ v.x, &w });
		}
		std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
		for (auto& o : order)
		{
			WorldSprite& w = *o.second;
			if (!w.sprite || !w.sprite->ok) continue;
			const cs::Vec3 pos = w.pos + w.velocity * (now - w.born);
			if (now >= w.visCheckAt)
			{
				w.visCheckAt = now + 0.1f;
				w.visible = LineOfSight(ToGothic(cam.eye), ToGothic(pos), player);
			}
			if (!w.visible) continue;
			const cs::Vec3 v = cam.ToView(pos);
			float sx, sy;
			if (!cam.proj.Project(v, sx, sy)) continue;
			const float age = now - w.born;
			const int frame = w.fps > 0 ? int(age * w.fps) : w.frame;
			if (w.fps > 0 && frame >= (int)w.sprite->frames.size() && w.life <= (float)w.sprite->frames.size() / w.fps + 0.01f) continue;
			float alpha = w.color.alpha / 255.0f;
			if (w.fadeIn > 0 && age < w.fadeIn) alpha *= age / w.fadeIn;
			if (w.fadeOut > 0 && age > w.life - w.fadeOut) alpha *= (w.life - age) / w.fadeOut;
			zCOLOR c = w.color;
			c.alpha = (unsigned char)(255.0f * (alpha < 0 ? 0 : (alpha > 1 ? 1 : alpha)));
			if (w.sprite->format == SprAdditive)
			{
				// additive: fade by darkening the colour
				c.r = (unsigned char)(c.r * alpha); c.g = (unsigned char)(c.g * alpha); c.b = (unsigned char)(c.b * alpha);
			}
			const SpriteFrame& fr = w.sprite->frames[frame % w.sprite->frames.size()];
			const float pw = cam.proj.focal * fr.w * w.size / v.x, ph = cam.proj.focal * fr.h * w.size / v.x;
			DrawSpriteQuad(*w.sprite, frame, sx, sy, pw, ph, w.roll, c, BlendFor(*w.sprite));
		}
	}

	// Bullet impact on the world: CS wall puff + ricochet sound on stone / metal.
	inline void Impact(State& s, const WorldHit& h)
	{
		const float now = (float)s.clock;
		const cs::Vec3 p = ToCS(h.point + h.normal * 2.0f);
		static const char* kPuffs[] = { "wall_puff1", "wall_puff2", "wall_puff3", "wall_puff4" };
		WorldSprite& w = AddWorldSprite(kPuffs[s.rng.Long(0, 3)], p, now, 0.6f, 0.4f, 20.0f);
		w.color = Rgba(200, 200, 200, 255);
		w.velocity = cs::Vec3(0, 0, 6.0f);
		w.roll = s.rng.Float(0, 360);
		if (h.mat == zMAT_GROUP_METAL)
			PlaySfxAt(SfxName(s.rng.Long(0, 1) ? "weapons/ric_metal-1.wav" : "weapons/ric_metal-2.wav"), h.point);
		else if (h.mat == zMAT_GROUP_STONE && s.rng.Long(0, 2) == 0)
			PlaySfxAt(SfxName(s.rng.Long(0, 1) ? "weapons/ric_conc-1.wav" : "weapons/ric_conc-2.wav"), h.point);
	}
}
