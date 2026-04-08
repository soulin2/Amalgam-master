#include "MovementSimulation.h"

#include "../../EnginePrediction/EnginePrediction.h"
#include <numeric>

static CUserCmd s_tDummyCmd = {};

void CMovementSimulation::Store(MoveStorage& tMoveStorage)
{
	auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap();
	if (!pMap)
		return;

	size_t iSize = tMoveStorage.m_pPlayer->GetIntermediateDataSize();
	tMoveStorage.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));

	CPredictionCopy copy = { PC_NETWORKED_ONLY, tMoveStorage.m_pData, PC_DATA_PACKED, tMoveStorage.m_pPlayer, PC_DATA_NORMAL };
	copy.TransferData("MovementSimulationStore", tMoveStorage.m_pPlayer->entindex(), pMap);
}

void CMovementSimulation::Reset(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_pData)
	{
		auto pMap = tMoveStorage.m_pPlayer->GetPredDescMap();
		if (!pMap)
			return;

		CPredictionCopy copy = { PC_NETWORKED_ONLY, tMoveStorage.m_pPlayer, PC_DATA_NORMAL, tMoveStorage.m_pData, PC_DATA_PACKED };
		copy.TransferData("MovementSimulationReset", tMoveStorage.m_pPlayer->entindex(), pMap);

		I::MemAlloc->Free(tMoveStorage.m_pData);
		tMoveStorage.m_pData = nullptr;
	}
}

static inline void HandleMovement(CTFPlayer* pPlayer, MoveData* pLastRecord, MoveData& tCurRecord, std::deque<MoveData>& vRecords)
{
	bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer();

	if (pLastRecord)
	{
		/*
		if (tRecord.m_iMode != pLastRecord->m_iMode)
		{
			pLastRecord = nullptr;
			vRecords.clear();
		}
		else */
		{	// does this eat up fps? i can't tell currently
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};
			SDK::TraceHull(pLastRecord->m_vOrigin, pLastRecord->m_vOrigin + pLastRecord->m_vVelocity * TICK_INTERVAL, pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION, pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION, pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit() && trace.plane.normal.z < 0.707f)
			{
				pLastRecord = nullptr;
				vRecords.clear();
			}
		}
	}
	if (!pLastRecord)
		return;

	if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
	{
		s_tDummyCmd.forwardmove = 450.f;
		s_tDummyCmd.sidemove = 0.f;
		SDK::FixMovement(&s_tDummyCmd, bLocal ? F::EnginePrediction.m_vAngles : pPlayer->GetEyeAngles(), {});
		tCurRecord.m_vDirection.x = s_tDummyCmd.forwardmove;
		tCurRecord.m_vDirection.y = -s_tDummyCmd.sidemove;
		return;
	}

	switch (tCurRecord.m_iMode)
	{
	case MoveEnum::Ground:
	{
		if (bLocal && Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		{
			float flMaxSpeed = SDK::MaxSpeed(pPlayer, true);
			tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * flMaxSpeed;
		}
		break;
	}
	case MoveEnum::Air:
	{
		float flMaxSpeed = SDK::MaxSpeed(pPlayer, true);
		tCurRecord.m_vDirection = tCurRecord.m_vVelocity.Normalized2D() * flMaxSpeed;
		break;
	}
	case MoveEnum::Swim:
	{
		tCurRecord.m_vDirection *= 2;
	}
	}
}

void CMovementSimulation::Store()
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vRecords = m_mRecords[pPlayer->entindex()];

		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->m_vecVelocity().IsZero())
		{
			vRecords.clear();
			continue;
		}
		else if (!H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;

		bool bLocal = pPlayer->entindex() == I::EngineClient->GetLocalPlayer() && !I::EngineClient->IsPlayingDemo();
		Vec3 vVelocity = bLocal ? F::EnginePrediction.m_vVelocity : pPlayer->m_vecVelocity();
		Vec3 vOrigin = bLocal ? F::EnginePrediction.m_vOrigin : pPlayer->m_vecOrigin();
		Vec3 vDirection = bLocal ? Math::RotatePoint(F::EnginePrediction.m_vDirection, {}, { 0, F::EnginePrediction.m_vAngles.y, 0 }) : vVelocity.To2D();

		MoveData* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
		Vec3 vAcceleration = {};
		if (pLastRecord)
		{
			float flDelta = pPlayer->m_flSimulationTime() - pLastRecord->m_flSimTime;
			if (flDelta > 0.f)
				vAcceleration = (vVelocity - pLastRecord->m_vVelocity) / flDelta;
		}

		vRecords.emplace_front(
			vDirection,
			pPlayer->m_flSimulationTime(),
			pPlayer->IsSwimming() ? MoveEnum::Swim : pPlayer->IsOnGround() ? MoveEnum::Ground : MoveEnum::Air,
			vVelocity,
			vOrigin,
			vAcceleration
		);
		MoveData& tCurRecord = vRecords.front();
		if (vRecords.size() > 66)
			vRecords.pop_back();

		HandleMovement(pPlayer, pLastRecord, tCurRecord, vRecords);
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		auto& vSimTimes = m_mSimTimes[pPlayer->entindex()];

		if (pEntity->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			vSimTimes.clear();
			continue;
		}

		float flDeltaTime = H::Entities.GetDeltaTime(pPlayer->entindex());
		if (!flDeltaTime)
			continue;

		vSimTimes.push_front(flDeltaTime);
		if (vSimTimes.size() > Vars::Aimbot::Projectile::DeltaCount.Value)
			vSimTimes.pop_back();
	}
}



bool CMovementSimulation::Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance, bool bStrafe)
{
	if (!pEntity || !pEntity->IsPlayer() || !pEntity->As<CTFPlayer>()->IsAlive())
	{
		tMoveStorage.m_bInitFailed = tMoveStorage.m_bFailed = true;
		return false;
	}

	auto pPlayer = pEntity->As<CTFPlayer>();
	tMoveStorage.m_pPlayer = pPlayer;

	// store vars
	m_bOldInPrediction = I::Prediction->m_bInPrediction;
	m_bOldFirstTimePredicted = I::Prediction->m_bFirstTimePredicted;
	m_flOldFrametime = I::GlobalVars->frametime;

	// store restore data
	Store(tMoveStorage);

	// the hacks that make it work
	I::MoveHelper->SetHost(pPlayer);
	pPlayer->m_pCurrentCommand() = &s_tDummyCmd;

	// Save original network velocity before averaging — used as floor for prediction parameters
	float flNetSpeed2D = pPlayer->m_vecVelocity().Length2D();

	if (auto pAvgVelocity = H::Entities.GetAvgVelocity(pPlayer->entindex()))
	{
		// Only override if averaged velocity has meaningful XY magnitude;
		// stale/zero avgVelocity would destroy movement setup and prediction
		if (!pAvgVelocity->To2D().IsZero())
			pPlayer->m_vecVelocity() = *pAvgVelocity;
	}

	if (pPlayer->m_bDucked() = pPlayer->IsDucking())
	{
		pPlayer->m_fFlags() &= ~FL_DUCKING; // breaks origin's z if FL_DUCKING is not removed
		pPlayer->m_flDucktime() = 0.f;
		pPlayer->m_flDuckJumpTime() = 0.f;
		pPlayer->m_bDucking() = false;
		pPlayer->m_bInDuckJump() = false;
	}

	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		pPlayer->m_vecBaseVelocity() = Vec3(); // residual basevelocity causes issues
		if (pPlayer->IsOnGround())
			pPlayer->m_vecVelocity().z = std::min(pPlayer->m_vecVelocity().z, 0.f); // step fix
		else
			pPlayer->m_hGroundEntity() = nullptr; // fix for velocity.z being set to 0 even if in air
	}
	else if (Vars::Misc::Movement::Bunnyhop.Value && G::OriginalCmd.buttons & IN_JUMP)
		tMoveStorage.m_bBunnyHop = true;

	// setup move data
	if (!SetupMoveData(tMoveStorage))
	{
		tMoveStorage.m_bFailed = true;
		return false;
	}

	// Compute initial speed ratio for prediction scaling (slow targets get less aggressive prediction)
	if (tMoveStorage.m_MoveData.m_flMaxSpeed > 1.f && pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		float flSpeed = tMoveStorage.m_MoveData.m_vecVelocity.Length2D();
		// Use the higher of averaged and network velocity for prediction parameters.
		// GetAvgVelocity can return stale/low values — network velocity provides a safety floor
		// that prevents velocity cap and input scaling from crushing prediction.
		flSpeed = std::max(flSpeed, flNetSpeed2D);
		tMoveStorage.m_flInitialSpeedRatio = std::clamp(flSpeed / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.05f, 1.f);
		tMoveStorage.m_flInitialSimSpeed2D = flSpeed;

		// Analyze recent velocity records for trend, consistency, and volatility
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		if (vRecords.size() >= 3)
		{
			const int iAnalyze = std::min(static_cast<int>(vRecords.size()), 8);
			float flTrendSum = 0.f, flTrendWeight = 0.f;
			float flSpeedSum = 0.f, flSpeedSqSum = 0.f;
			float flPeakSpeed = 0.f;
			int iSpeedSamples = 0;

			for (int j = 0; j < iAnalyze - 1; j++)
			{
				float flV1 = vRecords[j].m_vVelocity.Length2D();
				float flV2 = vRecords[j + 1].m_vVelocity.Length2D();
				float flDt = vRecords[j].m_flSimTime - vRecords[j + 1].m_flSimTime;
				if (flDt > 0.001f)
				{
					float flAccel = (flV1 - flV2) / flDt; // positive = accelerating
					float flW = 1.f / (1.f + float(j) * 0.3f); // recent samples weighted higher
					flTrendSum += flAccel * flW;
					flTrendWeight += flW;
				}
				if (j == 0) { flSpeedSum += flV1; flSpeedSqSum += flV1 * flV1; iSpeedSamples++; flPeakSpeed = flV1; }
				flSpeedSum += flV2; flSpeedSqSum += flV2 * flV2; iSpeedSamples++;
				flPeakSpeed = std::max(flPeakSpeed, flV2);
			}

			tMoveStorage.m_flPeakRecentSpeed = flPeakSpeed;

			// Speed volatility: how much the target's speed is fluctuating
			// High volatility = target recently had very different speeds → more uncertainty
			tMoveStorage.m_flSpeedVolatility = std::clamp(
				(flPeakSpeed - flSpeed) / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);

			// Velocity trend: weighted average acceleration
			if (flTrendWeight > 0.f)
				tMoveStorage.m_flVelocityTrend = flTrendSum / flTrendWeight;

			// Velocity consistency: 1.0 = perfectly stable, lower = erratic
			if (iSpeedSamples >= 2 && tMoveStorage.m_MoveData.m_flMaxSpeed > 1.f)
			{
				float flMean = flSpeedSum / float(iSpeedSamples);
				float flVariance = (flSpeedSqSum / float(iSpeedSamples)) - (flMean * flMean);
				float flStdDev = sqrtf(std::max(flVariance, 0.f));
				float flCoeffVar = flStdDev / tMoveStorage.m_MoveData.m_flMaxSpeed;
				tMoveStorage.m_flVelocityConsistency = std::clamp(1.f - flCoeffVar * 2.5f, 0.6f, 1.f);
			}

			// Compute smoothed 2D acceleration vector from recent velocity records
			// This provides the actual direction and magnitude of velocity change,
			// decomposed into tangential (speed change) and lateral (turning) components.
			// Used by the speed envelope model in RunTick for physics-based prediction.
			{
				Vec3 vAccelVecSum = {};
				float flAccelVecWeight = 0.f;
				for (int j2 = 0; j2 < iAnalyze - 1; j2++)
				{
					float flDt = vRecords[j2].m_flSimTime - vRecords[j2 + 1].m_flSimTime;
					if (flDt > 0.001f)
					{
						Vec3 vDV = vRecords[j2].m_vVelocity - vRecords[j2 + 1].m_vVelocity;
						vDV.z = 0.f;
						Vec3 vA = vDV * (1.f / flDt);
						float flW = 1.f / (1.f + float(j2) * 0.3f);
						vAccelVecSum = vAccelVecSum + vA * flW;
						flAccelVecWeight += flW;
					}
				}
				if (flAccelVecWeight > 0.f)
				{
					tMoveStorage.m_vSmoothedAccel2D = vAccelVecSum * (1.f / flAccelVecWeight);

					// Decompose acceleration into tangential (along velocity) and lateral (perpendicular)
					if (flSpeed > 10.f)
					{
						Vec3 vVelN = pPlayer->m_vecVelocity().Normalized2D();
						// Tangential: positive = speeding up, negative = braking
						tMoveStorage.m_flAccelAlignment = tMoveStorage.m_vSmoothedAccel2D.x * vVelN.x
							+ tMoveStorage.m_vSmoothedAccel2D.y * vVelN.y;
						// Lateral: magnitude of centripetal acceleration (turning force)
						float flAccelMag2 = tMoveStorage.m_vSmoothedAccel2D.x * tMoveStorage.m_vSmoothedAccel2D.x
							+ tMoveStorage.m_vSmoothedAccel2D.y * tMoveStorage.m_vSmoothedAccel2D.y;
						float flTangential2 = tMoveStorage.m_flAccelAlignment * tMoveStorage.m_flAccelAlignment;
						tMoveStorage.m_flAccelLateral = sqrtf(std::max(flAccelMag2 - flTangential2, 0.f));
					}
				}
			}

			// NOTE: VelocityTrend and SpeedVolatility no longer adjust SpeedRatio.
			// SpeedEnvelope in RunTick() already models acceleration/deceleration
			// using the decomposed acceleration vector (m_flAccelAlignment).
			// Stacking trend adjustments here caused double-dampening.

			// Minimum floor: clearly moving targets should not have prediction crushed to near-zero
			float flOrigRatio = flSpeed / tMoveStorage.m_MoveData.m_flMaxSpeed;
			if (flOrigRatio > 0.5f)
				tMoveStorage.m_flInitialSpeedRatio = std::max(tMoveStorage.m_flInitialSpeedRatio, 0.65f);
			else if (flOrigRatio > 0.15f)
				tMoveStorage.m_flInitialSpeedRatio = std::max(tMoveStorage.m_flInitialSpeedRatio, 0.35f);
		}

		// Scale movement inputs so simulation doesn't accelerate barely-moving targets to full speed
		tMoveStorage.m_MoveData.m_flForwardMove *= tMoveStorage.m_flInitialSpeedRatio;
		tMoveStorage.m_MoveData.m_flSideMove *= tMoveStorage.m_flInitialSpeedRatio;

		// Store base movement inputs for per-tick temporal decay application
		tMoveStorage.m_flBaseForwardMove = tMoveStorage.m_MoveData.m_flForwardMove;
		tMoveStorage.m_flBaseSideMove = tMoveStorage.m_MoveData.m_flSideMove;
	}

	// Peek/jiggle detection: analyze recent position records for oscillation pattern
	// A peeking player moves out from cover, stops/reverses, moves back — creating position oscillation
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		const int iMinPeekRecords = 6;
		if (static_cast<int>(vRecords.size()) >= iMinPeekRecords)
		{
			const int iPeekAnalyze = std::min(static_cast<int>(vRecords.size()), 20);

			// Step 1: Compute average position (potential peek center)
			Vec3 vAvgPos = {};
			for (int j = 0; j < iPeekAnalyze; j++)
				vAvgPos = vAvgPos + vRecords[j].m_vOrigin;
			vAvgPos = vAvgPos * (1.f / float(iPeekAnalyze));

			// Step 2: Compute average distance from center (oscillation amplitude)
			float flAvgDist = 0.f;
			float flMaxDist = 0.f;
			for (int j = 0; j < iPeekAnalyze; j++)
			{
				float flDist = (vRecords[j].m_vOrigin - vAvgPos).Length2D();
				flAvgDist += flDist;
				flMaxDist = std::max(flMaxDist, flDist);
			}
			flAvgDist /= float(iPeekAnalyze);

			// Step 3: Count direction reversals (sign changes in displacement from center)
			int iReversals = 0;
			int iPrevSide = 0;
			// Use the dominant movement axis for more robust detection
			Vec3 vSpread = {};
			for (int j = 0; j < iPeekAnalyze; j++)
			{
				Vec3 vDelta = vRecords[j].m_vOrigin - vAvgPos;
				vSpread.x += fabsf(vDelta.x);
				vSpread.y += fabsf(vDelta.y);
			}
			bool bUseX = vSpread.x >= vSpread.y;

			for (int j = 0; j < iPeekAnalyze; j++)
			{
				Vec3 vDelta = vRecords[j].m_vOrigin - vAvgPos;
				float flAxis = bUseX ? vDelta.x : vDelta.y;
				int iSide = (flAxis > 5.f) ? 1 : (flAxis < -5.f) ? -1 : 0;
				if (iSide != 0 && iPrevSide != 0 && iSide != iPrevSide)
					iReversals++;
				if (iSide != 0)
					iPrevSide = iSide;
			}

			// Step 4: Check for peek-like pattern criteria
			// - Multiple direction reversals (at least 2 for a full oscillation)
			// - Small amplitude (not traveling long distance, just jiggling)
			// - Reasonable radius (not standing still)
			bool bIsPeek = iReversals >= 2
				&& flMaxDist < 250.f    // not traveling far from center
				&& flAvgDist > 10.f     // actually oscillating, not stationary
				&& flMaxDist > 20.f;

			if (bIsPeek)
			{
				tMoveStorage.m_bPeekDetected = true;
				tMoveStorage.m_vPeekCenter = vAvgPos;
				tMoveStorage.m_flPeekRadius = flAvgDist;

				// Blend strength based on confidence: more reversals = higher confidence
				// Also factor in how tight the oscillation is (smaller radius → more confident)
				float flReversalConf = std::clamp(float(iReversals - 1) * 0.25f, 0.f, 1.f);
				float flRadiusConf = std::clamp(1.f - flAvgDist / 150.f, 0.2f, 1.f);
				tMoveStorage.m_flPeekBlend = flReversalConf * flRadiusConf;
				// Cap blend so simulation still has influence
				tMoveStorage.m_flPeekBlend = std::clamp(tMoveStorage.m_flPeekBlend, 0.f, 0.65f);
			}

			// Cover-proximity detection: trace backwards from target to find nearby walls
			// Helps detect single-reversal peeks (target peeks out from cover, reverses once)
			// Standard peek detection requires 2+ reversals, but real peeks often only have 1
			float flCurSpeed2D = pPlayer->m_vecVelocity().Length2D();
			if (!vRecords.empty() && flCurSpeed2D > 10.f)
			{
				Vec3 vVelDir = pPlayer->m_vecVelocity().Normalized2D();
				Vec3 vOrigin = pPlayer->m_vecOrigin();

				// Trace backwards (opposite to velocity) to find cover behind the target
				Vec3 vTraceEnd = vOrigin - vVelDir * 200.f;
				CGameTrace coverTrace = {};
				CTraceFilterWorldAndPropsOnly coverFilter = {};
				SDK::TraceHull(vOrigin, vTraceEnd,
					pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION,
					pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION,
					pPlayer->SolidMask(), &coverFilter, &coverTrace);

				if (coverTrace.DidHit() && coverTrace.plane.normal.z < 0.707f)
				{
					tMoveStorage.m_bNearCover = true;
					tMoveStorage.m_flCoverDist = (coverTrace.endpos - vOrigin).Length2D();
					tMoveStorage.m_vCoverNormal = coverTrace.plane.normal;

					// Retreat likely: near cover + showing signs of deceleration or reversal
					// AccelAlignment < -50 means braking hard; VelocityTrend < -30 means slowing down
					bool bDecelerating = tMoveStorage.m_flAccelAlignment < -50.f
						|| tMoveStorage.m_flVelocityTrend < -30.f;

					// If there was exactly 1 reversal and we're near cover, that's likely a peek
					bool bSingleReversalPeek = iReversals == 1 && tMoveStorage.m_flCoverDist < 150.f;

					// Speed drop: current speed significantly below peak recent speed
					bool bSpeedDrop = tMoveStorage.m_flPeakRecentSpeed > 0.f
						&& flCurSpeed2D < tMoveStorage.m_flPeakRecentSpeed * 0.65f;

					tMoveStorage.m_bRetreatLikely = bDecelerating || bSpeedDrop;

					// If cover is close and we see 1 reversal OR deceleration,
					// enable peek detection even though standard threshold (2 reversals) wasn't met
					if (!tMoveStorage.m_bPeekDetected && tMoveStorage.m_flCoverDist < 150.f
						&& (bSingleReversalPeek || (bDecelerating && tMoveStorage.m_flCoverDist < 100.f)))
					{
						tMoveStorage.m_bPeekDetected = true;
						tMoveStorage.m_vPeekCenter = vOrigin - vVelDir * tMoveStorage.m_flCoverDist * 0.5f;
						tMoveStorage.m_flPeekRadius = tMoveStorage.m_flCoverDist * 0.5f;

						float flCoverConf = std::clamp(1.f - tMoveStorage.m_flCoverDist / 150.f, 0.2f, 0.8f);
						float flDecelConf = bDecelerating ? 0.6f : 0.3f;
						tMoveStorage.m_flPeekBlend = std::max(tMoveStorage.m_flPeekBlend, flCoverConf * flDecelConf);
					}

					// Strengthen existing peek blend when cover is confirmed nearby
					if (tMoveStorage.m_bPeekDetected && tMoveStorage.m_bRetreatLikely)
					{
						// Boost blend by up to 50% when retreat is likely
						tMoveStorage.m_flPeekBlend = std::min(tMoveStorage.m_flPeekBlend * 1.5f, 0.85f);
					}
				}
			}
		}
	}

	// Ground strafe pattern detection: detect periodic A-D strafing on ground targets
	// This analyzes velocity direction changes in recent records to find oscillating patterns
	// that indicate a player is strafing left-right to dodge projectiles
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer() && tMoveStorage.m_bDirectMove)
	{
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		if (static_cast<int>(vRecords.size()) >= 6)
		{
			const int iScanMax = std::min(static_cast<int>(vRecords.size()), 24);

			// Compute velocity direction changes and find reversals
			int iReversals = 0, iSegmentLen = 0, iTotalSegLen = 0;
			int iLastSign = 0;
			float flLastYaw = 0.f;
			bool bFirstYaw = true;
			int iFirstReversalAt = 0;
			int iScanTicks = 0;

			// Track absolute yaw rate per tick for strafe amplitude measurement
			float flTotalAbsYaw = 0.f;
			int iTotalYawTicks = 0;

			for (int j = 1; j < iScanMax; j++)
			{
				auto& r1 = vRecords[j - 1];
				auto& r2 = vRecords[j];

				float flY1 = Math::VectorAngles(r1.m_vDirection).y;
				float flY2 = Math::VectorAngles(r2.m_vDirection).y;
				int jTicks = std::max(TIME_TO_TICKS(r1.m_flSimTime - r2.m_flSimTime), 1);
				float flDelta = Math::NormalizeAngle(flY1 - flY2);

				iScanTicks += jTicks;
				int iSign = (flDelta > 0.5f) ? 1 : (flDelta < -0.5f) ? -1 : 0;
				if (!iSign) { iSegmentLen += jTicks; continue; }

				// Accumulate absolute yaw for amplitude measurement
				flTotalAbsYaw += fabsf(flDelta);
				iTotalYawTicks += jTicks;

				if (iLastSign && iSign != iLastSign && iSegmentLen >= 2)
				{
					iReversals++;
					iTotalSegLen += iSegmentLen;
					if (iReversals == 1)
						iFirstReversalAt = iScanTicks - jTicks;
					iSegmentLen = jTicks;
				}
				else
					iSegmentLen += jTicks;

				iLastSign = iSign;
			}

			if (iReversals >= 2)
			{
				tMoveStorage.m_bGroundStrafeDetected = true;
				tMoveStorage.m_iStrafeHalfPeriod = iTotalSegLen / iReversals;
				tMoveStorage.m_iStrafePhaseTicks = iFirstReversalAt;
				tMoveStorage.m_iStrafeReversalCount = iReversals;
				tMoveStorage.m_flStrafeYawAmplitude = iTotalYawTicks > 0
					? flTotalAbsYaw / float(iTotalYawTicks) : 0.f;
			}
		}
	}

	// Historical direction volatility: measures how often the target changes direction
	// in recent records. High volatility = unpredictable dodger, low = straight/smooth
	// Used by RunTick to dampen yaw prediction for chaotic targets
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		if (static_cast<int>(vRecords.size()) >= 4)
		{
			const int iVolScan = std::min(static_cast<int>(vRecords.size()), 16);
			int iVelDirChanges = 0;
			int iInputDirChanges = 0;
			Vec3 vLastDir = {};
			Vec3 vLastInputDir = {};
			for (int j = 0; j < iVolScan; j++)
			{
				// Check velocity direction changes
				Vec3 vVel = vRecords[j].m_vVelocity;
				Vec3 vDir = vVel.Normalized2D();
				if (j > 0 && !vLastDir.IsZero() && !vDir.IsZero())
				{
					float flDot = vDir.Dot(vLastDir);
					if (flDot < 0.85f) iVelDirChanges++;      // moderate direction change
					if (flDot < 0.5f) iVelDirChanges++;       // sharp turn — double weight
					if (flDot < 0.0f) iVelDirChanges += 2;    // reversal — triple weight total
				}
				vLastDir = vDir;

				// Also check input direction (m_vDirection) for direction changes
				// A-D strafing changes input direction instantly while engine friction
				// smooths velocity transitions, so velocity-only detection misses rapid strafing
				Vec3 vInputDirRaw = vRecords[j].m_vDirection;
				Vec3 vInputDir = vInputDirRaw.Normalized2D();
				if (j > 0 && !vLastInputDir.IsZero() && !vInputDir.IsZero())
				{
					float flDot = vInputDir.Dot(vLastInputDir);
					if (flDot < 0.85f) iInputDirChanges++;
					if (flDot < 0.5f) iInputDirChanges++;
					if (flDot < 0.0f) iInputDirChanges += 2;
				}
				vLastInputDir = vInputDir;
			}
			// Use the higher of velocity and input direction changes
			int iDirChanges = std::max(iVelDirChanges, iInputDirChanges);
			tMoveStorage.m_flHistoricalDirVolatility = float(iDirChanges) / float(iVolScan);
		}
	}

	const int iStrafeSamples = tMoveStorage.m_bDirectMove
		? Vars::Aimbot::Projectile::GroundSamples.Value
		: Vars::Aimbot::Projectile::AirSamples.Value;

	// calculate strafe if desired
	bool bCalculated = bStrafe ? StrafePrediction(tMoveStorage, iStrafeSamples) : false;

	// Symmetric strafe detection: when A-D strafing produces near-zero average yaw,
	// the movement sim predicts a straight line in the instantaneous velocity direction.
	// This is maximally wrong when the target reverses. Fix: compute the mean velocity
	// direction across strafe cycles and enable sinusoidal yaw modulation in RunTick.
	if (tMoveStorage.m_bGroundStrafeDetected && fabsf(tMoveStorage.m_flAverageYaw) < 1.5f
		&& tMoveStorage.m_flStrafeYawAmplitude > 0.5f
		&& pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		tMoveStorage.m_bSymmetricStrafe = true;

		// Compute mean velocity direction over multiple strafe cycles
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		int iAvgWindow = std::min(static_cast<int>(vRecords.size()),
			std::max(tMoveStorage.m_iStrafeHalfPeriod * 4, 12));

		Vec3 vVelSum = {};
		for (int j = 0; j < iAvgWindow; j++)
			vVelSum = vVelSum + vRecords[j].m_vVelocity;

		Vec3 vMeanVel = vVelSum * (1.f / float(iAvgWindow));
		if (vMeanVel.Length2D() > 5.f)
			tMoveStorage.m_vMeanVelocityDir = vMeanVel.Normalized2D();
	}

	// really hope this doesn't work like shit
	if (bHitchance && bCalculated && !pPlayer->m_vecVelocity().IsZero() && Vars::Aimbot::Projectile::HitChance.Value)
	{
		const auto& vRecords = m_mRecords[pPlayer->entindex()];
		const auto iSamples = vRecords.size();

		float flCurrentChance = 1.f, flAverageYaw = 0.f;
		for (size_t i = 0; i < iSamples; i++)
		{
			if (vRecords.size() <= i + 2)
				break;

			const auto& pRecord1 = vRecords[i], &pRecord2 = vRecords[i + 1];
			const float flYaw1 = Math::VectorAngles(pRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(pRecord2.m_vDirection).y;
			const float flTime1 = pRecord1.m_flSimTime, flTime2 = pRecord2.m_flSimTime;
			const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

			float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2) / iTicks;
			flAverageYaw += flYaw;
			if (tMoveStorage.m_MoveData.m_flMaxSpeed)
				flYaw *= std::clamp(pRecord1.m_vVelocity.Length2D() / tMoveStorage.m_MoveData.m_flMaxSpeed, 0.f, 1.f);

			if ((i + 1) % iStrafeSamples == 0 || i == iSamples - 1)
			{
				flAverageYaw /= i % iStrafeSamples + 1;
				if (fabsf(tMoveStorage.m_flAverageYaw - flAverageYaw) > 0.5f)
					flCurrentChance -= 1.f / ((iSamples - 1) / float(iStrafeSamples) + 1);
				flAverageYaw = 0.f;
			}
		}

		if (flCurrentChance < Vars::Aimbot::Projectile::HitChance.Value / 100)
		{
			SDK::Output("MovementSimulation", std::format("Hitchance ({}% < {}%)", flCurrentChance * 100, Vars::Aimbot::Projectile::HitChance.Value).c_str(), { 80, 200, 120 }, Vars::Debug::Logging.Value);

			tMoveStorage.m_bFailed = true;
			return false;
		}
	}

	for (int i = 0; i < H::Entities.GetChoke(pPlayer->entindex()); i++)
		RunTick(tMoveStorage);

	// Reset simulated tick counter after choke compensation so that temporal
	// decay only counts actual prediction ticks, not network-delay compensation.
	// Without this, choke ticks inflate the decay timer and cause severe
	// under-prediction for long-flight projectiles (rockets, grenades).
	tMoveStorage.m_iSimulatedTicks = 0;
	tMoveStorage.m_flRecentDirChanges = 0.f;
	tMoveStorage.m_flAccelVariance = 0.f;

	return true;
}

bool CMovementSimulation::SetupMoveData(MoveStorage& tMoveStorage)
{
	if (!tMoveStorage.m_pPlayer)
		return false;

	tMoveStorage.m_MoveData.m_bFirstRunOfFunctions = false;
	tMoveStorage.m_MoveData.m_bGameCodeMovedPlayer = false;
	tMoveStorage.m_MoveData.m_nPlayerHandle = reinterpret_cast<IHandleEntity*>(tMoveStorage.m_pPlayer)->GetRefEHandle();

	tMoveStorage.m_MoveData.m_vecAbsOrigin = tMoveStorage.m_pPlayer->m_vecOrigin();
	tMoveStorage.m_MoveData.m_vecVelocity = tMoveStorage.m_pPlayer->m_vecVelocity();
	tMoveStorage.m_MoveData.m_flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer);
	tMoveStorage.m_MoveData.m_flClientMaxSpeed = tMoveStorage.m_MoveData.m_flMaxSpeed;

	if (!tMoveStorage.m_MoveData.m_vecVelocity.To2D().IsZero())
	{
		int iIndex = tMoveStorage.m_pPlayer->entindex();
		if (iIndex == I::EngineClient->GetLocalPlayer() && G::CurrentUserCmd)
			tMoveStorage.m_MoveData.m_vecViewAngles = G::CurrentUserCmd->viewangles;
		else
		{
			if (!tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
				tMoveStorage.m_MoveData.m_vecViewAngles = { 0.f, Math::VectorAngles(tMoveStorage.m_MoveData.m_vecVelocity).y, 0.f };
			else
				tMoveStorage.m_MoveData.m_vecViewAngles = H::Entities.GetEyeAngles(iIndex);
		}

		const auto& vRecords = m_mRecords[tMoveStorage.m_pPlayer->entindex()];
		if (!vRecords.empty())
		{
			auto& tRecord = vRecords.front();
			if (!tRecord.m_vDirection.IsZero())
			{
				s_tDummyCmd.forwardmove = tRecord.m_vDirection.x;
				s_tDummyCmd.sidemove = -tRecord.m_vDirection.y;
				s_tDummyCmd.upmove = tRecord.m_vDirection.z;
				SDK::FixMovement(&s_tDummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
				tMoveStorage.m_MoveData.m_flForwardMove = s_tDummyCmd.forwardmove;
				tMoveStorage.m_MoveData.m_flSideMove = s_tDummyCmd.sidemove;
				tMoveStorage.m_MoveData.m_flUpMove = s_tDummyCmd.upmove;
			}
		}
	}

	tMoveStorage.m_MoveData.m_vecAngles = tMoveStorage.m_MoveData.m_vecOldAngles = tMoveStorage.m_MoveData.m_vecViewAngles;
	if (auto pConstraintEntity = tMoveStorage.m_pPlayer->m_hConstraintEntity().Get())
		tMoveStorage.m_MoveData.m_vecConstraintCenter = pConstraintEntity->GetAbsOrigin();
	else
		tMoveStorage.m_MoveData.m_vecConstraintCenter = tMoveStorage.m_pPlayer->m_vecConstraintCenter();
	tMoveStorage.m_MoveData.m_flConstraintRadius = tMoveStorage.m_pPlayer->m_flConstraintRadius();
	tMoveStorage.m_MoveData.m_flConstraintWidth = tMoveStorage.m_pPlayer->m_flConstraintWidth();
	tMoveStorage.m_MoveData.m_flConstraintSpeedFactor = tMoveStorage.m_pPlayer->m_flConstraintSpeedFactor();

	tMoveStorage.m_flPredictedDelta = GetPredictedDelta(tMoveStorage.m_pPlayer);
	tMoveStorage.m_flSimTime = tMoveStorage.m_pPlayer->m_flSimulationTime();
	tMoveStorage.m_flPredictedSimTime = tMoveStorage.m_flSimTime + tMoveStorage.m_flPredictedDelta;
	tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();

	return true;
}

static inline float GetGravity()
{
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");

	return sv_gravity->GetFloat();
}

static inline float GetFrictionScale(float flVelocityXY, float flTurn, float flVelocityZ, float flMin = 50.f, float flMax = 150.f)
{
	if (0.f >= flVelocityZ || flVelocityZ > 250.f)
		return 1.f;

	static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
	float flScale = std::max(sv_airaccelerate->GetFloat(), 1.f);
	flMin *= flScale, flMax *= flScale;

	// entity friction will be 0.25f if velocity is between 0.f and 250.f
	return Math::RemapVal(fabsf(flVelocityXY * flTurn), flMin, flMax, 1.f, 0.25f);
}

//#define VISUALIZE_RECORDS
#ifdef VISUALIZE_RECORDS
static inline void VisualizeRecords(MoveData& tRecord1, MoveData& tRecord2, Color_t tColor, float flStraightFuzzyValue)
{
	static int iStaticTickcount = I::GlobalVars->tickcount;
	if (I::GlobalVars->tickcount != iStaticTickcount)
	{
		G::LineStorage.clear();
		iStaticTickcount = I::GlobalVars->tickcount;
	}

	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);
	const float flYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	const bool bStraight = fabsf(flYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue; // dumb way to get straight bool

	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord2.m_vOrigin), I::GlobalVars->curtime + 5.f, tColor);
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + Vec3(0, 0, 5)), I::GlobalVars->curtime + 5.f, tColor);
	if (!bStraight && flYaw)
	{
		Vec3 vVelocity = tRecord1.m_vVelocity.Normalized2D() * 5;
		vVelocity = Math::RotatePoint(vVelocity, {}, { 0, flYaw > 0 ? 90.f : -90.f, 0 });
		if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == MoveEnum::Air)
			vVelocity /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), flYaw, tRecord1.m_vVelocity.z + GetGravity() * TICK_INTERVAL, 0.f, 56.f);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tRecord1.m_vOrigin, tRecord1.m_vOrigin + vVelocity), I::GlobalVars->curtime + 5.f, tColor);
	}
}
#endif

static inline bool GetYawDifference(MoveData& tRecord1, MoveData& tRecord2, bool bStart, float* pYaw, float flStraightFuzzyValue, int iMaxChanges = 0, int iMaxChangeTime = 0, float flMaxSpeed = 0.f)
{
	const float flYaw1 = Math::VectorAngles(tRecord1.m_vDirection).y, flYaw2 = Math::VectorAngles(tRecord2.m_vDirection).y;
	const float flTime1 = tRecord1.m_flSimTime, flTime2 = tRecord2.m_flSimTime;
	const int iTicks = std::max(TIME_TO_TICKS(flTime1 - flTime2), 1);

	*pYaw = Math::NormalizeAngle(flYaw1 - flYaw2);
	if (flMaxSpeed && tRecord1.m_iMode != MoveEnum::Air)
		*pYaw *= std::clamp(tRecord1.m_vVelocity.Length2D() / flMaxSpeed, 0.f, 1.f);
	if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::CalculateIncrease && tRecord1.m_iMode == 1)
		*pYaw /= GetFrictionScale(tRecord1.m_vVelocity.Length2D(), *pYaw, tRecord1.m_vVelocity.z + GetGravity() * TICK_INTERVAL, 0.f, 56.f);
	if (fabsf(*pYaw) > 45.f)
		return false;

	static int iChanges, iStart;

	static int iStaticSign = 0;
	const int iLastSign = iStaticSign;
	const int iCurrSign = iStaticSign = *pYaw ? sign(*pYaw) : iStaticSign;

	static bool bStaticZero = false;
	const bool iLastZero = bStaticZero;
	const bool iCurrZero = bStaticZero = !*pYaw;

	const bool bChanged = iCurrSign != iLastSign || iCurrZero && iLastZero;
	const bool bStraight = fabsf(*pYaw) * tRecord1.m_vVelocity.Length2D() * iTicks < flStraightFuzzyValue; // dumb way to get straight bool

	if (bStart)
	{
		iChanges = 0, iStart = TIME_TO_TICKS(flTime1);
		if (bStraight && ++iChanges > iMaxChanges)
			return false;
		return true;
	}
	else
	{
		if ((bChanged || bStraight) && ++iChanges > iMaxChanges)
			return false;
		return iChanges && iStart - TIME_TO_TICKS(flTime2) > iMaxChangeTime ? false : true;
	}
}

void CMovementSimulation::GetAverageYaw(MoveStorage& tMoveStorage, int iSamples)
{
	auto pPlayer = tMoveStorage.m_pPlayer;
	auto& vRecords = m_mRecords[pPlayer->entindex()];
	if (vRecords.empty())
		return;

	bool bGround = tMoveStorage.m_bDirectMove; int iMinimumStrafes = 4;
	float flMaxSpeed = SDK::MaxSpeed(tMoveStorage.m_pPlayer, false, true);
	float flLowMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumDistance.Value : Vars::Aimbot::Projectile::AirLowMinimumDistance.Value;
	float flLowMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundLowMinimumSamples.Value : Vars::Aimbot::Projectile::AirLowMinimumSamples.Value;
	float flHighMinimumDistance = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumDistance.Value : Vars::Aimbot::Projectile::AirHighMinimumDistance.Value;
	float flHighMinimumSamples = bGround ? Vars::Aimbot::Projectile::GroundHighMinimumSamples.Value : Vars::Aimbot::Projectile::AirHighMinimumSamples.Value;

	float flAverageYaw = 0.f, flWeightSum = 0.f; int iTicks = 0, iSkips = 0;
	float flLastYaw = 0.f; bool bHasLastYaw = false;
	float flAngularAccel = 0.f; int iAccelSamples = 0;
	iSamples = std::min(iSamples, int(vRecords.size()));
	size_t i = 1; for (; i < iSamples; i++)
	{
		auto& tRecord1 = vRecords[i - 1];
		auto& tRecord2 = vRecords[i];
		if (tRecord1.m_iMode != tRecord2.m_iMode)
		{
			iSkips++;
			continue;
		}

		bGround = tRecord1.m_iMode != MoveEnum::Air;
		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		int iMaxChanges = bGround ? Vars::Aimbot::Projectile::GroundMaxChanges.Value : Vars::Aimbot::Projectile::AirMaxChanges.Value;
		int iMaxChangeTime = bGround ? Vars::Aimbot::Projectile::GroundMaxChangeTime.Value : Vars::Aimbot::Projectile::AirMaxChangeTime.Value;
		iMinimumStrafes = 4 + iMaxChanges;
#ifdef VISUALIZE_RECORDS
		VisualizeRecords(tRecord1, tRecord2, { 255, 0, 0 }, flStraightFuzzyValue);
#endif

		float flYaw = 0.f;
		bool bResult = GetYawDifference(tRecord1, tRecord2, !iTicks, &flYaw, flStraightFuzzyValue, iMaxChanges, iMaxChangeTime, flMaxSpeed);
		SDK::Output("GetYawDifference", std::format("{} ({}): {}, {}", i, iTicks, flYaw, bResult).c_str(), { 50, 127, 75 }, Vars::Debug::Logging.Value);
		if (!bResult)
			break;

		// Weighted moving average: recent frames get exponentially higher weight
		float flWeight = 1.f / (1.f + float(i - 1) * 0.15f);
		flAverageYaw += flYaw * flWeight;
		flWeightSum += flWeight;

		// Track angular acceleration (second derivative of yaw)
		if (bHasLastYaw)
		{
			float flYawDelta = flYaw - flLastYaw;
			flAngularAccel += flYawDelta;
			iAccelSamples++;
		}
		flLastYaw = flYaw;
		bHasLastYaw = true;

		iTicks += std::max(TIME_TO_TICKS(tRecord1.m_flSimTime - tRecord2.m_flSimTime), 1);
	}
#ifdef VISUALIZE_RECORDS
	size_t i2 = i; for (; i2 < iSamples; i2++)
	{
		auto& tRecord1 = vRecords[i2 - 1];
		auto& tRecord2 = vRecords[i2];

		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		VisualizeRecords(tRecord1, tRecord2, { 0, 0, 0 }, flStraightFuzzyValue);
	}
	/*
	for (; i2 < vRecords.size(); i2++)
	{
		auto& tRecord1 = vRecords[i2 - 1];
		auto& tRecord2 = vRecords[i2];

		float flStraightFuzzyValue = bGround ? Vars::Aimbot::Projectile::GroundStraightFuzzyValue.Value : Vars::Aimbot::Projectile::AirStraightFuzzyValue.Value;
		VisualizeRecords(tRecord1, tRecord2, { 0, 0, 0, 100 }, flStraightFuzzyValue);
	}
	*/
#endif
	if (i <= size_t(iMinimumStrafes + iSkips)) // valid strafes not high enough
		return;

	int iMinimum = flLowMinimumSamples;
	if (pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
	{
		float flDistance = 0.f;
		if (auto pLocal = H::Entities.GetLocal())
			flDistance = pLocal->m_vecOrigin().DistTo(tMoveStorage.m_pPlayer->m_vecOrigin());
		iMinimum = flDistance < flLowMinimumDistance ? flLowMinimumSamples : Math::RemapVal(flDistance, flLowMinimumDistance, flHighMinimumDistance, flLowMinimumSamples + 1, flHighMinimumSamples);
	}

	flAverageYaw = flWeightSum > 0.f ? flAverageYaw / flWeightSum : 0.f;

	// Apply angular acceleration correction for turn intent prediction
	if (iAccelSamples > 0)
	{
		float flAvgAccel = flAngularAccel / float(iAccelSamples);
		// Blend in acceleration: helps predict if player is speeding up or slowing their turn
		flAverageYaw += flAvgAccel * 0.25f;
	}

	if (fabsf(flAverageYaw) < 0.36f)
		return;

	tMoveStorage.m_flAverageYaw = flAverageYaw;
	SDK::Output("MovementSimulation", std::format("flAverageYaw calculated to {} from {} ({}) {}", flAverageYaw, iTicks, iMinimum, pPlayer->entindex() == I::EngineClient->GetLocalPlayer() ? "(local)" : "").c_str(), { 100, 255, 150 }, Vars::Debug::Logging.Value);

	// Air strafe reversal detection - analyze recent air movement for direction changes
	if (!tMoveStorage.m_bDirectMove && Vars::Aimbot::Projectile::AirReversalPrediction.Value)
	{
		const int iMinSegment = Vars::Aimbot::Projectile::AirReversalMinSegment.Value;

		// Independent scan: compute raw yaw deltas and find sign changes
		int iCurSign = 0, iSegmentLen = 0, iNumSegments = 0;
		int iTotalChangeInterval = 0, iNumChanges = 0;
		int iFirstChangeAt = 0; // ticks from most recent record to first observed change
		float flRecentSegYaw = 0.f;
		int iRecentSegTicks = 0;
		bool bRecentSegDone = false;

		int iScanTicks = 0;
		for (size_t j = 1; j < vRecords.size() && j < 66; j++)
		{
			auto& r1 = vRecords[j - 1];
			auto& r2 = vRecords[j];
			if (r1.m_iMode != MoveEnum::Air || r2.m_iMode != MoveEnum::Air)
				break; // only analyze contiguous air segment

			const float flY1 = Math::VectorAngles(r1.m_vDirection).y;
			const float flY2 = Math::VectorAngles(r2.m_vDirection).y;
			const int jTicks = std::max(TIME_TO_TICKS(r1.m_flSimTime - r2.m_flSimTime), 1);
			float flDelta = Math::NormalizeAngle(flY1 - flY2);

			iScanTicks += jTicks;
			int iNewSign = flDelta > 0.1f ? 1 : (flDelta < -0.1f ? -1 : 0);
			if (!iNewSign)
			{
				iSegmentLen += jTicks;
				continue;
			}

			if (iCurSign && iNewSign != iCurSign)
			{
				// Direction change detected
				if (iSegmentLen >= iMinSegment)
				{
					iNumSegments++;
					if (!bRecentSegDone)
					{
						// First segment (most recent) is complete
						iFirstChangeAt = iScanTicks - jTicks;
						bRecentSegDone = true;
					}
					iTotalChangeInterval += iSegmentLen;
					iNumChanges++;
				}
				iSegmentLen = jTicks;
			}
			else
				iSegmentLen += jTicks;

			// Accumulate most recent segment yaw (before first direction change)
			if (!bRecentSegDone)
			{
				float flW = 1.f / (1.f + float(j - 1) * 0.15f);
				flRecentSegYaw += flDelta * flW;
				iRecentSegTicks++;
			}

			iCurSign = iNewSign;
		}

		// Count the trailing segment
		if (iSegmentLen >= iMinSegment)
			iNumSegments++;

		if (iNumChanges >= 1 && iRecentSegTicks > 0)
		{
			tMoveStorage.m_bAirReversalDetected = true;
			tMoveStorage.m_flRecentAirYaw = flRecentSegYaw / float(iRecentSegTicks);
			tMoveStorage.m_iAirChangeInterval = iNumChanges > 0 ? iTotalChangeInterval / iNumChanges : 0;
			tMoveStorage.m_iTicksSinceChange = iFirstChangeAt;
			tMoveStorage.m_iSimulatedTicks = 0;

			// Override average yaw with most recent segment direction
			// This prioritizes the current strafe direction over the blended average
			if (fabsf(tMoveStorage.m_flRecentAirYaw) > 0.36f)
				tMoveStorage.m_flAverageYaw = tMoveStorage.m_flRecentAirYaw;

			SDK::Output("AirReversal", std::format("detected: recent={:.2f} interval={} sinceLast={} segments={}",
				tMoveStorage.m_flRecentAirYaw, tMoveStorage.m_iAirChangeInterval,
				tMoveStorage.m_iTicksSinceChange, iNumSegments).c_str(), { 255, 200, 50 }, Vars::Debug::Logging.Value);
		}
	}
}

bool CMovementSimulation::StrafePrediction(MoveStorage& tMoveStorage, int iSamples)
{
	if (tMoveStorage.m_bDirectMove
		? !(Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::Ground)
		: !(Vars::Aimbot::Projectile::StrafePrediction.Value & Vars::Aimbot::Projectile::StrafePredictionEnum::Air))
		return false;

	GetAverageYaw(tMoveStorage, iSamples);
	return true;
}

bool CMovementSimulation::SetDuck(MoveStorage& tMoveStorage, bool bDuck) // this only touches origin, bounds
{
	if (bDuck == tMoveStorage.m_pPlayer->m_bDucked())
		return true;

	auto pGameRules = I::TFGameRules();
	auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
	float flScale = tMoveStorage.m_pPlayer->m_flModelScale();

	if (!tMoveStorage.m_pPlayer->IsOnGround())
	{
		Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flScale;
		Vec3 vDuckHullMins = (pViewVectors ? pViewVectors->m_vDuckHullMin : Vec3(-24, -24, 0)) * flScale;
		Vec3 vDuckHullMaxs = (pViewVectors ? pViewVectors->m_vDuckHullMax : Vec3(24, 24, 62)) * flScale;

		if (bDuck)
			tMoveStorage.m_MoveData.m_vecAbsOrigin += (vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins);
		else
		{
			Vec3 vOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin - ((vHullMaxs - vHullMins) - (vDuckHullMaxs - vDuckHullMins));

			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};
			SDK::TraceHull(vOrigin, vOrigin, vHullMins, vHullMaxs, tMoveStorage.m_pPlayer->SolidMask(), &filter, &trace);
			if (trace.DidHit())
				return false;

			tMoveStorage.m_MoveData.m_vecAbsOrigin = vOrigin;
		}
	}
	tMoveStorage.m_pPlayer->m_bDucked() = bDuck;

	return true;
}

void CMovementSimulation::SetBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	// fixes issues with origin compression
	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vHullMax = Vec3(24, 24, 82) - PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0) + PLAYER_ORIGIN_COMPRESSION;
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62) - PLAYER_ORIGIN_COMPRESSION;
		}
	}
}

void CMovementSimulation::RestoreBounds(CTFPlayer* pPlayer)
{
	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
		return;

	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pViewVectors = pGameRules->GetViewVectors())
		{
			pViewVectors->m_vHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vHullMax = Vec3(24, 24, 82);
			pViewVectors->m_vDuckHullMin = Vec3(-24, -24, 0);
			pViewVectors->m_vDuckHullMax = Vec3(24, 24, 62);
		}
	}
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, std::function<void(CMoveData&)>* pCallback)
{
	if (tMoveStorage.m_bFailed || !tMoveStorage.m_pPlayer || !tMoveStorage.m_pPlayer->IsPlayer())
		return;

	if (bPath)
		tMoveStorage.m_vPath.push_back(tMoveStorage.m_MoveData.m_vecAbsOrigin);

	// make sure frametime and prediction vars are right
	I::Prediction->m_bInPrediction = true;
	I::Prediction->m_bFirstTimePredicted = false;
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
	SetBounds(tMoveStorage.m_pPlayer);

	float flCorrection = 0.f;
	tMoveStorage.m_iSimulatedTicks++;

	// Track direction changes during simulation for confidence decay
	if (!tMoveStorage.m_vLastSimVelocity.IsZero())
	{
		Vec3 vCurVel = tMoveStorage.m_MoveData.m_vecVelocity.To2D();
		Vec3 vLastVel = tMoveStorage.m_vLastSimVelocity.To2D();
		if (!vCurVel.IsZero() && !vLastVel.IsZero())
		{
			float flDot = vCurVel.Normalized2D().Dot(vLastVel.Normalized2D());
			if (flDot < 0.95f) // direction changed significantly
				tMoveStorage.m_flRecentDirChanges += 1.f - flDot;

			float flAccelDiff = fabsf(vCurVel.Length2D() - vLastVel.Length2D());
			tMoveStorage.m_flAccelVariance = tMoveStorage.m_flAccelVariance * 0.9f + flAccelDiff * 0.1f;
		}
	}
	tMoveStorage.m_vLastSimVelocity = tMoveStorage.m_MoveData.m_vecVelocity;

	// Temporal decay: reduce prediction confidence for long simulations (long-range projectiles)
	// Adaptive parameters based on target velocity behavior
	float flTemporalDecay = 1.f;
	{
		// Adaptive decay start: tuned for projectile prediction windows
		// Rockets need ~50 ticks, grenades ~60, arrows ~30. Decay must not
		// activate within the useful prediction window for these weapons.
		int iDecayStart;
		float flBaseRate;
		if (tMoveStorage.m_bSymmetricStrafe && tMoveStorage.m_iStrafeHalfPeriod > 0)
		{
			// Symmetric A-D strafe dodger: target oscillates unpredictably.
			// Use early, moderate decay so prediction stays close to current position
			// for long-flight projectiles, while still being aggressive enough for short flights.
			iDecayStart = 12;   // ~0.18s — before first expected reversal for most strafe periods
			flBaseRate = 0.010f;
		}
		else if (tMoveStorage.m_flVelocityTrend < -50.f && tMoveStorage.m_flInitialSpeedRatio > 0.4f)
		{
			iDecayStart = 15;   // Decelerating target: early decay to prevent over-prediction on stops/dodges
			flBaseRate = 0.012f;
		}
		else if (tMoveStorage.m_flInitialSpeedRatio > 0.8f && tMoveStorage.m_flVelocityConsistency < 0.8f)
		{
			iDecayStart = 20;   // Fast erratic: decay at ~0.30s
			flBaseRate = 0.008f;
		}
		else if (tMoveStorage.m_flInitialSpeedRatio < 0.4f)
		{
			iDecayStart = 50;   // Slow target: very predictable, decay late (~0.75s)
			flBaseRate = 0.004f;
		}
		else if (tMoveStorage.m_flVelocityConsistency > 0.95f
			&& fabsf(tMoveStorage.m_flAverageYaw) < 0.5f
			&& tMoveStorage.m_flHistoricalDirVolatility < 0.2f
			&& !tMoveStorage.m_bSymmetricStrafe)
		{
			// Highly predictable straight-line target: near-zero direction changes,
			// consistent speed, no strafe pattern. Delay decay significantly to prevent
			// under-prediction that causes UNDER_PREDICT misses for long-flight projectiles.
			iDecayStart = 80;   // ~1.20s — well beyond most projectile flight times
			flBaseRate = 0.003f;
		}
		else
		{
			iDecayStart = 35;   // Normal: default (~0.53s)
			flBaseRate = 0.006f;
		}

		// Volatility-aware decay: targets with high direction volatility should decay faster
		// because their behavior is unpredictable. This is in addition to the category selection above.
		if (tMoveStorage.m_flHistoricalDirVolatility > 0.5f)
		{
			float flVolBoost = (tMoveStorage.m_flHistoricalDirVolatility - 0.5f) * 0.5f;
			flBaseRate += flBaseRate * flVolBoost;
			iDecayStart = std::max(iDecayStart - static_cast<int>(flVolBoost * 15.f), 10);
		}

		if (tMoveStorage.m_iSimulatedTicks > iDecayStart)
		{
			int iExcess = tMoveStorage.m_iSimulatedTicks - iDecayStart;
			float flDecayRate = flBaseRate
				+ tMoveStorage.m_flRecentDirChanges * 0.004f
				+ tMoveStorage.m_flAccelVariance * 0.00005f
				+ (1.f - tMoveStorage.m_flVelocityConsistency) * 0.005f;

			// Gentler exponential decay: slower projectiles (rockets, pills) need longer
			// accurate prediction windows — overly aggressive decay causes under-prediction
			float flExponent = float(iExcess) * flDecayRate;
			// Mild progressive acceleration for very long flights (>80 excess ticks)
			if (iExcess > 80)
				flExponent += float(iExcess - 80) * flDecayRate * 0.2f;
			flTemporalDecay = expf(-flExponent);

			// Floor: maintain minimum prediction to avoid total movement death
			flTemporalDecay = std::max(flTemporalDecay, 0.30f);
		}
	}

	// Combined prediction scale: initial speed ratio * temporal decay
	float flPredScale = tMoveStorage.m_flInitialSpeedRatio * flTemporalDecay;

	// ===== Physics-based speed envelope model =====
	// Replaces static forwardmove dampening with acceleration-aware velocity prediction.
	// Models target speed evolution: v(t) = v0 + (a_align / alpha) * (1 - e^(-alpha*t))
	// where a_align is the observed acceleration projected onto velocity direction.
	// Naturally handles deceleration (braking targets), acceleration, and steady-state.
	float flSpeedEnvelope = 1.f;
	if (tMoveStorage.m_flInitialSimSpeed2D > 10.f)
	{
		float flT = float(tMoveStorage.m_iSimulatedTicks) * TICK_INTERVAL;

		if (fabsf(tMoveStorage.m_flAccelAlignment) > 5.f)
		{
			// Damping rate alpha: controls how quickly acceleration effect saturates
			// Higher alpha = effect saturates faster (less total speed change)
			// Lower alpha = acceleration effect builds up more over time
			const float flAlpha = 2.0f;
			float flDeltaSpeed = (tMoveStorage.m_flAccelAlignment / flAlpha) * (1.f - expf(-flAlpha * flT));
			float flPredSpeed = tMoveStorage.m_flInitialSimSpeed2D + flDeltaSpeed;
			flPredSpeed = std::clamp(flPredSpeed, 0.f, tMoveStorage.m_MoveData.m_flMaxSpeed);
			flSpeedEnvelope = flPredSpeed / tMoveStorage.m_flInitialSimSpeed2D;
		}
	}

	// Combine speed envelope with temporal decay for movement input scaling
	if (tMoveStorage.m_flBaseForwardMove != 0.f || tMoveStorage.m_flBaseSideMove != 0.f)
	{
		float flMoveFactor = flSpeedEnvelope * flTemporalDecay;

		// Straight-line preservation: targets with no yaw change and consistent speed
		// are highly predictable — preserve their movement input to prevent under-prediction
		// Exception: symmetric strafers LOOK straight (yaw ≈ 0) but actually oscillate.
		// Don't boost their prediction — they need temporal decay to work normally.
		if (fabsf(tMoveStorage.m_flAverageYaw) < 1.0f && !tMoveStorage.m_bSymmetricStrafe)
		{
			float flYawFactor = 1.f - fabsf(tMoveStorage.m_flAverageYaw);
			float flStraight = flYawFactor * tMoveStorage.m_flVelocityConsistency;
			flMoveFactor = flMoveFactor + (flSpeedEnvelope - flMoveFactor) * flStraight;
		}

		// Adaptive floor based on acceleration direction:
		// Braking targets (a_align < -30): lower floor (0.35) allows aggressive reduction
		// Normal targets: standard floor (0.70) prevents Source friction runaway
		float flMoveFloor = tMoveStorage.m_flAccelAlignment < -30.f ? 0.35f : 0.70f;
		flMoveFactor = std::max(flMoveFactor, flMoveFloor);

		tMoveStorage.m_MoveData.m_flForwardMove = tMoveStorage.m_flBaseForwardMove * flMoveFactor;
		tMoveStorage.m_MoveData.m_flSideMove = tMoveStorage.m_flBaseSideMove * flMoveFactor;
	}

	// ===== Bounded-turn yaw model =====
	// Replaces constant yaw rate + ad-hoc decay with a physically-motivated model.
	// Key formula: omega(t) = omega_0 * e^(-lambda * t)
	// where lambda = |omega_0| / theta_max
	// This bounds the total accumulated turn angle to theta_max (~90 degrees),
	// regardless of initial yaw rate. Prevents spiral path predictions while
	// maintaining accurate short-term direction prediction.
	//
	// Total angle integral: theta(T) = (omega_0 / lambda) * (1 - e^(-lambda*T)) -> theta_max
	//
	// Behavior:
	//   Low yaw rate (1 deg/tick): slow decay, nearly straight-line prediction
	//   High yaw rate (8 deg/tick): fast decay, total turn bounded at ~90 degrees
	//   This matches real player behavior: sharp dodges are short-lived
	if (tMoveStorage.m_flAverageYaw
		|| (tMoveStorage.m_bSymmetricStrafe && tMoveStorage.m_flStrafeYawAmplitude > 0.5f))
	{
		// Air strafe reversal: predict direction changes during simulation
		if (tMoveStorage.m_bAirReversalDetected && !tMoveStorage.m_bDirectMove
			&& tMoveStorage.m_iAirChangeInterval > 0)
		{
			int iTicksTotal = tMoveStorage.m_iTicksSinceChange + tMoveStorage.m_iSimulatedTicks;
			int iReversals = iTicksTotal / tMoveStorage.m_iAirChangeInterval;

			float flMag = fabsf(tMoveStorage.m_flAverageYaw);
			float flDir = sign(tMoveStorage.m_flRecentAirYaw);
			tMoveStorage.m_flAverageYaw = flMag * flDir * (iReversals % 2 == 0 ? 1.f : -1.f);
		}

		// Ground strafe reversal: sinusoidal yaw modulation for A-D strafing on ground
		// Replaces the binary flip with smooth oscillation that matches how Source engine
		// physics translates A-D input into curved movement paths.
		// For symmetric strafing (average yaw ≈ 0), provides the oscillation that would
		// otherwise be completely absent from the prediction.
		if (tMoveStorage.m_bGroundStrafeDetected && tMoveStorage.m_bDirectMove
			&& tMoveStorage.m_iStrafeHalfPeriod > 0)
		{
			float flPhase = float(tMoveStorage.m_iStrafePhaseTicks + tMoveStorage.m_iSimulatedTicks)
				* 3.14159265f / float(tMoveStorage.m_iStrafeHalfPeriod);
			float flSin = sinf(flPhase);

			if (tMoveStorage.m_bSymmetricStrafe)
			{
				// Symmetric strafe: use detected yaw amplitude with sinusoidal oscillation
				// This provides active direction prediction even when average yaw ≈ 0
				tMoveStorage.m_flAverageYaw = tMoveStorage.m_flStrafeYawAmplitude * flSin;
			}
			else
			{
				// Asymmetric strafe: modulate existing yaw magnitude with phase
				tMoveStorage.m_flAverageYaw = fabsf(tMoveStorage.m_flAverageYaw) * flSin;
			}

			// Amplitude decay: longer flights cross more reversals, reducing confidence
			// in the predicted strafe phase. Decay pulls aim toward center of oscillation.
			float flReversalsInFlight = float(tMoveStorage.m_iSimulatedTicks)
				/ float(tMoveStorage.m_iStrafeHalfPeriod);
			if (flReversalsInFlight > 1.f)
			{
				float flStrafeDampen = expf(-0.20f * (flReversalsInFlight - 1.f));
				tMoveStorage.m_flAverageYaw *= flStrafeDampen;
			}
		}

		float flMult = 1.f;
		if (!tMoveStorage.m_bDirectMove && !tMoveStorage.m_pPlayer->InCond(TF_COND_SHIELD_CHARGE))
		{
			flCorrection = 90.f * sign(tMoveStorage.m_flAverageYaw);
			if (Vars::Aimbot::Projectile::MovesimFrictionFlags.Value & Vars::Aimbot::Projectile::MovesimFrictionFlagsEnum::RunReduce)
				flMult = GetFrictionScale(tMoveStorage.m_MoveData.m_vecVelocity.Length2D(), tMoveStorage.m_flAverageYaw, tMoveStorage.m_MoveData.m_vecVelocity.z + GetGravity() * TICK_INTERVAL);
		}

		// Compute effective yaw rate with friction scaling
		float flEffYaw = tMoveStorage.m_flAverageYaw * flMult;

		// === Direction volatility dampening ===
		// For targets with high historical direction volatility (frequent direction changes),
		// dampen the yaw rate prediction toward zero. Rationale: against unpredictable dodgers,
		// any committed direction prediction is wrong ~50% of the time. Reducing the yaw
		// prediction makes the aim point more conservative (closer to straight extrapolation),
		// which minimizes the WORST-case miss distance.
		// Scaling: volatility 0.3 → no dampening, 1.0+ → 60% reduction
		if (tMoveStorage.m_flHistoricalDirVolatility > 0.3f)
		{
			float flVolatilityExcess = tMoveStorage.m_flHistoricalDirVolatility - 0.3f;
			float flDampen = 1.f / (1.f + flVolatilityExcess * 2.0f);
			flEffYaw *= flDampen;
		}

		// Bounded-turn decay rate: lambda = |effective_yaw| / max_turn_angle
		// Max turn angle ~90 degrees: most dodges/strafes involve at most a quarter-turn
		// Higher yaw rate → proportionally faster decay → same bounded total angle
		const float flMaxTurnAngle = 90.f;
		float flLambda = fabsf(flEffYaw) / flMaxTurnAngle;

		// Minimum decay rate: even for slow turns, don't predict indefinite turning
		// 0.015/tick ≈ half-life of 46 ticks (0.69s) for very gentle curves
		flLambda = std::max(flLambda, 0.015f);

		// Lateral acceleration influence: high lateral acceleration confirms active turning,
		// reduce decay rate to maintain turn prediction; low lateral acceleration means
		// the turn was impulsive and will end sooner
		if (tMoveStorage.m_flAccelLateral > 50.f)
		{
			float flLateralFactor = std::clamp(tMoveStorage.m_flAccelLateral / 200.f, 0.f, 1.f);
			// For volatile targets, lateral acceleration indicates direction-changing, not sustained turning
			// Reduce the decay-slowing effect so the yaw decays faster for dodgers
			if (tMoveStorage.m_flHistoricalDirVolatility > 0.5f)
			{
				float flVolReduce = std::clamp((tMoveStorage.m_flHistoricalDirVolatility - 0.5f) * 1.5f, 0.f, 0.8f);
				flLateralFactor *= (1.f - flVolReduce);
			}
			flLambda *= (1.f - flLateralFactor * 0.4f); // up to 40% reduction in decay rate
		}

		float flBoundedDecay = expf(-flLambda * float(tMoveStorage.m_iSimulatedTicks));
		tMoveStorage.m_MoveData.m_vecViewAngles.y += flEffYaw * flBoundedDecay + flCorrection;
	}
	else if (!tMoveStorage.m_bDirectMove)
		tMoveStorage.m_MoveData.m_flForwardMove = tMoveStorage.m_MoveData.m_flSideMove = 0.f;

	float flOldSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed;
	if (tMoveStorage.m_pPlayer->m_bDucked() && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->IsSwimming())
		tMoveStorage.m_MoveData.m_flClientMaxSpeed /= 3;

	if (tMoveStorage.m_bBunnyHop && tMoveStorage.m_pPlayer->IsOnGround() && !tMoveStorage.m_pPlayer->m_bDucked())
	{
		tMoveStorage.m_MoveData.m_nOldButtons = 0;
		tMoveStorage.m_MoveData.m_nButtons |= IN_JUMP;
	}

	// Save pre-move state for geometry-stuck detection
	tMoveStorage.m_vPreMoveOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
	tMoveStorage.m_vPreMoveVelocity = tMoveStorage.m_MoveData.m_vecVelocity;

	I::GameMovement->ProcessMovement(tMoveStorage.m_pPlayer, &tMoveStorage.m_MoveData);
	if (pCallback)
		(*pCallback)(tMoveStorage.m_MoveData);

	// === Geometry-stuck detection and linear extrapolation fallback ===
	// When the simulated player hits geometry (clipping from origin compression, slight
	// position offsets, etc.) ProcessMovement barely moves them. This causes severe
	// under-prediction for targets moving in straight lines through areas where the
	// simulation clips geometry but the real player doesn't.
	// Detection: if the player barely moved despite having velocity and inputs, switch
	// to linear extrapolation based on pre-move velocity.
	{
		float flMovedDist = (tMoveStorage.m_MoveData.m_vecAbsOrigin - tMoveStorage.m_vPreMoveOrigin).Length2D();
		float flPreSpeed = tMoveStorage.m_vPreMoveVelocity.Length2D();
		float flExpectedDist = flPreSpeed * TICK_INTERVAL;

		if (!tMoveStorage.m_bUsingExtrapolation)
		{
			// Check if stuck: moved less than 15% of expected distance with significant expected movement
			if (flMovedDist < flExpectedDist * 0.15f && flExpectedDist > 3.f)
			{
				tMoveStorage.m_iStuckTicks++;
				if (tMoveStorage.m_iStuckTicks >= 3) // 3 consecutive stuck ticks to confirm
					tMoveStorage.m_bUsingExtrapolation = true;
			}
			else
			{
				tMoveStorage.m_iStuckTicks = 0;
			}
		}

		if (tMoveStorage.m_bUsingExtrapolation && flPreSpeed > 1.f)
		{
			// Linear extrapolation: advance position along pre-move velocity direction
			// Apply temporal decay and speed envelope to the extrapolated speed
			float flExtrSpeed = flPreSpeed * flPredScale;
			flExtrSpeed = std::clamp(flExtrSpeed, 0.f, tMoveStorage.m_MoveData.m_flMaxSpeed);
			Vec3 vDir2D = tMoveStorage.m_vPreMoveVelocity.Normalized2D();

			tMoveStorage.m_MoveData.m_vecAbsOrigin.x = tMoveStorage.m_vPreMoveOrigin.x + vDir2D.x * flExtrSpeed * TICK_INTERVAL;
			tMoveStorage.m_MoveData.m_vecAbsOrigin.y = tMoveStorage.m_vPreMoveOrigin.y + vDir2D.y * flExtrSpeed * TICK_INTERVAL;
			// Keep Z from ProcessMovement (gravity/ground snap is correct)

			// Maintain velocity for next tick's extrapolation
			tMoveStorage.m_MoveData.m_vecVelocity.x = vDir2D.x * flExtrSpeed;
			tMoveStorage.m_MoveData.m_vecVelocity.y = vDir2D.y * flExtrSpeed;

			// Also re-check: if we moved through the stuck region and ProcessMovement would
			// work again, stop extrapolating. Check by seeing if velocity was restored.
			if (tMoveStorage.m_iSimulatedTicks > 5 && flMovedDist > flExpectedDist * 0.5f)
			{
				tMoveStorage.m_bUsingExtrapolation = false;
				tMoveStorage.m_iStuckTicks = 0;
			}
		}
	}

	// === Symmetric strafe lateral velocity dampening ===
	// For targets confirmed to be A-D strafing with near-zero average yaw,
	// dampen the lateral velocity component (perpendicular to mean movement direction)
	// after ProcessMovement. This prevents position accumulation in the strafe direction
	// during long-flight predictions where the target will reverse multiple times.
	// The dampening increases with expected reversals during projectile flight time.
	if (tMoveStorage.m_bSymmetricStrafe && !tMoveStorage.m_vMeanVelocityDir.IsZero()
		&& tMoveStorage.m_iStrafeHalfPeriod > 0)
	{
		Vec3 vAvgDir = tMoveStorage.m_vMeanVelocityDir;
		Vec3 vSimVel = tMoveStorage.m_MoveData.m_vecVelocity;

		// Decompose velocity into forward (along mean path) and lateral (perpendicular)
		float flForward = vSimVel.x * vAvgDir.x + vSimVel.y * vAvgDir.y;
		float flLateral = vSimVel.x * (-vAvgDir.y) + vSimVel.y * vAvgDir.x;

		// Dampen lateral component: more reversal cycles in flight → stronger dampening
		float flReversalsInFlight = float(tMoveStorage.m_iSimulatedTicks)
			/ float(tMoveStorage.m_iStrafeHalfPeriod);
		float flLateralDampen = 1.f / (1.f + flReversalsInFlight * 0.4f);
		flLateral *= flLateralDampen;

		// Reconstruct velocity with dampened lateral component
		tMoveStorage.m_MoveData.m_vecVelocity.x = flForward * vAvgDir.x + flLateral * (-vAvgDir.y);
		tMoveStorage.m_MoveData.m_vecVelocity.y = flForward * vAvgDir.y + flLateral * vAvgDir.x;
	}

	// Peek/jiggle blend: pull simulated position toward oscillation center
	// This prevents over-predicting targets that are jiggle-peeking
	if (tMoveStorage.m_bPeekDetected && tMoveStorage.m_flPeekBlend > 0.f)
	{
		// When retreat is likely (cover behind + decelerating), use much more aggressive blending
		// Normal peeks: gradual pull to center over 50 ticks, halved blend
		// Retreat peeks: faster pull over 25 ticks, full blend strength
		float flTimeDivisor = tMoveStorage.m_bRetreatLikely ? 25.f : 50.f;
		float flBlendScale = tMoveStorage.m_bRetreatLikely ? 0.85f : 0.5f;
		float flTimeBlend = std::clamp(float(tMoveStorage.m_iSimulatedTicks) / flTimeDivisor, 0.f, 1.f);
		float flBlend = tMoveStorage.m_flPeekBlend * flTimeBlend * flBlendScale;

		// Blend the XY position toward peek center, leave Z alone (vertical is usually stable)
		Vec3 vSimPos = tMoveStorage.m_MoveData.m_vecAbsOrigin;
		Vec3 vBlended = vSimPos;
		vBlended.x = vSimPos.x + (tMoveStorage.m_vPeekCenter.x - vSimPos.x) * flBlend;
		vBlended.y = vSimPos.y + (tMoveStorage.m_vPeekCenter.y - vSimPos.y) * flBlend;

		// Apply when drifting away from center; use tighter threshold for retreat scenarios
		float flDistFromCenter = (vSimPos - tMoveStorage.m_vPeekCenter).Length2D();
		float flActivationDist = tMoveStorage.m_bRetreatLikely
			? tMoveStorage.m_flPeekRadius * 0.3f
			: tMoveStorage.m_flPeekRadius * 0.5f;
		if (flDistFromCenter > flActivationDist)
		{
			tMoveStorage.m_MoveData.m_vecAbsOrigin = vBlended;

			// Dampen velocity more aggressively for retreat scenarios
			float flVelDampen = tMoveStorage.m_bRetreatLikely
				? 1.f - flBlend * 0.8f
				: 1.f - flBlend * 0.5f;
			tMoveStorage.m_MoveData.m_vecVelocity.x *= flVelDampen;
			tMoveStorage.m_MoveData.m_vecVelocity.y *= flVelDampen;
		}

		// Cover wall clamp: if sim position would be past the detected cover wall,
		// clamp it to stay on the playable side of the wall
		if (tMoveStorage.m_bNearCover && tMoveStorage.m_flCoverDist > 0.f)
		{
			// Check if sim position is behind cover relative to where the target started
			Vec3 vToCover = tMoveStorage.m_vCoverNormal * (-1.f); // direction toward cover
			Vec3 vFromStart = vSimPos - tMoveStorage.m_vPeekCenter;
			float flProjection = vFromStart.x * vToCover.x + vFromStart.y * vToCover.y;
			if (flProjection > tMoveStorage.m_flCoverDist * 0.8f)
			{
				// Sim has moved past cover — clamp back
				float flExcess = flProjection - tMoveStorage.m_flCoverDist * 0.8f;
				tMoveStorage.m_MoveData.m_vecAbsOrigin.x -= vToCover.x * flExcess;
				tMoveStorage.m_MoveData.m_vecAbsOrigin.y -= vToCover.y * flExcess;

				// Kill velocity component toward cover
				float flVelTowardCover = tMoveStorage.m_MoveData.m_vecVelocity.x * vToCover.x
					+ tMoveStorage.m_MoveData.m_vecVelocity.y * vToCover.y;
				if (flVelTowardCover > 0.f)
				{
					tMoveStorage.m_MoveData.m_vecVelocity.x -= vToCover.x * flVelTowardCover;
					tMoveStorage.m_MoveData.m_vecVelocity.y -= vToCover.y * flVelTowardCover;
				}
			}
		}
	}

	// Post-ProcessMovement safety cap: prevent simulation from producing unreasonable speeds
	// Only caps clearly excessive values — fine-grained speed control is handled by
	// SpeedEnvelope and input scaling above, not by capping after physics
	if (tMoveStorage.m_flInitialSimSpeed2D > 0.1f)
	{
		float flSimSpeed = tMoveStorage.m_MoveData.m_vecVelocity.Length2D();
		if (flSimSpeed > 1.f)
		{
			// Safety cap: never exceed 150% of initial speed or class max speed
			float flSafetyCap = std::max(tMoveStorage.m_flInitialSimSpeed2D * 1.5f,
				tMoveStorage.m_MoveData.m_flMaxSpeed * 0.4f);

			if (flSimSpeed > flSafetyCap && flSafetyCap > 0.1f)
			{
				float flScale = flSafetyCap / flSimSpeed;
				tMoveStorage.m_MoveData.m_vecVelocity.x *= flScale;
				tMoveStorage.m_MoveData.m_vecVelocity.y *= flScale;
			}
		}
	}

	tMoveStorage.m_MoveData.m_flClientMaxSpeed = flOldSpeed;

	tMoveStorage.m_flSimTime += TICK_INTERVAL;
	tMoveStorage.m_bPredictNetworked = tMoveStorage.m_flSimTime >= tMoveStorage.m_flPredictedSimTime;
	if (tMoveStorage.m_bPredictNetworked)
	{
		tMoveStorage.m_vPredictedOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
		tMoveStorage.m_flPredictedSimTime += tMoveStorage.m_flPredictedDelta;
	}
	bool bLastbDirectMove = tMoveStorage.m_bDirectMove;
	tMoveStorage.m_bDirectMove = tMoveStorage.m_pPlayer->IsOnGround() || tMoveStorage.m_pPlayer->IsSwimming();

	if (tMoveStorage.m_flAverageYaw)
		tMoveStorage.m_MoveData.m_vecViewAngles.y -= flCorrection;
	else if (tMoveStorage.m_bDirectMove && !bLastbDirectMove
		&& !tMoveStorage.m_MoveData.m_flForwardMove && !tMoveStorage.m_MoveData.m_flSideMove
		&& tMoveStorage.m_MoveData.m_vecVelocity.Length2D() > tMoveStorage.m_MoveData.m_flMaxSpeed * 0.015f)
	{
		Vec3 vDirection = tMoveStorage.m_MoveData.m_vecVelocity.Normalized2D() * 450.f;
		s_tDummyCmd.forwardmove = vDirection.x, s_tDummyCmd.sidemove = -vDirection.y;
		SDK::FixMovement(&s_tDummyCmd, {}, tMoveStorage.m_MoveData.m_vecViewAngles);
		tMoveStorage.m_MoveData.m_flForwardMove = s_tDummyCmd.forwardmove, tMoveStorage.m_MoveData.m_flSideMove = s_tDummyCmd.sidemove;
	}

	RestoreBounds(tMoveStorage.m_pPlayer);
}

void CMovementSimulation::RunTick(MoveStorage& tMoveStorage, bool bPath, std::function<void(CMoveData&)> fCallback)
{
	RunTick(tMoveStorage, bPath, &fCallback);
}

void CMovementSimulation::Restore(MoveStorage& tMoveStorage)
{
	if (tMoveStorage.m_bInitFailed || !tMoveStorage.m_pPlayer)
		return;

	I::MoveHelper->SetHost(nullptr);
	tMoveStorage.m_pPlayer->m_pCurrentCommand() = nullptr;

	Reset(tMoveStorage);

	I::Prediction->m_bInPrediction = m_bOldInPrediction;
	I::Prediction->m_bFirstTimePredicted = m_bOldFirstTimePredicted;
	I::GlobalVars->frametime = m_flOldFrametime;
}

float CMovementSimulation::GetPredictedDelta(CBaseEntity* pEntity)
{
	auto& vSimTimes = m_mSimTimes[pEntity->entindex()];
	if (!vSimTimes.empty())
	{
		switch (Vars::Aimbot::Projectile::DeltaMode.Value)
		{
		case 0: return std::reduce(vSimTimes.begin(), vSimTimes.end()) / vSimTimes.size();
		case 1: return *std::max_element(vSimTimes.begin(), vSimTimes.end());
		}
	}
	return TICK_INTERVAL;
}