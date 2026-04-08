#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

Enum(PointType, None = 0, Out = 1 << 0, In = 1 << 1, Out2 = 1 << 2, In2 = 1 << 3)
Enum(Calculated, Pending, Good, Time, Bad)

struct Info_t
{
	CTFPlayer* m_pLocal = nullptr;
	CTFWeaponBase* m_pWeapon = nullptr;
	Target_t* m_pTarget = nullptr;
	CBaseEntity* m_pProjectile = nullptr;

	Vec3 m_vLocalEye = {};
	Vec3 m_vTargetEye = {};

	float m_flLatency = 0.f;

	Vec3 m_vHull = {};
	Vec3 m_vOffset = {};
	Vec3 m_vAngFix = {};
	float m_flVelocity = 0.f;
	float m_flGravity = 0.f;
	float m_flRadius = 0.f;
	float m_flRadiusTime = 0.f;
	float m_flBoundingTime = 0.f;
	float m_flOffsetTime = 0.f;
	int m_iSplashCount = 0;
	int m_iSplashMode = 0;
	int m_iArmTime = 0;

	// Water detection
	bool m_bShooterInWater = false;
	bool m_bTargetInWater = false;
	int m_iShooterWaterLevel = 0;
	int m_iTargetWaterLevel = 0;
};

// Miss tracking for projectile shot logging — detailed for AI analysis
struct MissRecord_t
{
	int m_iTargetIndex = 0;
	Vec3 m_vShooterPos = {};            // Shooter eye position at shot
	Vec3 m_vTargetPosAtShot = {};       // Target origin when shot fired
	Vec3 m_vTargetVelAtShot = {};       // Target velocity when shot fired
	Vec3 m_vPredictedPos = {};          // Predicted target origin at impact
	Vec3 m_vShootAngles = {};           // Angles used for the shot
	Vec3 m_vActualPosAtImpact = {};     // Target pos captured near impact time
	float m_flFlightTime = 0.f;
	float m_flShotTime = 0.f;
	float m_flPredictedYaw = 0.f;
	float m_flDistance = 0.f;           // Distance to target at shot
	int m_iTargetHealthAtShot = 0;
	int m_iShooterClass = 0;            // Shooter's TF class (TF_CLASS_*)
	int m_iWeaponID = 0;               // Weapon ID (ETFWeaponType)
	int m_iTargetClass = 0;            // Target's TF class
	float m_flProjectileSpeed = 0.f;   // Projectile speed (units/s)
	float m_flProjectileGravity = 0.f; // Projectile gravity multiplier
	bool m_bDamageChecked = false;
	bool m_bPending = false;

	// Enhanced fields for detailed AI analysis
	bool m_bTargetOnGround = false;     // Was target on ground at shot time
	bool m_bTargetInAir = false;        // Was target airborne
	bool m_bTargetWasMoving = false;    // Was target moving (speed > 10)
	bool m_bSplashMode = false;         // Was splash prediction used (vs direct)
	float m_flSpeedRatio = 0.f;        // Initial speed ratio from MoveSim
	float m_flTemporalDecay = 0.f;     // Temporal decay at simulated tick count
	float m_flVelocityTrend = 0.f;     // Acceleration trend (+accel, -decel)
	float m_flVelocityConsistency = 0.f; // Speed stability 0.6-1.0
	bool m_bPeekDetected = false;       // Was peek/jiggle pattern detected
	float m_flAccelAlignment = 0.f;     // Tangential acceleration component
	float m_flAccelLateral = 0.f;       // Lateral (turning) acceleration
	int m_iSimTicks = 0;               // Number of simulation ticks used
	float m_flDragCoeff = 0.f;         // Drag coefficient applied
	float m_flLatency = 0.f;           // Network latency compensation
	int m_iTargetChoke = 0;            // Target choke ticks
	Vec3 m_vTargetAccel = {};           // Target acceleration vector at shot
	float m_flHeightDiff = 0.f;        // Vertical difference (shooter - target)
	std::string m_sMapName;             // Map name for context

	// New prediction system diagnostics
	float m_flHistoricalDirVolatility = 0.f; // Direction change frequency in recent records
	bool m_bGroundStrafeDetected = false;    // Periodic A-D strafe pattern found
	bool m_bUsingExtrapolation = false;      // Fell back to linear extrapolation (geometry stuck)
	int m_iStrafeHalfPeriod = 0;             // Strafe half-period in ticks (if detected)
	bool m_bSymmetricStrafe = false;         // Symmetric A-D spam (yaw ~= 0, sinusoidal prediction active)
	float m_flStrafeYawAmplitude = 0.f;      // Yaw oscillation amplitude (deg/tick)
	float m_flShotConfidence = 0.f;          // Confidence score at time of shot (0-100)

	// Cover-proximity detection diagnostics
	bool m_bNearCover = false;               // Wall detected behind target's velocity direction
	bool m_bRetreatLikely = false;           // Near cover + decelerating/reversing
	float m_flCoverDist = 0.f;              // Distance to cover wall behind target

	// Trajectory data for miss analysis (populated during shot tracking)
	std::vector<Vec3> m_vPredictedPath;                    // Predicted movement path (from sim)
	std::vector<std::pair<float, Vec3>> m_vActualTrajectory; // Actual positions with timestamps
	Vec3 m_vTargetVelAtImpact = {};                        // Target velocity near impact time
	float m_flLastTrackTime = 0.f;                         // Last time actual pos was recorded
};

struct Solution_t
{
	float m_flPitch = 0.f;
	float m_flYaw = 0.f;
	float m_flTime = 0.f;
	int m_iCalculated = CalculatedEnum::Pending;
};
struct Point_t
{
	Vec3 m_vPoint = {};
	Solution_t m_tSolution = {};
};

struct History_t
{
	Vec3 m_vOrigin;
	int m_iSimtime;
};
struct Direct_t : History_t
{
	float m_flPitch;
	float m_flYaw;
	float m_flTime;
	Vec3 m_vPoint;
	int m_iPriority;
	float m_flConfidence = 100.f;
};
struct Splash_t : History_t
{
	float m_flTimeTo;
};

class CAimbotProjectile
{
private:
	std::unordered_map<int, Vec3> GetDirectPoints();
	std::vector<Point_t> GetSplashPoints(Vec3 vOrigin, std::vector<std::pair<Vec3, int>>& vSpherePoints, int iSimTime);
	void SetupSplashPoints(Vec3& vPos, std::vector<std::pair<Vec3, int>>& vSpherePoints, std::vector<Vec3>& vSimplePoints);
	std::vector<Point_t> GetSplashPointsSimple(Vec3 vOrigin, std::vector<Vec3>& vSpherePoints, int iSimTime);

	void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, bool bAccuracy = true, int iTolerance = -1);
	bool TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, bool bSplash, bool bSecondTest = false);

	float CalculateConfidence(Target_t& tTarget, int iSimTime, float flTimeTo);

	bool HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, bool bSplash = false);
	bool HandleDirect(std::vector<Direct_t>& vDirectHistory);
	bool HandleSplash(std::vector<Splash_t>& vSplashHistory);

	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bVisuals = true);
	bool RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	bool CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile);
	bool TestAngle(CBaseEntity* pProjectile, const Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash);

	bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod = Vars::Aimbot::General::AimType.Value);

	Info_t m_tInfo = {};
	MoveStorage m_tMoveStorage = {};
	ProjectileInfo m_tProjInfo = {};

	bool m_bLastTickHeld = false;

	float m_flTimeTo = std::numeric_limits<float>::max();
	std::vector<Vec3> m_vPlayerPath = {};
	std::vector<Vec3> m_vProjectilePath = {};
	std::vector<DrawBox_t> m_vBoxes = {};

	Vec3 m_vAngleTo = {};
	Vec3 m_vPredicted = {};
	Vec3 m_vTarget = {};

	int m_iResult = false;
	bool m_bVisuals = true;
	float m_flCurrentConfidence = 0.f;

	// Miss tracking
	std::deque<MissRecord_t> m_vPendingShots;

public:
	void CheckMiss();
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer);

	bool AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile);
	float GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon = nullptr, CTFPlayer* pPlayer = nullptr, float flScale = 1.f, CTFWeaponBase* pAirblast = nullptr);

	int m_iLastTickCancel = 0;
};

ADD_FEATURE(CAimbotProjectile, AimbotProjectile);