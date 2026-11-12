#include <Novice.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "FontRenderer.h"

const char kWindowTitle[] = "LC1B_05_ウエノユウタ";

// ==================================================
// 定数
// ==================================================
const int kScreenW = 1280;
const int kScreenH = 720;

const float kGravity = 0.8f;
const float kMoveSpeed = 6.0f;
const float kJumpPower = -17.0f;
const float kMaxFall = 20.0f;
const float kPi = 3.14159265f;

const int kMaxEBullets = 256; // ステージ3の弾幕用に拡張
const int kMaxLasers = 32;    // ステージ3のレーザー用
const int kPlatformNum = 4;

const int kPlayerMaxHp = 5;
const int kIntroFrames = 120;
const int kTotalStages = 3; // ステージ数

const int kBreakDuration = 360; // ブレイク時の行動停止フレーム数 (3秒)

// ==================================================
// 構造体
// ==================================================
struct Player {
	float x, y;
	float vx, vy;
	float w, h;
	bool onGround;
	int dir; // 1:右 -1:左
	int hp;
	int invincible;
	int jumpCount;   // 二段ジャンプ用(使用した回数)
	int dashTimer;   // ダッシュ残りフレーム
	int dashDir;
	int dashCooldown;
	int airDashLeft; // 空中ダッシュ残り回数(着地で回復)
	int parryTimer;  // パリィ判定が出ている残りフレーム
	int parryCooldown;
	int parryFlash; // パリィ成功エフェクト
	// ---- 近接攻撃 ----
	int atkTimer;     // 攻撃の残りフレーム
	int atkStep;      // コンボの段数(地上:1〜8, 空中:1〜4)
	bool atkAir;      // 空中攻撃か
	bool atkHit;      // この一振りがボスに当たったか
	int atkHi, atkLo; // 判定が出るフレーム範囲(atkTimerがこの範囲内)
	int atkDmg;
	int comboWindow;  // 次の段を入力できる猶予(この間は空中に留まる)
	int airComboLeft; // 空中コンボ開始権(着地で回復)
};

struct EBullet {
	float x, y, vx, vy, r;
	bool active;
	bool wave;
	bool reflected; // パリィで跳ね返された弾
	bool meteor;    // 流星群
	// ---- ステージ3用拡張 ----
	bool unparryable;       // true: 弾2（パリィ不可）, false: 弾1（パリィ可能）
	bool useGravity;        // 重力落下するか
	bool spawnLaserOnLand;  // 着弾地点から縦レーザーを放つか
	bool waitTimeStop;      // 時間停止解除待ちか
	float aimX, aimY;       // 時間停止解除後に向かうターゲット座標
	float launchSpeed;      // 時間停止解除後の弾速
};

struct Laser {
	bool active;
	float x1, y1, x2, y2; // 始点・終点（予告線・レーザー共通）
	float width;          // レーザーの幅
	int warnTimer;        // 予告時間（>0 の間は予告線のみ）
	int activeTimer;      // 照射時間（warnTimer==0 になってから減少）
	bool spawnSpread;     // 照射開始時に着弾点から16方向弾を放つか
	bool spawnedSpread;   // 16方向弾を放ち済みか
	bool movingVertical;  // 画面端から移動する縦レーザーか
	float vx;             // 移動速度
	bool airPhaseThrough; // プレイヤーが地面にいない時のみ半透明＆判定消滅
};

struct Platform {
	float x, y, w; // 上面のみ判定
};

struct Boss {
	float x, y, w, h;
	float vx, vy;
	int hp;
	int maxHp;
	int breakGauge;    // 現在のブレイクゲージ
	int maxBreakGauge; // 最大ブレイクゲージ
	bool isBroken;     // ブレイク状態か
	int breakTimer;    // ブレイク残り時間
	int state;
	int timer;
	int dir;
	int flash;
	int type; // 0:スナイパー, 1:クラッシャー, 2:ウィザード

	// ---- ステージ3専用AI変数 ----
	bool hidden;            // 画面から消えているか
	int phase;              // 1: HP60%以上, 2: HP60%未満
	int patternPool[16];    // 未実行の行動リスト（重み付き）
	int patternCount;       // リスト内の残り行動数
	int currentPattern;     // 実行中の行動ID
	int subState;           // 行動内のサブステップ
	int subTimer;           // 行動内のサブタイマー
	float targetX, targetY; // 移動目標座標
	float startX, startY;   // 移動開始座標
	float angleOffset;      // 連続弾の角度オフセット
	int repeatCount;        // 行動内の繰り返し回数
	int timeStopTimer;      // 時間停止残りフレーム
	int timeStopPrepTimer;  // 時間停止前の1秒の溜め用タイマー
};

// ダメージ数字の表示
const int kMaxDamagePopups = 16;
const int kDamagePopupFrames = 45;

struct DamagePopup {
	float x, y;
	int damage;
	int timer;
	bool isCritical;
};

enum Scene { kTitle, kStageSelect, kPlay, kGameOver, kVictory };

// ==================================================
// 描画ヘルパー(画面シェイク対応)
// ==================================================
static int gShakeX = 0;
static int gShakeY = 0;

static void Rect(float x, float y, float w, float h, unsigned int color) {
	Novice::DrawBox(static_cast<int>(x) + gShakeX, static_cast<int>(y) + gShakeY, static_cast<int>(w), static_cast<int>(h), 0.0f, color, kFillModeSolid);
}

static void Circle(float x, float y, float r, unsigned int color) {
	Novice::DrawEllipse(static_cast<int>(x) + gShakeX, static_cast<int>(y) + gShakeY, static_cast<int>(r), static_cast<int>(r), 0.0f, color, kFillModeSolid);
}

static void PrintfCenter(int centerX, int y, const char* format, ...) {
	char buf[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	GetFont().Draw(centerX - GetFont().Width(buf) / 2, y, buf);
}

bool isScreenReverse = false;
bool isReverseAttackUsed = false;

// ==================================================
// 共通関数
// ==================================================
static bool HitBox(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static bool HitLineBox(float x1, float y1, float x2, float y2, float lineRadius, float bx, float by, float bw, float bh) {
	float minX = bx - lineRadius;
	float maxX = bx + bw + lineRadius;
	float minY = by - lineRadius;
	float maxY = by + bh + lineRadius;

	float dx = x2 - x1;
	float dy = y2 - y1;
	float t0 = 0.0f;
	float t1 = 1.0f;

	float p[4] = { -dx, dx, -dy, dy };
	float q[4] = { x1 - minX, maxX - x1, y1 - minY, maxY - y1 };

	for (int i = 0; i < 4; i++) {
		if (fabsf(p[i]) < 1e-6f) {
			if (q[i] < 0.0f) return false;
		}
		else {
			float r = q[i] / p[i];
			if (p[i] < 0.0f) {
				if (r > t1) return false;
				if (r > t0) t0 = r;
			}
			else {
				if (r < t0) return false;
				if (r < t1) t1 = r;
			}
		}
	}
	return t0 <= t1;
}

static bool LandOnPlatform(const Platform* plats, int platCount, float x, float w, float prevBottom, float newBottom, float* landY) {
	for (int i = 0; i < platCount; i++) {
		const Platform& p = plats[i];
		if (x + w > p.x && x < p.x + p.w && prevBottom <= p.y && newBottom >= p.y) {
			*landY = p.y;
			return true;
		}
	}
	return false;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void SpawnDamagePopup(DamagePopup* popups, const Boss& boss, int damage, bool isCritical = false) {
	for (int i = 0; i < kMaxDamagePopups; i++) {
		if (popups[i].timer <= 0) {
			popups[i].x = boss.x + boss.w * 0.5f + static_cast<float>(rand() % 41 - 20);
			popups[i].y = boss.y - 20.0f;
			popups[i].damage = damage;
			popups[i].timer = kDamagePopupFrames;
			popups[i].isCritical = isCritical;
			return;
		}
	}
}

static void DamageBossBreak(Boss& boss, int amount, int& shake) {
	if (boss.isBroken || boss.hp <= 0) return;

	boss.breakGauge -= amount;
	if (boss.breakGauge <= 0) {
		boss.breakGauge = 0;
		boss.isBroken = true;
		boss.breakTimer = kBreakDuration;
		boss.vx = 0.0f;
		boss.vy = 0.0f;
		boss.state = 0;
		boss.hidden = false;
		boss.timeStopTimer = 0;
		boss.timeStopPrepTimer = 0;
		boss.flash = 15;
		shake = 20;
	}
}

static void FireEBullet(
	EBullet* arr,
	float x, float y,
	float vx, float vy,
	float r,
	bool wave,
	bool meteor = false,
	bool unparryable = false,
	bool useGravity = false,
	bool spawnLaserOnLand = false
) {
	for (int i = 0; i < kMaxEBullets; i++) {
		if (!arr[i].active) {
			arr[i].active = true;
			arr[i].x = x;
			arr[i].y = y;
			arr[i].vx = vx;
			arr[i].vy = vy;
			arr[i].r = r;
			arr[i].wave = wave;
			arr[i].reflected = false;
			arr[i].meteor = meteor;
			arr[i].unparryable = unparryable;
			arr[i].useGravity = useGravity;
			arr[i].spawnLaserOnLand = spawnLaserOnLand;
			arr[i].waitTimeStop = false;
			arr[i].aimX = 0.0f;
			arr[i].aimY = 0.0f;
			arr[i].launchSpeed = 0.0f;
			return;
		}
	}
}

static void FireAngle(EBullet* arr, float x, float y, float angle, float speed, float r, bool unparryable = false) {
	FireEBullet(arr, x, y, cosf(angle) * speed, sinf(angle) * speed, r, false, false, unparryable);
}

static void CalcRayEnd(float sx, float sy, float angle, float groundY, float* outX, float* outY) {
	float dx = cosf(angle);
	float dy = sinf(angle);
	float minT = 10000.0f;

	if (dx > 1e-5f) {
		float t = (kScreenW - sx) / dx;
		if (t > 0.0f && t < minT) minT = t;
	}
	else if (dx < -1e-5f) {
		float t = (0.0f - sx) / dx;
		if (t > 0.0f && t < minT) minT = t;
	}

	if (dy > 1e-5f) {
		float t = (groundY - sy) / dy;
		if (t > 0.0f && t < minT) minT = t;
	}
	else if (dy < -1e-5f) {
		float t = (0.0f - sy) / dy;
		if (t > 0.0f && t < minT) minT = t;
	}

	*outX = sx + dx * minT;
	*outY = sy + dy * minT;
}

static void SpawnLaser(
	Laser* lasers,
	float x1, float y1, float x2, float y2,
	float width,
	int warnTimer, int activeTimer,
	bool spawnSpread = false,
	bool movingVertical = false,
	float vx = 0.0f,
	bool airPhaseThrough = false
) {
	for (int i = 0; i < kMaxLasers; i++) {
		if (!lasers[i].active) {
			lasers[i].active = true;
			lasers[i].x1 = x1;
			lasers[i].y1 = y1;
			lasers[i].x2 = x2;
			lasers[i].y2 = y2;
			lasers[i].width = width;
			lasers[i].warnTimer = warnTimer;
			lasers[i].activeTimer = activeTimer;
			lasers[i].spawnSpread = spawnSpread;
			lasers[i].spawnedSpread = false;
			lasers[i].movingVertical = movingVertical;
			lasers[i].vx = vx;
			lasers[i].airPhaseThrough = airPhaseThrough;
			return;
		}
	}
}

// ステージ3用：行動プールを補充（フェーズ1は7種、重み付き、フェーズ2は7種+時間停止4種）
static void RefillStage3Patterns(Boss& boss) {
	int idx = 0;
	// 行動0と1は「2」の重みなので、プールに2つずつ入れる
	boss.patternPool[idx++] = 0;
	boss.patternPool[idx++] = 0;
	boss.patternPool[idx++] = 1;
	boss.patternPool[idx++] = 1;
	boss.patternPool[idx++] = 2;
	boss.patternPool[idx++] = 3;
	boss.patternPool[idx++] = 4;
	boss.patternPool[idx++] = 5;
	boss.patternPool[idx++] = 6;

	if (boss.phase == 2) {
		boss.patternPool[idx++] = 7;
		boss.patternPool[idx++] = 8;
		boss.patternPool[idx++] = 9;
		boss.patternPool[idx++] = 10;
	}
	boss.patternCount = idx;
}

static int PopStage3Pattern(Boss& boss) {
	if (boss.patternCount <= 0) {
		RefillStage3Patterns(boss);
	}
	int idx = rand() % boss.patternCount;
	int chosen = boss.patternPool[idx];
	boss.patternPool[idx] = boss.patternPool[boss.patternCount - 1];
	boss.patternCount--;
	return chosen;
}

static void ResetGame(Player& pl, EBullet* ebullets, Laser* lasers, Boss& boss, float groundY, int stage) {
	pl.x = 200.0f;
	pl.y = groundY - 48.0f;
	pl.vx = pl.vy = 0.0f;
	pl.w = 32.0f;
	pl.h = 48.0f;
	pl.onGround = true;
	pl.dir = 1;
	pl.hp = kPlayerMaxHp;
	pl.invincible = 0;
	pl.jumpCount = 0;
	pl.dashTimer = 0;
	pl.dashDir = 1;
	pl.dashCooldown = 0;
	pl.airDashLeft = 1;
	pl.parryTimer = 0;
	pl.parryCooldown = 0;
	pl.parryFlash = 0;
	pl.atkTimer = 0;
	pl.atkStep = 0;
	pl.atkAir = false;
	pl.atkHit = false;
	pl.atkHi = pl.atkLo = 0;
	pl.atkDmg = 0;
	pl.comboWindow = 0;
	pl.airComboLeft = 1;

	for (int i = 0; i < kMaxEBullets; i++) ebullets[i].active = false;
	for (int i = 0; i < kMaxLasers; i++) lasers[i].active = false;

	// ステージ3のボスのみサイズを 48 * 76 に変更
	if (stage == 2) {
		boss.w = 48.0f;
		boss.h = 76.0f;
	}
	else {
		boss.w = 140.0f;
		boss.h = 140.0f;
	}
	boss.x = 1000.0f;
	boss.y = groundY - boss.h;
	boss.vx = boss.vy = 0.0f;
	boss.type = stage;

	const int bossHpTable[kTotalStages] = { 5000, 6500, 8000 };
	boss.maxHp = bossHpTable[stage];
	boss.hp = boss.maxHp;

	const int bossBreakTable[kTotalStages] = { 1500, 1500, 1500 };
	boss.maxBreakGauge = bossBreakTable[stage];
	boss.breakGauge = boss.maxBreakGauge;
	boss.isBroken = false;
	boss.breakTimer = 0;

	boss.state = 0;
	boss.timer = 60;
	boss.dir = -1;
	boss.flash = 0;

	boss.hidden = false;
	boss.phase = 1;
	boss.patternCount = 0;
	boss.currentPattern = -1;
	boss.subState = 0;
	boss.subTimer = 0;
	boss.targetX = boss.x;
	boss.targetY = boss.y;
	boss.startX = boss.x;
	boss.startY = boss.y;
	boss.angleOffset = 0.0f;
	boss.repeatCount = 0;
	boss.timeStopTimer = 0;
	boss.timeStopPrepTimer = 0;
	if (stage == 2) {
		RefillStage3Patterns(boss);
	}
}

static void CancelAttack(Player& pl) {
	pl.atkTimer = 0;
	pl.atkStep = 0;
	pl.atkHit = false;
	pl.comboWindow = 0;
}

static void StartAttack(Player& pl, bool air, int step, int dir) {
	pl.dir = dir;
	pl.atkAir = air;
	pl.atkStep = step;
	pl.atkHit = false;
	pl.comboWindow = 0;
	if (!air) {
		if (step < 8) {
			if (step == 0 || step == 1 || step == 2) {
				pl.atkTimer = 16;
			}
			else {
				pl.atkTimer = 10;
			}
			pl.atkHi = 11;
			pl.atkLo = 5;
			if (step == 0 || step == 1 || step == 2) {
				pl.atkDmg = 200;
			}
			else {
				pl.atkDmg = 100;
			}
		}
		else {
			pl.atkTimer = 26;
			pl.atkHi = 20;
			pl.atkLo = 6;
			pl.atkDmg = 600;
		}
	}
	else if (step < 4) {
		pl.atkTimer = 14;
		pl.atkHi = 10;
		pl.atkLo = 3;
		pl.atkDmg = 200;
		pl.vy = 0.0f;
	}
	else {
		pl.atkTimer = 24;
		pl.atkHi = 18;
		pl.atkLo = 5;
		pl.atkDmg = 400;
		pl.vy = 0.0f;
	}
}

static void MeleeBox(const Player& pl, float* x, float* y, float* w, float* h) {
	float ov = 0.0f;
	if (!pl.atkAir) {
		if (pl.atkStep < 8) {
			*w = 90.0f;
			*h = pl.h + 10.0f;
			*y = pl.y - 5.0f;
		}
		else {
			*w = 120.0f;
			*h = pl.h + 20.0f;
			*y = pl.y - 10.0f;
			ov = 15.0f;
		}
	}
	else if (pl.atkStep < 4) {
		*w = 80.0f;
		*h = 70.0f;
		*y = pl.y + pl.h * 0.5f - 35.0f;
	}
	else {
		*w = 120.0f;
		*h = 110.0f;
		*y = pl.y + pl.h * 0.5f - 55.0f;
		ov = 10.0f;
	}
	*x = (pl.dir == 1) ? pl.x + pl.w - ov : pl.x - *w + ov;
}

static void HurtPlayer(Player& pl) {
	CancelAttack(pl);
	pl.hp--;
	pl.invincible = 90;
	pl.vy = -10.0f;
	pl.onGround = false;
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Novice::Initialize(kWindowTitle, kScreenW, kScreenH);
	GetFont().Create();

	char keys[256] = { 0 };
	char preKeys[256] = { 0 };

	//int meteorTexture = Novice::LoadTexture("./Resources/images/meteor.png");
	int meteorAnimationFrame = 0;
	int meteorAnimationTimer = 0;

	const int kMeteorAnimationSpeed = 5;
	//const int kMeteorFrameWidth = 64;
	//const int kMeteorFrameHeight = 64;

	srand(static_cast<unsigned int>(time(nullptr)));

	Platform platforms[kPlatformNum] = {
		{0.0f, 640.0f, 1280.0f}, // 地面
		{120.0f, 500.0f, 260.0f},
		{900.0f, 500.0f, 260.0f},
		{510.0f, 370.0f, 260.0f},
	};
	const float groundY = platforms[0].y;

	Player player;
	EBullet ebullets[kMaxEBullets];
	Laser lasers[kMaxLasers];
	Boss boss;
	DamagePopup popups[kMaxDamagePopups] = {};
	int selectedStage = 0;
	int currentStage = 0;
	int bestFrames[kTotalStages] = { 0, 0, 0 };

	ResetGame(player, ebullets, lasers, boss, groundY, selectedStage);

	Scene scene = kTitle;
	int introTimer = 0;
	int shake = 0;
	int playFrames = 0;
	int frame = 0;

	while (Novice::ProcessMessage() == 0) {
		Novice::BeginFrame();

		memcpy(preKeys, keys, 256);
		Novice::GetHitKeyStateAll(keys);

		frame++;

		// ステージ3は3つの足場を削除し、地面(1つ)のみにする
		int activePlatformNum = (currentStage == 2) ? 1 : kPlatformNum;

		bool pressStart = (preKeys[DIK_SPACE] == 0 && keys[DIK_SPACE] != 0) || (preKeys[DIK_RETURN] == 0 && keys[DIK_RETURN] != 0);

		if (scene == kTitle) {
			if (pressStart) {
				scene = kStageSelect;
			}
		}
		else if (scene == kStageSelect) {
			if ((preKeys[DIK_A] == 0 && keys[DIK_A] != 0) || (preKeys[DIK_LEFT] == 0 && keys[DIK_LEFT] != 0)) {
				selectedStage = (selectedStage - 1 + kTotalStages) % kTotalStages;
			}
			if ((preKeys[DIK_D] == 0 && keys[DIK_D] != 0) || (preKeys[DIK_RIGHT] == 0 && keys[DIK_RIGHT] != 0)) {
				selectedStage = (selectedStage + 1) % kTotalStages;
			}
			if (pressStart) {
				currentStage = selectedStage;
				ResetGame(player, ebullets, lasers, boss, groundY, currentStage);
				for (int i = 0; i < kMaxDamagePopups; i++) popups[i].timer = 0;
				introTimer = kIntroFrames;
				playFrames = 0;
				shake = 0;
				gShakeX = 0;
				gShakeY = 0;
				scene = kPlay;
			}
			if (preKeys[DIK_BACKSPACE] == 0 && keys[DIK_BACKSPACE] != 0) {
				scene = kTitle;
			}
		}
		else if (scene == kGameOver || scene == kVictory) {
			shake = 0;
			gShakeX = 0;
			gShakeY = 0;
			if (pressStart) {
				scene = kStageSelect;
			}
		}
		else if (scene == kPlay) {
			bool isTimeStopped = (boss.type == 2 && boss.timeStopTimer > 0);

			if (!isTimeStopped) {
				// ----- プレイヤー操作 -----
				player.vx = 0.0f;
				if (keys[DIK_A]) {
					player.vx = -kMoveSpeed;
					player.dir = -1;
				}
				if (keys[DIK_D]) {
					player.vx = kMoveSpeed;
					player.dir = 1;
				}

				bool jumpTrigger = (preKeys[DIK_W] == 0 && keys[DIK_W] != 0) || (preKeys[DIK_SPACE] == 0 && keys[DIK_SPACE] != 0);
				bool jumpHeld = keys[DIK_W] != 0 || keys[DIK_SPACE] != 0;

				if (player.dashCooldown > 0) player.dashCooldown--;
				if (player.parryCooldown > 0) player.parryCooldown--;
				if (player.parryTimer > 0) player.parryTimer--;
				if (player.parryFlash > 0) player.parryFlash--;

				if (jumpTrigger && player.dashTimer == 0) {
					if (player.onGround) {
						CancelAttack(player);
						player.vy = kJumpPower;
						player.onGround = false;
						player.jumpCount = 1;
					}
					else if (player.jumpCount < 2) {
						CancelAttack(player);
						player.vy = kJumpPower * 0.9f;
						player.jumpCount = 2;
					}
				}

				if (!player.onGround && player.vy < 0.0f && !jumpHeld) {
					player.vy += 1.2f;
				}

				bool dashTrigger = (preKeys[DIK_LSHIFT] == 0 && keys[DIK_LSHIFT] != 0);
				if (dashTrigger && player.dashTimer == 0 && player.dashCooldown == 0 && player.parryTimer == 0 && (player.onGround || player.airDashLeft > 0)) {
					CancelAttack(player);
					player.dashTimer = 10;
					player.dashCooldown = 35;
					int d = player.dir;
					if (keys[DIK_A]) d = -1;
					if (keys[DIK_D]) d = 1;
					player.dashDir = d;
					player.dir = d;
					if (!player.onGround) player.airDashLeft--;
				}

				if (Novice::IsTriggerMouse(1) && player.parryCooldown == 0 && player.dashTimer == 0) {
					CancelAttack(player);
					player.parryTimer = 12;
					player.parryCooldown = 45;
				}

				if (Novice::IsTriggerMouse(0) && player.dashTimer == 0 && player.parryTimer == 0) {
					int adir = player.dir;
					if (keys[DIK_A]) {
						adir = -1;
					}
					else if (keys[DIK_D]) {
						adir = 1;
					}
					else {
						adir = (boss.x + boss.w * 0.5f > player.x + player.w * 0.5f) ? 1 : -1;
					}

					bool canNext = player.atkHit && player.atkStep > 0 &&
						((!player.atkAir && player.atkStep < 8) || (player.atkAir && player.atkStep < 4)) &&
						(player.atkTimer <= 6 || player.comboWindow > 0);

					if (canNext) {
						StartAttack(player, player.atkAir, player.atkStep + 1, adir);
					}
					else if (player.atkTimer == 0 && player.comboWindow == 0) {
						if (player.onGround) {
							StartAttack(player, false, 1, adir);
						}
						else if (player.airComboLeft > 0) {
							player.airComboLeft--;
							StartAttack(player, true, 1, adir);
						}
					}
				}

				if (player.atkTimer > 0) {
					player.atkTimer--;
					if (player.atkTimer == 0) {
						if (player.atkAir && player.atkHit && player.atkStep < 4) {
							player.comboWindow = 22;
						}
						else if (!player.atkAir && player.atkHit && player.atkStep < 8) {
							player.comboWindow = 22;
						}
						else {
							player.atkStep = 0;
						}
					}
				}
				else if (player.comboWindow > 0) {
					player.comboWindow--;
					if (player.comboWindow == 0) player.atkStep = 0;
				}

				bool hover = false;
				if (player.atkTimer > 0) {
					if (!player.atkAir) {
						player.vx = 0.0f;
					}
					else {
						player.vx *= 0.4f;
						hover = true;
					}
				}
				else if (player.comboWindow > 0) {
					player.vx *= 0.4f;
					hover = true;
				}

				bool dashing = player.dashTimer > 0;
				if (dashing) {
					player.vx = player.dashDir * 18.0f;
					player.vy = 0.0f;
					player.dashTimer--;
				}
				else if (hover) {
					player.vy = 0.0f;
				}
				player.x += player.vx;
				player.x = Clamp(player.x, 0.0f, kScreenW - player.w);

				float prevBottom = player.y + player.h;
				if (!dashing && !hover) player.vy += kGravity;
				if (player.vy > kMaxFall) player.vy = kMaxFall;
				player.y += player.vy;
				player.onGround = false;

				float landY;
				if (player.vy >= 0.0f && LandOnPlatform(platforms, activePlatformNum, player.x, player.w, prevBottom, player.y + player.h, &landY)) {
					player.y = landY - player.h;
					player.vy = 0.0f;
					player.onGround = true;
					player.jumpCount = 0;
					player.airDashLeft = 1;
					player.airComboLeft = 1;
					if (player.atkAir) CancelAttack(player);
				}
				if (!player.onGround && player.jumpCount == 0) player.jumpCount = 1;

				if (player.invincible > 0) player.invincible--;
			}

			if (boss.flash > 0) boss.flash--;
			if (shake > 0) shake--;
			if (!isTimeStopped) playFrames++;
			for (int i = 0; i < kMaxDamagePopups; i++) {
				if (popups[i].timer > 0) popups[i].timer--;
			}

			// 近接攻撃のヒット判定
			if (!isTimeStopped && player.atkTimer > 0 && player.atkTimer <= player.atkHi && player.atkTimer >= player.atkLo) {
				float ax, ay, aw, ah;
				MeleeBox(player, &ax, &ay, &aw, &ah);

				if (introTimer == 0 && !boss.hidden && !player.atkHit && HitBox(ax, ay, aw, ah, boss.x, boss.y, boss.w, boss.h)) {
					bool finisher = (!player.atkAir && player.atkStep == 8) || (player.atkAir && player.atkStep == 4);
					player.atkHit = true;

					int dmg = boss.isBroken ? static_cast<int>(player.atkDmg * 1.5f) : player.atkDmg;
					boss.hp -= dmg;
					SpawnDamagePopup(popups, boss, dmg, boss.isBroken);

					boss.flash = 6;
					shake = finisher ? 16 : 6;
					if (player.invincible < 15) player.invincible = 15;

					if (boss.type != 2) {
						float knock = finisher ? 60.0f : (!player.atkAir ? 20.0f : 6.0f);
						boss.x = Clamp(boss.x + player.dir * knock, 0.0f, kScreenW - boss.w);
					}
				}

				for (int i = 0; i < kMaxEBullets; i++) {
					EBullet& eb = ebullets[i];
					if (!eb.active || eb.wave || eb.reflected || eb.unparryable) continue;
					if (HitBox(ax, ay, aw, ah, eb.x - eb.r, eb.y - eb.r, eb.r * 2.0f, eb.r * 2.0f)) {
						eb.active = false;
					}
				}
			}

			if (introTimer > 0) {
				introTimer--;
			}
			else {
				float bcx = boss.x + boss.w * 0.5f;
				float bcy = boss.y + boss.h * 0.5f;
				float pcx = player.x + player.w * 0.5f;
				float pcy = player.y + player.h * 0.5f;
				float aim = atan2f(pcy - bcy, pcx - bcx);

				if (boss.isBroken) {
					boss.breakTimer--;
					boss.hidden = false;
					boss.timeStopTimer = 0;
					boss.timeStopPrepTimer = 0;
					if (boss.y + boss.h < groundY) {
						boss.vy += kGravity;
						boss.y += boss.vy;
						if (boss.y + boss.h >= groundY) {
							boss.y = groundY - boss.h;
							boss.vy = 0.0f;
						}
					}
					if (boss.breakTimer <= 0) {
						boss.isBroken = false;
						boss.breakGauge = boss.maxBreakGauge;
						boss.state = 0;
						boss.timer = (boss.type == 2) ? 120 : 60;
					}
				}
				else {
					if (boss.type == 0) {
						// (Sniper AI 省略せずそのまま)
						switch (boss.state) {
						case 0:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 1.5f;
							boss.timer--;
							if (boss.timer <= 0) {
								int pick = rand() % 3;
								if (pick == 0) { boss.state = 1; boss.timer = 50; }
								else if (pick == 1) { boss.state = 2; boss.timer = 70; }
								else { boss.state = 3; boss.timer = 100; }
							}
							break;
						case 1:
							boss.timer--;
							if (boss.timer % 15 == 0) FireAngle(ebullets, bcx, bcy - 20.0f, aim, 9.0f, 8.0f);
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 70; }
							break;
						case 2:
							boss.timer--;
							if (boss.timer == 35) {
								for (int i = -1; i <= 1; i++) FireAngle(ebullets, bcx, bcy, aim + i * 0.2f, 7.0f, 10.0f);
							}
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 90; }
							break;
						case 3:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 1.5f;
							boss.timer--;
							if (boss.timer <= 40 && boss.timer > 0 && boss.timer % 4 == 0) {
								float startX = static_cast<float>(rand() % kScreenW);
								float targetX = static_cast<float>(rand() % kScreenW);
								float meteorVx = (targetX - startX) / 55.0f;
								FireEBullet(ebullets, startX, -40.0f, meteorVx, 10.0f, 15.0f, false, true);
							}
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 70; }
							break;
						case 4:
							boss.timer--;
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 60; }
							break;
						}
					}
					else if (boss.type == 1) {
						// (Crusher AI 省略せずそのまま)
						switch (boss.state) {
						case 0:
							if (!isReverseAttackUsed) { isScreenReverse = true; isReverseAttackUsed = true; }
						case 1:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 2.5f;
							boss.timer--;
							if (boss.timer <= 0) {
								boss.state = (rand() % 3 == 0) ? 2 : 3;
								if (boss.state == 1) { boss.timer = 30; boss.dir = (pcx > bcx) ? 1 : -1; }
								else { boss.timer = 25; }
							}
							break;
						case 2:
							boss.x += boss.dir * 16.0f;
							boss.timer--;
							if (boss.x <= 0.0f || boss.x + boss.w >= kScreenW || boss.timer <= 0) {
								shake = 12; boss.state = 0; boss.timer = 80;
							}
							break;
						case 3:
							boss.timer--;
							if (boss.timer <= 0) {
								boss.vy = -22.0f;
								boss.vx = Clamp((pcx - bcx) / 30.0f, -8.0f, 8.0f);
								boss.state = 4;
							}
							break;
						case 4:
							boss.vy += kGravity;
							boss.x += boss.vx;
							boss.y += boss.vy;
							boss.x = Clamp(boss.x, 0.0f, kScreenW - boss.w);
							if (boss.y + boss.h >= groundY) {
								boss.y = groundY - boss.h;
								boss.vy = 0.0f;
								shake = 18;
								FireEBullet(ebullets, boss.x, groundY - 14.0f, -8.0f, 0.0f, 16.0f, true);
								FireEBullet(ebullets, boss.x + boss.w, groundY - 14.0f, 8.0f, 0.0f, 16.0f, true);
								boss.state = 0; boss.timer = 90;
							}
							break;
						case 5:
							boss.timer--;
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 60; }
							break;
						}

					}
					else {
						// ==================================================
						// ステージ3 ウィザード型 ボスAI
						// ==================================================
						if (boss.phase == 1 && boss.hp < static_cast<int>(boss.maxHp * 0.6f)) {
							boss.phase = 2;
							boss.hidden = false;
							boss.vx = 0.0f;
							boss.vy = 0.0f;
							boss.y = groundY - boss.h;
							boss.state = 0;
							boss.timer = 90;
							boss.currentPattern = -1;
							boss.timeStopTimer = 0;
							boss.timeStopPrepTimer = 0;
							boss.flash = 20;
							shake = 18;
							for (int i = 0; i < kMaxLasers; i++) lasers[i].active = false;
							RefillStage3Patterns(boss);
						}

						if (boss.state == 0) {
							boss.hidden = false;
							boss.vx = 0.0f;
							boss.vy = 0.0f;
							boss.y = groundY - boss.h;
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.timer--;
							if (boss.timer <= 0) {
								boss.currentPattern = PopStage3Pattern(boss);
								boss.state = 1;
								boss.subState = 0;
								boss.subTimer = 0;
								boss.repeatCount = 0;
								boss.timeStopPrepTimer = 0;
							}
						}
						else if (boss.state == 1) {
							// フェーズ2の時間停止行動（7〜10）の前には必ず1秒（60フレーム）の溜めを入れる
							if (boss.currentPattern >= 7 && boss.currentPattern <= 10 && boss.timeStopPrepTimer < 60) {
								boss.timeStopPrepTimer++;
								if (boss.timeStopPrepTimer == 60) {
									boss.subTimer = 0;
								}
							}
							else {
								switch (boss.currentPattern) {
								case 0:
									// パターン0: 背後から出現し真っすぐ突撃 (出現から突撃までに0.2秒=12フレームのラグ, 突進中のみ接触ダメージ有効, パリィ無効)
									if (boss.subState == 0) {
										boss.hidden = true;
										boss.subTimer++;
										if (boss.subTimer >= 35) {
											float behindOffset = 150.0f;
											float spawnCx = pcx - player.dir * behindOffset;
											if (spawnCx < 40.0f || spawnCx > kScreenW - 40.0f) {
												spawnCx = pcx + player.dir * behindOffset;
											}
											boss.x = Clamp(spawnCx - boss.w * 0.5f, 0.0f, kScreenW - boss.w);
											boss.y = groundY - boss.h;
											boss.dir = (pcx > boss.x + boss.w * 0.5f) ? 1 : -1;
											boss.hidden = false;
											boss.flash = 8;
											boss.subState = 1;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 1) {
										// 出現から突撃までに0.2秒（12フレーム）のラグ
										boss.subTimer++;
										if (boss.subTimer >= 12) {
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										// 突進中
										boss.x += boss.dir * 22.0f;
										boss.subTimer++;
										if (boss.x <= 0.0f || boss.x + boss.w >= kScreenW || boss.subTimer >= 35) {
											shake = 10;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 1:
									// パターン1: 上部から出現し、高速で落下 (出現から落下までに0.2秒=12フレームのラグ, 落下中のみ接触ダメージ有効, パリィ無効)
									if (boss.subState == 0) {
										boss.hidden = true;
										boss.subTimer++;
										if (boss.subTimer >= 35) {
											boss.x = Clamp(pcx - boss.w * 0.5f, 0.0f, kScreenW - boss.w);
											boss.y = 70.0f;
											boss.dir = (pcx > boss.x + boss.w * 0.5f) ? 1 : -1;
											boss.hidden = false;
											boss.flash = 8;
											boss.subState = 1;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 1) {
										boss.subTimer++;
										if (boss.subTimer >= 12) {
											boss.subState = 2;
											boss.vy = 26.0f;
										}
									}
									else if (boss.subState == 2) {
										boss.y += boss.vy;
										if (boss.y + boss.h >= groundY) {
											boss.y = groundY - boss.h;
											boss.vy = 0.0f;
											shake = 14;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 2:
									// パターン2: 地面をまっ直ぐ移動する弾2を左右から放ちながら、プレイヤーに向けて弾1を放つ（レーザー削除）
									boss.dir = (pcx > bcx) ? 1 : -1;
									boss.subTimer++;
									if (boss.subTimer % 45 == 1) {
										FireEBullet(ebullets, 10.0f, groundY - 14.0f, 7.0f, 0.0f, 12.0f, false, false, true);
										FireEBullet(ebullets, kScreenW - 10.0f, groundY - 14.0f, -7.0f, 0.0f, 12.0f, false, false, true);
									}
									if (boss.subTimer % 36 == 18) {
										FireAngle(ebullets, bcx, bcy, aim, 8.0f, 10.0f, false);
									}
									if (boss.subTimer >= 210) {
										boss.state = 0;
										boss.timer = 120;
									}
									break;

								case 3:
									// パターン3: 空中のランダムな位置へ移動し、プレイヤーに向けてレーザーを放つ（レーザーは1回のみ）
									if (boss.subState == 0) {
										boss.startX = boss.x;
										boss.startY = boss.y;
										boss.targetX = static_cast<float>(100 + rand() % (kScreenW - 200));
										boss.targetY = static_cast<float>(90 + rand() % 220);
										boss.subState = 1;
										boss.subTimer = 0;
									}
									else if (boss.subState == 1) {
										boss.subTimer++;
										float t = boss.subTimer / 20.0f;
										boss.x = boss.startX + (boss.targetX - boss.startX) * t;
										boss.y = boss.startY + (boss.targetY - boss.startY) * t;
										boss.dir = (pcx > boss.x + boss.w * 0.5f) ? 1 : -1;
										if (boss.subTimer >= 20) {
											float curCx = boss.x + boss.w * 0.5f;
											float curCy = boss.y + boss.h * 0.5f;
											float ang = atan2f(pcy - curCy, pcx - curCx);
											float endX, endY;
											CalcRayEnd(curCx, curCy, ang, groundY, &endX, &endY);
											SpawnLaser(lasers, curCx, curCy, endX, endY, 22.0f, 35, 16, true);
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										// 1回のみなのでそのまま着地へ
										if (boss.subTimer >= 52) {
											boss.y = groundY - boss.h;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 4:
									// パターン4: 空中の画面中央へ移動。上方向から60~-60度ずれた方向にレーザーを放ちまくる（放つレーザーの合計と速度を1.5倍に）
									if (boss.subState == 0) {
										boss.startX = boss.x;
										boss.startY = boss.y;
										boss.targetX = kScreenW * 0.5f - boss.w * 0.5f;
										boss.targetY = 180.0f;
										boss.subState = 1;
										boss.subTimer = 0;
									}
									else if (boss.subState == 1) {
										boss.subTimer++;
										float t = boss.subTimer / 25.0f;
										boss.x = boss.startX + (boss.targetX - boss.startX) * t;
										boss.y = boss.startY + (boss.targetY - boss.startY) * t;
										if (boss.subTimer >= 25) {
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										// 1.5倍の頻度（約18フレーム毎、かつ合計発射数を増やす）
										if (boss.subTimer % 18 == 1 && boss.subTimer <= 220) {
											float curCx = boss.x + boss.w * 0.5f;
											float curCy = boss.y + boss.h * 0.5f;
											float degOffset = static_cast<float>((rand() % 121) - 60);
											float ang = -kPi * 0.5f + degOffset * (kPi / 180.0f);
											float endX, endY;
											CalcRayEnd(curCx, curCy, ang, groundY, &endX, &endY);
											SpawnLaser(lasers, curCx, curCy, endX, endY, 22.0f, 25, 14, true);
										}
										if (boss.subTimer >= 260) {
											boss.y = groundY - boss.h;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 5:
									// パターン5: 空中の端へ移動。画面を左右に移動しながら、重力に従う弾1と弾2をばら撒く（落とす弾の数を1.5倍程度増やす）
									if (boss.subState == 0) {
										boss.startX = boss.x;
										boss.startY = boss.y;
										boss.targetX = kScreenW - boss.w - 40.0f;
										boss.targetY = 120.0f;
										boss.subState = 1;
										boss.subTimer = 0;
									}
									else if (boss.subState == 1) {
										boss.subTimer++;
										float t = boss.subTimer / 25.0f;
										boss.x = boss.startX + (boss.targetX - boss.startX) * t;
										boss.y = boss.startY + (boss.targetY - boss.startY) * t;
										if (boss.subTimer >= 25) {
											boss.subState = 2;
											boss.subTimer = 0;
											boss.dir = -1;
										}
									}
									else if (boss.subState == 2) {
										boss.x -= 11.0f;
										boss.subTimer++;
										// 1.5倍増やす（12フレーム毎 -> 8フレーム毎）
										if (boss.subTimer % 8 == 0) {
											bool isBullet2 = ((boss.subTimer / 8) % 2 == 0);
											float bvx = static_cast<float>((rand() % 5) - 2);
											FireEBullet(ebullets, bcx, bcy, bvx, -2.0f, 10.0f, false, false, isBullet2, true, isBullet2);
										}
										if (boss.x <= 40.0f) {
											boss.x = 40.0f;
											boss.subState = 3;
											boss.subTimer = 0;
											boss.dir = 1;
										}
									}
									else if (boss.subState == 3) {
										boss.x += 11.0f;
										boss.subTimer++;
										if (boss.subTimer % 8 == 0) {
											bool isBullet2 = ((boss.subTimer / 8) % 2 == 0);
											float bvx = static_cast<float>((rand() % 5) - 2);
											FireEBullet(ebullets, bcx, bcy, bvx, -2.0f, 10.0f, false, false, isBullet2, true, isBullet2);
										}
										if (boss.x >= kScreenW - boss.w - 40.0f) {
											boss.y = groundY - boss.h;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 6:
									// パターン6: X980地点に移動。16方向に連続して弾をばら撒く。起点方向5度ズレ。右端からX0地点まで持続する縦レーザーを2回放つ
									if (boss.subState == 0) {
										boss.startX = boss.x;
										boss.startY = boss.y;
										boss.targetX = 980.0f;
										boss.targetY = 200.0f;
										boss.subState = 1;
										boss.subTimer = 0;
									}
									else if (boss.subState == 1) {
										boss.subTimer++;
										float t = boss.subTimer / 25.0f;
										boss.x = boss.startX + (boss.targetX - boss.startX) * t;
										boss.y = boss.startY + (boss.targetY - boss.startY) * t;
										if (boss.subTimer >= 25) {
											boss.subState = 2;
											boss.subTimer = 0;
											boss.angleOffset = 0.0f;
											// 1回目の右端からX0地点まで持続する縦レーザー (vx = -kScreenW / activeTimer)
											SpawnLaser(lasers, kScreenW - 20.0f, 0.0f, kScreenW - 20.0f, groundY, 36.0f, 25, 260, false, true, -kScreenW / 260.0f, true);
										}
									}
									else if (boss.subState == 2) {
										boss.dir = -1;
										boss.subTimer++;
										if (boss.subTimer % 28 == 1 && boss.subTimer <= 220) {
											float curCx = boss.x + boss.w * 0.5f;
											float curCy = boss.y + boss.h * 0.5f;
											for (int i = 0; i < 16; i++) { // 32方向から16方向に修正
												float ang = boss.angleOffset + i * (2.0f * kPi / 16.0f);
												bool isBullet2 = (i % 2 == 1);
												FireAngle(ebullets, curCx, curCy, ang, 5.2f, 8.0f, isBullet2);
											}
											boss.angleOffset += 5.0f * (kPi / 180.0f);
										}
										// 行動中に2回目のレーザーを放つ
										if (boss.subTimer == 130) {
											SpawnLaser(lasers, kScreenW - 20.0f, 0.0f, kScreenW - 20.0f, groundY, 36.0f, 25, 130, false, true, -kScreenW / 130.0f, true);
										}
										if (boss.subTimer >= 260) {
											boss.y = groundY - boss.h;
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 7:
									// フェーズ2行動1: 時間停止し、プレイヤー周囲に4つの弾2を設置 (90*4)
									if (boss.subState == 0) {
										boss.timeStopTimer = 90;
										boss.subState = 1;
										boss.subTimer = 0;
										shake = 12;

										const float dist = 160.0f;
										for (int k = 0; k < 4; k++) {
											float ang = k * (kPi * 0.5f);
											float bx = pcx + cosf(ang) * dist;
											float by = pcy + sinf(ang) * dist;

											// 空きスロットを探して確実に設定
											for (int i = 0; i < kMaxEBullets; i++) {
												if (!ebullets[i].active) {
													ebullets[i].active = true;
													ebullets[i].x = bx;
													ebullets[i].y = by;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 0.0f;
													ebullets[i].r = 11.0f;
													ebullets[i].wave = false;
													ebullets[i].reflected = false;
													ebullets[i].meteor = false;
													ebullets[i].unparryable = true; // 弾2
													ebullets[i].useGravity = false;
													ebullets[i].spawnLaserOnLand = false;
													ebullets[i].waitTimeStop = true;
													ebullets[i].aimX = pcx;
													ebullets[i].aimY = pcy;
													ebullets[i].launchSpeed = 11.5f;
													break;
												}
											}
										}
									}
									else if (boss.subState == 1) {
										boss.timeStopTimer--;
										for (int i = 0; i < kMaxEBullets; i++) {
											if (ebullets[i].active && ebullets[i].waitTimeStop) {
												ebullets[i].aimX = pcx;
												ebullets[i].aimY = pcy;
											}
										}
										if (boss.timeStopTimer <= 0) {
											boss.timeStopTimer = 0;
											for (int i = 0; i < kMaxEBullets; i++) {
												if (ebullets[i].active && ebullets[i].waitTimeStop) {
													ebullets[i].waitTimeStop = false;
													float ang = atan2f(ebullets[i].aimY - ebullets[i].y, ebullets[i].aimX - ebullets[i].x);
													ebullets[i].vx = cosf(ang) * ebullets[i].launchSpeed;
													ebullets[i].vy = sinf(ang) * ebullets[i].launchSpeed;
												}
											}
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										if (boss.subTimer >= 40) {
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 8:
									// フェーズ2行動2: 時間停止し、プレイヤー周囲に4つの弾2を設置 (45+90*4)
									if (boss.subState == 0) {
										boss.timeStopTimer = 90;
										boss.subState = 1;
										boss.subTimer = 0;
										shake = 12;

										const float dist = 160.0f;
										for (int k = 0; k < 4; k++) {
											float ang = (45.0f * (kPi / 180.0f)) + k * (kPi * 0.5f);
											float bx = pcx + cosf(ang) * dist;
											float by = pcy + sinf(ang) * dist;

											for (int i = 0; i < kMaxEBullets; i++) {
												if (!ebullets[i].active) {
													ebullets[i].active = true;
													ebullets[i].x = bx;
													ebullets[i].y = by;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 0.0f;
													ebullets[i].r = 11.0f;
													ebullets[i].wave = false;
													ebullets[i].reflected = false;
													ebullets[i].meteor = false;
													ebullets[i].unparryable = true;
													ebullets[i].useGravity = false;
													ebullets[i].spawnLaserOnLand = false;
													ebullets[i].waitTimeStop = true;
													ebullets[i].aimX = pcx;
													ebullets[i].aimY = pcy;
													ebullets[i].launchSpeed = 11.5f;
													break;
												}
											}
										}
									}
									else if (boss.subState == 1) {
										boss.timeStopTimer--;
										for (int i = 0; i < kMaxEBullets; i++) {
											if (ebullets[i].active && ebullets[i].waitTimeStop) {
												ebullets[i].aimX = pcx;
												ebullets[i].aimY = pcy;
											}
										}
										if (boss.timeStopTimer <= 0) {
											boss.timeStopTimer = 0;
											for (int i = 0; i < kMaxEBullets; i++) {
												if (ebullets[i].active && ebullets[i].waitTimeStop) {
													ebullets[i].waitTimeStop = false;
													float ang = atan2f(ebullets[i].aimY - ebullets[i].y, ebullets[i].aimX - ebullets[i].x);
													ebullets[i].vx = cosf(ang) * ebullets[i].launchSpeed;
													ebullets[i].vy = sinf(ang) * ebullets[i].launchSpeed;
												}
											}
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										if (boss.subTimer >= 40) {
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 9:
									// フェーズ2行動3: 時間停止し、プレイヤー周囲に8つの弾1を設置
									if (boss.subState == 0) {
										boss.timeStopTimer = 90;
										boss.subState = 1;
										boss.subTimer = 0;
										shake = 12;

										const float dist = 170.0f;
										for (int k = 0; k < 8; k++) {
											float ang = k * (2.0f * kPi / 8.0f);
											float bx = pcx + cosf(ang) * dist;
											float by = pcy + sinf(ang) * dist;

											for (int i = 0; i < kMaxEBullets; i++) {
												if (!ebullets[i].active) {
													ebullets[i].active = true;
													ebullets[i].x = bx;
													ebullets[i].y = by;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 0.0f;
													ebullets[i].r = 10.0f;
													ebullets[i].wave = false;
													ebullets[i].reflected = false;
													ebullets[i].meteor = false;
													ebullets[i].unparryable = false; // 弾1（パリィ可能）
													ebullets[i].useGravity = false;
													ebullets[i].spawnLaserOnLand = false;
													ebullets[i].waitTimeStop = true;
													ebullets[i].aimX = pcx;
													ebullets[i].aimY = pcy;
													ebullets[i].launchSpeed = 10.0f;
													break;
												}
											}
										}
									}
									else if (boss.subState == 1) {
										boss.timeStopTimer--;
										for (int i = 0; i < kMaxEBullets; i++) {
											if (ebullets[i].active && ebullets[i].waitTimeStop) {
												ebullets[i].aimX = pcx;
												ebullets[i].aimY = pcy;
											}
										}
										if (boss.timeStopTimer <= 0) {
											boss.timeStopTimer = 0;
											for (int i = 0; i < kMaxEBullets; i++) {
												if (ebullets[i].active && ebullets[i].waitTimeStop) {
													ebullets[i].waitTimeStop = false;
													float ang = atan2f(ebullets[i].aimY - ebullets[i].y, ebullets[i].aimX - ebullets[i].x);
													ebullets[i].vx = cosf(ang) * ebullets[i].launchSpeed;
													ebullets[i].vy = sinf(ang) * ebullets[i].launchSpeed;
												}
											}
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										if (boss.subTimer >= 40) {
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;

								case 10:
									// フェーズ2行動4: 時間停止し、空中に10個の弾1、20個の弾2を設置。解除後一斉に下方向へ。着弾後弾2なら縦レーザー
									if (boss.subState == 0) {
										boss.timeStopTimer = 90;
										boss.subState = 1;
										boss.subTimer = 0;
										shake = 14;

										// 10個の弾1
										for (int k = 0; k < 10; k++) {
											float bx = static_cast<float>(50 + rand() % (kScreenW - 100));
											float by = static_cast<float>(50 + rand() % 150);
											for (int i = 0; i < kMaxEBullets; i++) {
												if (!ebullets[i].active) {
													ebullets[i].active = true;
													ebullets[i].x = bx;
													ebullets[i].y = by;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 0.0f;
													ebullets[i].r = 9.0f;
													ebullets[i].wave = false;
													ebullets[i].reflected = false;
													ebullets[i].meteor = false;
													ebullets[i].unparryable = false;
													ebullets[i].useGravity = false;
													ebullets[i].spawnLaserOnLand = false;
													ebullets[i].waitTimeStop = true;
													break;
												}
											}
										}
										// 20個の弾2
										for (int k = 0; k < 20; k++) {
											float bx = static_cast<float>(30 + rand() % (kScreenW - 60));
											float by = static_cast<float>(30 + rand() % 200);
											for (int i = 0; i < kMaxEBullets; i++) {
												if (!ebullets[i].active) {
													ebullets[i].active = true;
													ebullets[i].x = bx;
													ebullets[i].y = by;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 0.0f;
													ebullets[i].r = 9.0f;
													ebullets[i].wave = false;
													ebullets[i].reflected = false;
													ebullets[i].meteor = false;
													ebullets[i].unparryable = true;
													ebullets[i].useGravity = false;
													ebullets[i].spawnLaserOnLand = true; // 着弾後縦レーザー
													ebullets[i].waitTimeStop = true;
													break;
												}
											}
										}
									}
									else if (boss.subState == 1) {
										boss.timeStopTimer--;
										if (boss.timeStopTimer <= 0) {
											boss.timeStopTimer = 0;
											for (int i = 0; i < kMaxEBullets; i++) {
												if (ebullets[i].active && ebullets[i].waitTimeStop) {
													ebullets[i].waitTimeStop = false;
													ebullets[i].vx = 0.0f;
													ebullets[i].vy = 8.0f; // 下方向に動き出す
												}
											}
											boss.subState = 2;
											boss.subTimer = 0;
										}
									}
									else if (boss.subState == 2) {
										boss.subTimer++;
										if (boss.subTimer >= 60) {
											boss.state = 0;
											boss.timer = 120;
										}
									}
									break;
								}
							}
						}
					}
				}

				boss.x = Clamp(boss.x, 0.0f, kScreenW - boss.w);

				// ==================================================
				// レーザーの更新と当たり判定
				// ==================================================
				if (!isTimeStopped) {
					for (int i = 0; i < kMaxLasers; i++) {
						Laser& lz = lasers[i];
						if (!lz.active) continue;

						if (lz.warnTimer > 0) {
							lz.warnTimer--;
							if (lz.warnTimer == 0 && lz.spawnSpread && !lz.spawnedSpread) {
								lz.spawnedSpread = true;
								for (int k = 0; k < 16; k++) {
									float ang = k * (2.0f * kPi / 16.0f);
									bool isBullet2 = (k % 2 == 1);
									FireAngle(ebullets, lz.x2, lz.y2, ang, 5.5f, 8.5f, isBullet2);
								}
							}
						}
						else {
							if (lz.movingVertical) {
								lz.x1 += lz.vx;
								lz.x2 += lz.vx;
								if (lz.x1 < -50.0f || lz.x1 > kScreenW + 50.0f) {
									lz.active = false;
									continue;
								}
							}
							lz.activeTimer--;
							if (lz.activeTimer <= 0) {
								lz.active = false;
								continue;
							}

							bool canHit = true;
							if (lz.airPhaseThrough && !player.onGround) {
								canHit = false;
							}

							if (canHit && player.invincible == 0 && player.dashTimer == 0) {
								if (HitLineBox(lz.x1, lz.y1, lz.x2, lz.y2, lz.width * 0.5f, player.x, player.y, player.w, player.h)) {
									HurtPlayer(player);
								}
							}
						}
					}
				}

				// ==================================================
				// ボスの弾の更新とパリィ・被弾判定
				// ==================================================
				if (!isTimeStopped) {
					for (int i = 0; i < kMaxEBullets; i++) {
						EBullet& b = ebullets[i];
						if (!b.active) continue;

						if (b.waitTimeStop) continue; // 時間停止解除待ちの弾は移動しない

						if (b.useGravity) {
							b.vy += kGravity * 0.45f;
						}

						b.x += b.vx;
						b.y += b.vy;

						if (b.spawnLaserOnLand && b.y >= groundY) {
							SpawnLaser(lasers, b.x, 0.0f, b.x, groundY, 26.0f, 30, 15, false);
							b.active = false;
							continue;
						}

						if (b.x < -60.0f || b.x > kScreenW + 60.0f || b.y < -120.0f || b.y > kScreenH + 60.0f) {
							b.active = false;
							continue;
						}

						if (b.reflected) {
							if (!boss.hidden && HitBox(b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f, boss.x, boss.y, boss.w, boss.h)) {
								b.active = false;
								int baseParryDmg = 100;
								int dmg = boss.isBroken ? static_cast<int>(baseParryDmg * 1.5f) : baseParryDmg;
								boss.hp -= dmg;
								SpawnDamagePopup(popups, boss, dmg, boss.isBroken);
								DamageBossBreak(boss, 150, shake);
								boss.flash = 10;
								shake = 8;
							}
							continue;
						}

						// 弾1(!b.unparryable)のみパリィ可能。弾2(b.unparryable)はパリィ不可
						if (!b.unparryable && player.parryTimer > 0 && HitBox(player.x - 24.0f, player.y - 24.0f, player.w + 48.0f, player.h + 48.0f, b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f)) {
							if (b.wave) {
								b.active = false;
							}
							else {
								b.reflected = true;
								b.useGravity = false;
								float ang = atan2f(bcy - b.y, bcx - b.x);
								b.vx = cosf(ang) * 14.0f;
								b.vy = sinf(ang) * 14.0f;
								b.r = 14.0f;
							}

							player.parryCooldown = 0;
							player.invincible = 20;
							player.parryFlash = 10;
							shake = 6;
						}
						else if (player.invincible == 0 && player.dashTimer == 0 &&
							HitBox(player.x, player.y, player.w, player.h, b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f)) {
							HurtPlayer(player);
							if (!b.wave) b.active = false;
						}
					}
				}

				// ステージ3のボス接触処理（接触ダメージ・接触パリィ完全無効。ただしパターン0・1の突進中のみ接触ダメージを有効化）
				if (boss.type == 2) {
					bool bossRushActive = (boss.state == 1 && (boss.currentPattern == 0 || boss.currentPattern == 1) && boss.subState == 2);
					if (bossRushActive && !boss.isBroken && !boss.hidden && HitBox(player.x, player.y, player.w, player.h, boss.x, boss.y, boss.w, boss.h)) {
						if (player.invincible == 0 && player.dashTimer == 0) {
							HurtPlayer(player);
						}
					}
				}
				else if (!boss.isBroken && !boss.hidden && HitBox(player.x, player.y, player.w, player.h, boss.x, boss.y, boss.w, boss.h)) {
					// ステージ1・2のボス接触処理
					if (player.parryTimer > 0) {
						boss.state = 4;
						boss.timer = 70;
						boss.flash = 8;
						player.invincible = 30;
						player.parryCooldown = 0;
						player.parryFlash = 10;
						player.vy = -9.0f;
						player.onGround = false;
						shake = 10;
					}
					else if (player.invincible == 0 && player.dashTimer == 0) {
						HurtPlayer(player);
					}
				}
			}

			if (boss.hp <= 0) {
				boss.hp = 0;
				for (int i = 0; i < kMaxEBullets; i++) ebullets[i].active = false;
				for (int i = 0; i < kMaxLasers; i++) lasers[i].active = false;
				if (bestFrames[currentStage] == 0 || playFrames < bestFrames[currentStage]) {
					bestFrames[currentStage] = playFrames;
				}
				shake = 0;
				gShakeX = 0;
				gShakeY = 0;
				scene = kVictory;
			}
			else if (player.hp <= 0) {
				player.hp = 0;
				shake = 0;
				gShakeX = 0;
				gShakeY = 0;
				scene = kGameOver;
			}
		}

		meteorAnimationTimer++;
		if (meteorAnimationTimer >= kMeteorAnimationSpeed) {
			meteorAnimationTimer = 0;
			meteorAnimationFrame++;
			if (meteorAnimationFrame >= 4) meteorAnimationFrame = 0;
		}

		if (shake > 0) {
			gShakeX = (rand() % 11) - 5;
			gShakeY = (rand() % 11) - 5;
		}
		else {
			gShakeX = gShakeY = 0;
		}

		Novice::DrawBox(0, 0, kScreenW, kScreenH, 0.0f, 0x1B1F3BFF, kFillModeSolid);

		if (scene == kPlay && boss.type == 2 && boss.timeStopTimer > 0) {
			Novice::DrawBox(0, 0, kScreenW, kScreenH, 0.0f, 0x0A0518CC, kFillModeSolid);
		}

		for (int i = 0; i < activePlatformNum; i++) {
			float h = (i == 0) ? (kScreenH - platforms[i].y) : 20.0f;
			Rect(platforms[i].x, platforms[i].y, platforms[i].w, h, (i == 0) ? 0x3E6B48FF : 0x8B6B4AFF);
		}

		if (scene == kPlay) {
			unsigned int bodyColor = 0;
			if (boss.type == 0) bodyColor = 0x33CC99FF;
			else if (boss.type == 1) bodyColor = 0xCC4433FF;
			else bodyColor = 0x9933CCFF;

			if (boss.isBroken) {
				bodyColor = ((boss.breakTimer / 5) % 2 == 0) ? 0xFFCC00FF : 0x888899FF;
			}
			if (boss.flash > 0) bodyColor = 0xFFFFFFFF;
			if (!boss.isBroken && boss.state == 4 && boss.flash == 0 && (boss.timer / 6) % 2 == 0) bodyColor = 0x66CCFFFF;

			float drawH = boss.h;
			float drawY = boss.y;
			if (boss.isBroken) {
				float crouch = (boss.type == 2) ? 10.0f : 20.0f;
				drawH = boss.h - crouch;
				drawY = boss.y + crouch;
			}

			if (!boss.hidden) {
				Rect(boss.x, drawY, boss.w, drawH, bodyColor);

				if (boss.type == 2) {
					float eyeY = drawY + 20.0f;
					float eyeX = (boss.dir == 1) ? boss.x + boss.w - 26.0f : boss.x + 6.0f;
					Rect(eyeX, eyeY, 8.0f, 10.0f, 0xFFFFFFFF);
					Rect(eyeX + 12.0f, eyeY, 8.0f, 10.0f, 0xFFFFFFFF);
					Rect(eyeX + 2.0f, eyeY + 3.0f, 4.0f, 4.0f, 0x000000FF);
					Rect(eyeX + 14.0f, eyeY + 3.0f, 4.0f, 4.0f, 0x000000FF);

					Rect(boss.x + boss.w * 0.5f - 8.0f, drawY - 18.0f, 16.0f, 18.0f, 0xDD99FFFF);
				}
				else {
					float eyeY = drawY + 40.0f;
					float eyeX = (boss.dir == 1) ? boss.x + boss.w - 70.0f : boss.x + 20.0f;
					Rect(eyeX, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
					Rect(eyeX + 30.0f, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
					Rect(eyeX + 6.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);
					Rect(eyeX + 36.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);
				}
			}

			if (boss.isBroken) {
				PrintfCenter(static_cast<int>(boss.x + boss.w * 0.5f), static_cast<int>(drawY - 55.0f), "BREAK!! (1.5x DMG)");
			}
			else if (boss.type == 2 && boss.timeStopPrepTimer > 0 && boss.timeStopPrepTimer < 60) {
				PrintfCenter(640, 180, "PREPARING TIME STOP...");
			}
			else if (boss.type == 2 && boss.timeStopTimer > 0) {
				PrintfCenter(640, 180, "TIME STOP!!");
			}

			for (int i = 0; i < kMaxLasers; i++) {
				const Laser& lz = lasers[i];
				if (!lz.active) continue;

				if (lz.warnTimer > 0) {
					Novice::DrawLine(
						static_cast<int>(lz.x1) + gShakeX, static_cast<int>(lz.y1) + gShakeY,
						static_cast<int>(lz.x2) + gShakeX, static_cast<int>(lz.y2) + gShakeY,
						0xFF448888
					);
				}
				else {
					bool isTransparent = (lz.airPhaseThrough && !player.onGround);
					unsigned int outerCol = isTransparent ? 0xFF336633 : 0xFF2255DD;
					unsigned int innerCol = isTransparent ? 0xFFFFFF44 : 0xFFFFFFFF;

					if (fabsf(lz.x1 - lz.x2) < 1.0f) {
						float topY = (lz.y1 < lz.y2) ? lz.y1 : lz.y2;
						float botY = (lz.y1 < lz.y2) ? lz.y2 : lz.y1;
						Rect(lz.x1 - lz.width * 0.5f, topY, lz.width, botY - topY, outerCol);
						Rect(lz.x1 - lz.width * 0.25f, topY, lz.width * 0.5f, botY - topY, innerCol);
					}
					else {
						float dx = lz.x2 - lz.x1;
						float dy = lz.y2 - lz.y1;
						float len = sqrtf(dx * dx + dy * dy);
						if (len > 0.0f) {
							float nx = -dy / len;
							float ny = dx / len;
							int halfW = static_cast<int>(lz.width * 0.5f);
							for (int w = -halfW; w <= halfW; w++) {
								unsigned int col = (abs(w) <= halfW / 2) ? innerCol : outerCol;
								Novice::DrawLine(
									static_cast<int>(lz.x1 + nx * w) + gShakeX, static_cast<int>(lz.y1 + ny * w) + gShakeY,
									static_cast<int>(lz.x2 + nx * w) + gShakeX, static_cast<int>(lz.y2 + ny * w) + gShakeY,
									col
								);
							}
						}
					}
				}
			}

			for (int i = 0; i < kMaxEBullets; i++) {
				const EBullet& b = ebullets[i];
				if (!b.active) continue;
				if (b.reflected) {
					Circle(b.x, b.y, b.r, 0x66FFFFFF);
					Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
				}
				else if (boss.type == 2) {
					if (b.unparryable) {
						Circle(b.x, b.y, b.r, 0xFF2266FF);
						Circle(b.x, b.y, b.r * 0.5f, 0x440022FF);
					}
					else {
						Circle(b.x, b.y, b.r, 0x33EEFFFF);
						Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
					}
				}
				else {
					Circle(b.x, b.y, b.r, boss.type == 0 ? 0x00FF88FF : 0xFF8800FF);
					Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
				}
			}

			if (player.dashTimer > 0) {
				for (int i = 1; i <= 3; i++) {
					Rect(player.x - player.dashDir * i * 18.0f, player.y, player.w, player.h, 0x4AA8FF40);
				}
			}
			if (!(player.invincible > 0 && (player.invincible / 4) % 2 == 0)) {
				Rect(player.x, player.y, player.w, player.h, (player.dashTimer > 0) ? 0x4AA8FF99 : 0x4AA8FFFF);
				float eyeXp = (player.dir == 1) ? player.x + 20.0f : player.x + 4.0f;
				Rect(eyeXp, player.y + 10.0f, 8.0f, 8.0f, 0xFFFFFFFF);
			}

			if (player.atkTimer > 0 && player.atkTimer <= player.atkHi && player.atkTimer >= player.atkLo) {
				float sx, sy, sw, sh;
				MeleeBox(player, &sx, &sy, &sw, &sh);
				bool fin = (!player.atkAir && player.atkStep == 8) || (player.atkAir && player.atkStep == 4);
				Rect(sx, sy, sw, sh, fin ? 0xFFAA44A0 : (player.atkAir ? 0xFFFFFFA0 : 0xFFE060A0));
				float cy = sy + sh * 0.5f;
				Rect(sx, cy - 3.0f, sw, 6.0f, 0xFFFFFFFF);
			}

			float pcx2 = player.x + player.w * 0.5f;
			float pcy2 = player.y + player.h * 0.5f;
			if (player.parryTimer > 0) {
				Circle(pcx2, pcy2, 46.0f, 0x66FFFF80);
			}
			if (player.parryFlash > 0) {
				Circle(pcx2, pcy2, 50.0f + (10 - player.parryFlash) * 5.0f, 0xFFFFFF60);
			}

			for (int i = 0; i < kMaxDamagePopups; i++) {
				const DamagePopup& d = popups[i];
				if (d.timer <= 0) continue;
				int rise = kDamagePopupFrames - d.timer;
				unsigned int alpha = (d.timer > 15) ? 255u : static_cast<unsigned int>(d.timer * 255 / 15);
				char buf[16];
				snprintf(buf, sizeof(buf), "%d", d.damage);
				int tx = static_cast<int>(d.x) - GetFont().Width(buf) / 2;
				int ty = static_cast<int>(d.y) - rise;
				unsigned int baseColor = d.isCritical ? 0xFFEE33u : 0xFF3030u;
				GetFont().Draw(tx + 2, ty + 2, buf, alpha);
				GetFont().Draw(tx, ty, buf, (baseColor << 8) | alpha);
			}

			GetFont().Printf(20, 10, "PLAYER");
			for (int i = 0; i < kPlayerMaxHp; i++) {
				Novice::DrawBox(20 + i * 26, 42, 20, 14, 0.0f, (i < player.hp) ? 0xFF5577FF : 0x55555588, kFillModeSolid);
			}
			GetFont().Printf(20, 62, "TIME: %.1f", playFrames / 60.0f);

			const char* bossNames[3] = { "SNIPER BOSS", "CRUSHER BOSS", "WIZARD BOSS" };
			PrintfCenter(640, 6, "STAGE %d : %s", currentStage + 1, bossNames[currentStage]);
			Novice::DrawBox(440, 36, 400, 18, 0.0f, 0x444444FF, kFillModeSolid);
			Novice::DrawBox(440, 36, 400 * boss.hp / boss.maxHp, 18, 0.0f, 0xE03030FF, kFillModeSolid);
			Novice::DrawBox(440, 36, 400, 18, 0.0f, 0xFFFFFFFF, kFillModeWireFrame);

			int breakW = boss.isBroken
				? (400 * boss.breakTimer / kBreakDuration)
				: (400 * boss.breakGauge / boss.maxBreakGauge);
			unsigned int breakColor = boss.isBroken ? 0x44EEFFFF : 0xFFCC22FF;
			Novice::DrawBox(440, 58, 400, 10, 0.0f, 0x333333FF, kFillModeSolid);
			Novice::DrawBox(440, 58, breakW, 10, 0.0f, breakColor, kFillModeSolid);
			Novice::DrawBox(440, 58, 400, 10, 0.0f, 0xCCCCCCFF, kFillModeWireFrame);
		}

		if (scene == kTitle) {
			PrintfCenter(640, 150, "=== BOSS BATTLE ===");
			if ((frame / 30) % 2 == 0) {
				PrintfCenter(640, 360, "PRESS SPACE TO START");
			}
		}
		else if (scene == kStageSelect) {
			PrintfCenter(640, 80, "=== STAGE SELECT ===");
			const char* stageNames[kTotalStages] = { "STAGE 1", "STAGE 2", "STAGE 3" };
			const char* bossTypes[kTotalStages] = { "Sniper Type", "Crusher Type", "Wizard Type" };
			const int boxW = 180;
			const int boxH = 210;
			const int boxGap = 20;
			const int boxStartX = (kScreenW - (boxW * kTotalStages + boxGap * (kTotalStages - 1))) / 2;
			const int boxY = 220;

			for (int i = 0; i < kTotalStages; i++) {
				bool isSelected = (i == selectedStage);
				unsigned int boxColor = isSelected ? 0x336699FF : 0x222233FF;
				unsigned int frameColor = isSelected ? 0xFFFFFFFF : 0x888888FF;
				int boxX = boxStartX + i * (boxW + boxGap);
				int centerX = boxX + boxW / 2;
				Novice::DrawBox(boxX, boxY, boxW, boxH, 0.0f, boxColor, kFillModeSolid);
				Novice::DrawBox(boxX, boxY, boxW, boxH, 0.0f, frameColor, kFillModeWireFrame);
				PrintfCenter(centerX, boxY + 25, "%s", stageNames[i]);
				PrintfCenter(centerX, boxY + 65, "%s", bossTypes[i]);
				if (isSelected) PrintfCenter(centerX, boxY + 160, "^^ SELECT");
			}
		}
		else if (scene == kGameOver) {
			Novice::DrawBox(400, 240, 480, 200, 0.0f, 0x000000CC, kFillModeSolid);
			PrintfCenter(640, 270, "YOU DIED");
			PrintfCenter(640, 390, "PRESS SPACE TO RETURN TO SELECT");
		}
		else if (scene == kVictory) {
			Novice::DrawBox(400, 240, 480, 200, 0.0f, 0x000000CC, kFillModeSolid);
			PrintfCenter(640, 270, "STAGE CLEAR!");
			PrintfCenter(640, 390, "PRESS SPACE TO RETURN TO SELECT");
		}

		Novice::EndFrame();

		if (preKeys[DIK_ESCAPE] == 0 && keys[DIK_ESCAPE] != 0) {
			break;
		}
	}

	Novice::Finalize();
	return 0;
}