// GothicCS - CS rounds: freeze time, round time, buy time / buy zone, win
// conditions, team rewards and loss bonus, restart. Numbers from
// THIRD_PARTY_NOTICES.md (ReGameDLL_CS, original CS 1.6): mp_freezetime 6,
// mp_roundtime 5 min, mp_buytime 1.5 min, ROUND_BEGIN_DELAY 5 s, rewards
// (bomb 3500, elimination / defuse / time 3250, defused bomb +800 for T),
// loss bonus 1400 +500 per further loss.
// The player is one team, the bots spawned with K the other.
// Included inside GOTHIC_NAMESPACE after Bots.inl.

namespace GothicCS
{
	enum RoundPhase { RP_FREEZE, RP_LIVE, RP_END };
	enum Winner { WIN_NONE = 0, WIN_T = 1, WIN_CT = 2, WIN_DRAW = 3 };

	struct BotSpawn { cs::Vec3 pos; float yaw; };

	struct RoundState
	{
		RoundPhase phase = RP_FREEZE;
		double phaseStart = 0;        // start of the current phase
		double liveStart = 0;         // freeze end (buy time and round time count from here)
		double endAt = 0;
		int lossBonus = 1400, tLosses = 0, ctLosses = 0;
		bool noMoneyNextRound = false;   // living Ts when the time ran out
		std::vector<BotSpawn> botSpawns;
		int roundNumber = 0;
		int scoreT = 0, scoreCT = 0;
	};
	inline RoundState& Round() { static RoundState r; return r; }

	constexpr double kFreezeTime = 6.0, kRoundTime = 5.0 * 60.0, kBuyTime = 1.5 * 60.0, kRoundBeginDelay = 5.0;

	inline bool BotsRoundLive() { return Round().phase == RP_LIVE; }

	inline bool FreezePeriod() { return Round().phase == RP_FREEZE; }
	inline void RecordBotSpawn(const cs::Vec3& pos, float yaw) { Round().botSpawns.push_back({ pos, yaw }); }

	inline int RoundSecondsLeft()
	{
		State& s = S();
		const RoundState& r = Round();
		if (Bomb().planted) return -1;   // the client hides the timer once the bomb is planted
		if (r.phase == RP_FREEZE) return (int)std::ceil((std::max)(0.0, kFreezeTime - (s.clock - r.phaseStart)));
		if (r.phase == RP_LIVE) return (int)std::ceil((std::max)(0.0, kRoundTime - (s.clock - r.liveStart)));
		return 0;
	}

	// No func_buyzone in Gothic: within 200 units of the team spawn (CS rule for maps without one).
	inline bool InBuyZone(State& s)
	{
		return s.alive && cs::Length(s.pm.origin - (s.spawnFeet + cs::Vec3(0, 0, 36))) <= 200.0f + 36.0f;
	}

	inline bool CanBuy(State& s, const char** why)
	{
		const RoundState& r = Round();
		if (!s.alive) { *why = "You are dead"; return false; }
		if (!InBuyZone(s)) { *why = "You must be in the buy zone near your spawn"; return false; }
		if (r.phase == RP_LIVE && s.clock - r.liveStart > kBuyTime)
		{
			*why = "The 90-second buy period has ended.";
			return false;
		}
		return true;
	}

	inline int AliveBots()
	{
		int n = 0;
		for (Bot* b : Bots()) if (!b->dead) n++;
		return n;
	}

	inline void TerminateRound(State& s, Winner w, const char* text, const char* radio, int winnerReward, bool tGetsPlantBonus = false)
	{
		RoundState& r = Round();
		if (r.phase == RP_END) return;
		r.phase = RP_END;
		r.endAt = s.clock + kRoundBeginDelay;
		CentrePrint(text);
		if (radio) PlaySfx(SfxName(radio));
		if (w == WIN_T) r.scoreT++;
		else if (w == WIN_CT) r.scoreCT++;
		if (w == WIN_DRAW) return;

		// loss bonus (one global value): a broken streak resets it to 1500
		const bool tWon = w == WIN_T;
		if (tWon) { if (r.tLosses > 1) r.lossBonus = 1500; r.tLosses = 0; r.ctLosses++; }
		else { if (r.ctLosses > 1) r.lossBonus = 1500; r.ctLosses = 0; r.tLosses++; }
		if (r.tLosses > 1 && r.lossBonus < 3000) r.lossBonus += 500;
		else if (r.ctLosses > 1 && r.lossBonus < 3000) r.lossBonus += 500;

		const bool playerWon = (s.inv.ct && !tWon) || (!s.inv.ct && tWon);
		int award = playerWon ? winnerReward : r.lossBonus;
		if (!s.inv.ct && tGetsPlantBonus) award += 800;
		if (r.noMoneyNextRound) award = 0;
		s.pendingAward = award;
	}

	// Seed three defenders per configured site; positions must have ground and sight of the site.
	inline void SetupSiteGuards(State& s)
	{
		if (s.inv.ct || !Round().botSpawns.empty()) return;
		for (const Bombsite& site : s.bombsites)
			for (int i = 0; i < 3; ++i) {
				const float angle = i * 120.0f * cs::kDegToRad;
				cs::Vec3 spot = site.pos + cs::Vec3(std::cos(angle) * 180, std::sin(angle) * 180, 0);
				bool hit = false;
				const zVEC3 ground = GroundBelow(ToGothic(spot) + zVEC3(0, 150, 0), hit);
				if (!hit) continue;
				spot = ToCS(ground);
				if (std::fabs(spot.z - site.pos.z) > 64 || !LineOfSight(ToGothic(site.pos + cs::Vec3(0,0,36)), ToGothic(spot + cs::Vec3(0,0,36)), player)) continue;
				if (SpawnBotAt(s, spot, i * 120.0f)) RecordBotSpawn(spot, i * 120.0f);
			}
	}

	inline void StartRound(State& s);
	inline int RoundScoreT() { return Round().scoreT; }
	inline int RoundScoreCT() { return Round().scoreCT; }
	inline bool RoundHasBots() { return !Round().botSpawns.empty(); }

	// Win conditions, per 100 Hz step.
	inline void RoundStep(State& s)
	{
		RoundState& r = Round();
		if (r.phase == RP_FREEZE)
		{
			if (s.clock - r.phaseStart >= kFreezeTime)
			{
				r.phase = RP_LIVE;
				r.liveStart = s.clock;
				static const char* kGo[] = { "radio/moveout.wav", "radio/letsgo.wav", "radio/locknload.wav", "radio/com_go.wav" };
				PlaySfx(SfxName(kGo[s.rng.Long(0, 3)]));
			}
			return;
		}
		if (r.phase == RP_END)
		{
			if (s.clock >= r.endAt) StartRound(s);
			return;
		}
		const bool match = !r.botSpawns.empty();
		PlantedBomb& b = Bomb();
		if (b.exploded) { TerminateRound(s, WIN_T, "Target Succesfully Bombed!", "radio/terwin.wav", 3500); return; }
		if (b.defusedThisRound) { TerminateRound(s, WIN_CT, "The bomb has been defused!", "radio/ctwin.wav", 3250, true); return; }
		if (match)
		{
			const bool botsAlive = AliveBots() > 0;
			if (!botsAlive && (s.inv.ct || s.bombsites.empty()))
			{
				if (s.inv.ct) TerminateRound(s, WIN_CT, "Counter-Terrorists Win!", "radio/ctwin.wav", 3250);
				else TerminateRound(s, WIN_T, "Terrorists Win!", "radio/terwin.wav", 3250);
				return;
			}
			if (!s.alive && !b.planted)
			{
				if (s.inv.ct) TerminateRound(s, WIN_T, "Terrorists Win!", "radio/terwin.wav", 3250);
				else TerminateRound(s, WIN_CT, "Counter-Terrorists Win!", "radio/ctwin.wav", 3250);
				return;
			}
		}
		if (!b.planted && s.clock - r.liveStart >= kRoundTime)
		{
			// Target_Saved: CTs win; living Ts get no money next round
			if (!s.inv.ct && s.alive) r.noMoneyNextRound = true;
			TerminateRound(s, WIN_CT, "Target has been saved!", "radio/ctwin.wav", 3250);
		}
	}

	inline Bot* SpawnBotAt(State& s, const cs::Vec3& feet, float yaw);
	inline void Respawn(State& s, bool keepEquipment);

	inline void StartRound(State& s)
	{
		RoundState& r = Round();
		r.roundNumber++;
		// pay the result of the last round
		if (s.pendingAward)
		{
			s.inv.AddMoney(s.pendingAward);
			s.pendingAward = 0;
		}
		r.noMoneyNextRound = false;
		// world reset
		RemoveBomb();
		RemoveAllGrenades();
		WorldSprites().clear();
		// bots: remove (dead ones too) and spawn again at their spawn points
		for (Bot* b : Bots())
		{
			RemoveModelVob(b->gun);
			if (b->npc && ogame && ogame->GetGameWorld())
			{
				b->npc->SetSleeping(FALSE);
				ogame->GetSpawnManager()->DeleteNpc(b->npc);
			}
			delete b;
		}
		Bots().clear();
		for (const BotSpawn& sp : r.botSpawns) SpawnBotAt(s, sp.pos, sp.yaw);
		// the player keeps the equipment when he survived
		Respawn(s, s.alive);
		r.phase = RP_FREEZE;
		r.phaseStart = s.clock;
		s.buyMenu = 0;
	}
}
