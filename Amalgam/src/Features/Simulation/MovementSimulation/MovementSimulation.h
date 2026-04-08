#pragma once
#include "../../../SDK/SDK.h"
#include <functional>

Enum(Move, Ground, Air, Swim)

struct MoveStorage
{
	CTFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;

	float m_flAverageYaw = 0.f;
	bool m_bBunnyHop = false;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;

	// Air strafe reversal prediction
	bool m_bAirReversalDetected = false;
	float m_flRecentAirYaw = 0.f;      // yaw rate of most recent air segment
	int m_iAirChangeInterval = 0;       // avg ticks between direction changes
	int m_iTicksSinceChange = 0;        // ticks elapsed since last observed change
	int m_iSimulatedTicks = 0;          // tick counter during RunTick simulation
	float m_flInitialSpeedRatio = 1.f;  // initial velocity ratio for prediction scaling

	// Direction change history for prediction confidence
	float m_flRecentDirChanges = 0.f;   // weighted count of recent direction changes
	float m_flAccelVariance = 0.f;      // variance of acceleration magnitudes
	Vec3 m_vLastSimVelocity = {};       // velocity from previous RunTick for change detection

	// Velocity trend analysis for adaptive prediction
	float m_flVelocityTrend = 0.f;      // positive=accelerating, negative=decelerating (units/s^2)
	float m_flVelocityConsistency = 1.f; // 0.6=highly erratic, 1.0=stable speed

	// Velocity cap system for simulation accuracy
	float m_flPeakRecentSpeed = 0.f;    // highest speed in recent records
	float m_flSpeedVolatility = 0.f;    // (peak-current)/maxSpeed, 0=stable speed profile
	float m_flInitialSimSpeed2D = 0.f;  // speed at simulation start for velocity capping
	float m_flBaseForwardMove = 0.f;    // original forward move after speedRatio scaling
	float m_flBaseSideMove = 0.f;       // original side move after speedRatio scaling

	// Physics-based prediction: acceleration vector model
	// Replaces scalar velocity trend with full 2D acceleration analysis
	Vec3 m_vSmoothedAccel2D = {};       // weighted-average 2D acceleration vector from recent records (u/s^2)
	float m_flAccelAlignment = 0.f;     // accel dot velocity_dir: >0 speeding up, <0 braking (u/s^2)
	float m_flAccelLateral = 0.f;       // accel perpendicular to velocity: lateral force for turning (u/s^2)

	// Peek/jiggle detection and average position prediction
	bool m_bPeekDetected = false;       // true if oscillating peek pattern found
	Vec3 m_vPeekCenter = {};            // center point of the peek oscillation
	float m_flPeekRadius = 0.f;        // average oscillation radius (half amplitude)
	float m_flPeekBlend = 0.f;         // 0.0=pure sim, 1.0=pure center (based on confidence)

	// Cover-proximity detection: detects when target is near a wall behind them
	// Indicates the target likely peeked out from cover and may retreat
	bool m_bNearCover = false;           // wall detected behind target's current velocity direction
	float m_flCoverDist = 0.f;           // distance to cover wall (0 = no cover, smaller = closer)
	Vec3 m_vCoverNormal = {};            // wall surface normal (points away from wall)
	bool m_bRetreatLikely = false;       // combined signal: near cover + decelerating/reversing

	// Ground strafe pattern detection: detects periodic A-D strafing on ground
	bool m_bGroundStrafeDetected = false;  // periodic ground strafe pattern found
	int m_iStrafeHalfPeriod = 0;           // avg ticks per half-cycle (direction segment)
	int m_iStrafePhaseTicks = 0;           // ticks since last detected reversal in records
	int m_iStrafeReversalCount = 0;        // number of observed velocity direction reversals

	// Symmetric strafe (A-D spam) detection and correction
	// When A-D strafing produces near-zero average yaw, the movement sim predicts
	// a straight line while the target oscillates. These fields enable sinusoidal
	// prediction and lateral velocity dampening to match the oscillation pattern.
	bool m_bSymmetricStrafe = false;       // strafe detected with near-zero average yaw
	Vec3 m_vMeanVelocityDir = {};          // unit direction of mean velocity across strafe cycles
	float m_flStrafeYawAmplitude = 0.f;    // average absolute yaw rate per tick in strafe segments

	// Geometry-stuck detection: detects when ProcessMovement fails to move the player
	// due to origin compression clipping into geometry
	Vec3 m_vPreMoveOrigin = {};            // origin before ProcessMovement (for stuck check)
	Vec3 m_vPreMoveVelocity = {};          // velocity before ProcessMovement
	int m_iStuckTicks = 0;                 // consecutive ticks where sim barely moved
	bool m_bUsingExtrapolation = false;    // currently using linear extrapolation fallback

	// Direction volatility from historical records (set in Initialize, used in RunTick)
	// 0.0 = perfectly straight/predictable, higher = more direction changes in recent history
	float m_flHistoricalDirVolatility = 0.f;
};

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	Vec3 m_vAcceleration = {};
};

class CMovementSimulation
{
private:
	void Store(MoveStorage& tMoveStorage);
	void Reset(MoveStorage& tMoveStorage);

	bool SetupMoveData(MoveStorage& tMoveStorage);
	void GetAverageYaw(MoveStorage& tMoveStorage, int iSamples);
	bool StrafePrediction(MoveStorage& tMoveStorage, int iSamples);

	void SetBounds(CTFPlayer* pPlayer);
	void RestoreBounds(CTFPlayer* pPlayer);

	bool m_bOldInPrediction = false;
	bool m_bOldFirstTimePredicted = false;
	float m_flOldFrametime = 0.f;

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};

public:
	void Store();

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true);
	bool SetDuck(MoveStorage& tMoveStorage, bool bDuck);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, std::function<void(CMoveData&)>* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, std::function<void(CMoveData&)> fCallback);
	void Restore(MoveStorage& tMoveStorage);

	float GetPredictedDelta(CBaseEntity* pEntity);
};

ADD_FEATURE(CMovementSimulation, MoveSim);