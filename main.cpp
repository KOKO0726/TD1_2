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

const int kMaxEBullets = 96;  // ボスの弾
const int kPlatformNum = 4;

const int kPlayerMaxHp = 5;
const int kIntroFrames = 120;
const int kTotalStages = 3;   // ステージ数

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
	int jumpCount;      // 二段ジャンプ用(使用した回数)
	int dashTimer;      // ダッシュ残りフレーム
	int dashDir;
	int dashCooldown;
	int airDashLeft;    // 空中ダッシュ残り回数(着地で回復)
	int parryTimer;     // パリィ判定が出ている残りフレーム
	int parryCooldown;
	int parryFlash;     // パリィ成功エフェクト
	// ---- 近接攻撃 ----
	int atkTimer;       // 攻撃の残りフレーム
	int atkStep;        // コンボの段数(地上:1〜8, 空中:1〜4)
	bool atkAir;        // 空中攻撃か
	bool atkHit;        // この一振りがボスに当たったか
	int atkHi, atkLo;   // 判定が出るフレーム範囲(atkTimerがこの範囲内)
	int atkDmg;
	int comboWindow;    // 次の段を入力できる猶予(この間は空中に留まる)
	int airComboLeft;   // 空中コンボ開始権(着地で回復)
};

struct EBullet {
	float x, y, vx, vy, r;
	bool active;
	bool wave;
	bool reflected; // パリィで跳ね返された弾
	bool meteor; // 流星群
};

struct Platform {
	float x, y, w; // 上面のみ判定
};

struct Boss {
	float x, y, w, h;
	float vx, vy;
	int hp;
	int maxHp;
	int breakGauge;     // 現在のブレイクゲージ
	int maxBreakGauge;  // 最大ブレイクゲージ
	bool isBroken;      // ブレイク状態か
	int breakTimer;     // ブレイク残り時間
	int state;
	int timer;
	int dir;
	int flash;
	int type; // 0:スナイパー, 1:クラッシャー, 2:ウィザード
};

// ダメージ数字の表示(ボスにダメージを与えたとき、数字が浮かんで消える)
const int kMaxDamagePopups = 16;    // 同時に出せる数
const int kDamagePopupFrames = 45;  // 表示している長さ(フレーム)

struct DamagePopup {
	float x, y;     // 出した位置(ボスの頭の上)
	int damage;     // 表示するダメージ量
	int timer;      // 残りフレーム(0 以下 = 表示していない)
	bool isCritical;// ブレイク中(1.5倍)の強調表示用
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

// centerX を中心にして文字を出す(書式は printf と同じ)
static void PrintfCenter(int centerX, int y, const char* format, ...) {
	char buf[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	GetFont().Draw(centerX - GetFont().Width(buf) / 2, y, buf);
}

// ==================================================
// 共通関数
// ==================================================
static bool HitBox(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static bool LandOnPlatform(const Platform* plats, float x, float w, float prevBottom, float newBottom, float* landY) {
	for (int i = 0; i < kPlatformNum; i++) {
		const Platform& p = plats[i];
		if (x + w > p.x && x < p.x + p.w && prevBottom <= p.y && newBottom >= p.y) {
			*landY = p.y;
			return true;
		}
	}
	return false;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ダメージ数字を1つ出す(空いている枠に入れる)
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

// ボスのブレイクゲージを削る処理
static void DamageBossBreak(Boss& boss, int amount, int& shake) {
	if (boss.isBroken || boss.hp <= 0) return;

	boss.breakGauge -= amount;
	if (boss.breakGauge <= 0) {
		boss.breakGauge = 0;
		boss.isBroken = true;
		boss.breakTimer = kBreakDuration;
		boss.vx = 0.0f;
		boss.vy = 0.0f;
		boss.state = 0; // 進行中の攻撃状態をリセット
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
	bool meteor = false
)
{
	for (int i = 0; i < kMaxEBullets; i++)
	{
		if (!arr[i].active)
		{
			arr[i].active = true;
			arr[i].x = x;
			arr[i].y = y;
			arr[i].vx = vx;
			arr[i].vy = vy;
			arr[i].r = r;
			arr[i].wave = wave;
			arr[i].reflected = false;
			arr[i].meteor = meteor;
			return;
		}
	}
}

static void FireAngle(EBullet* arr, float x, float y, float angle, float speed, float r)
{
	FireEBullet(arr, x, y, cosf(angle) * speed, sinf(angle) * speed, r, false);
}

//==================================================
// ボスの種類ごとに初期化する関数
//==================================================
static void ResetGame(Player& pl, EBullet* ebullets, Boss& boss, float groundY, int stage)
{
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

	boss.w = 140.0f;
	boss.h = 140.0f;
	boss.x = 1000.0f;
	boss.y = groundY - boss.h;
	boss.vx = boss.vy = 0.0f;
	// ステージごとにボスの種類を決定 (0:スナイパー, 1:クラッシャー, 2:ウィザード)
	boss.type = stage;
	// ボスのHPを100倍に設定 (元: 50, 65, 80)
	const int bossHpTable[kTotalStages] = { 5000, 6500, 8000 };
	boss.maxHp = bossHpTable[stage];
	boss.hp = boss.maxHp;

	// ブレイクゲージの初期化 (パリィ数回でブレイクできる値に設定)
	const int bossBreakTable[kTotalStages] = { 2000, 2500, 3000 };
	boss.maxBreakGauge = bossBreakTable[stage];
	boss.breakGauge = boss.maxBreakGauge;
	boss.isBroken = false;
	boss.breakTimer = 0;

	boss.state = 0;
	boss.timer = 60;
	boss.dir = -1;
	boss.flash = 0;
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
		// 地上コンボ (1〜8段目) - 攻撃力を100倍に設定
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
				pl.atkDmg = 200; // 元: 2
			}
			else {
				pl.atkDmg = 100; // 元: 1
			}
		}
		else {
			// 8段目（最後の一撃：高威力）
			pl.atkTimer = 26;
			pl.atkHi = 20;
			pl.atkLo = 6;
			pl.atkDmg = 600; // 元: 6
		}
	}
	else if (step < 4) {
		pl.atkTimer = 14;
		pl.atkHi = 10;
		pl.atkLo = 3;
		pl.atkDmg = 200; // 元: 2
		pl.vy = 0.0f;
	}
	else {
		pl.atkTimer = 24;
		pl.atkHi = 18;
		pl.atkLo = 5;
		pl.atkDmg = 400; // 元: 4
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
			// 8段目は大きめの判定
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

// Windowsアプリのエントリーポイント
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Novice::Initialize(kWindowTitle, kScreenW, kScreenH);
	GetFont().Create();

	char keys[256] = { 0 };
	char preKeys[256] = { 0 };

	// ==================================================
	// 画像
	// ==================================================
	int meteorTexture = Novice::LoadTexture("./Resources/images/meteor.png");

	int meteorAnimationFrame = 0;
	int meteorAnimationTimer = 0;

	const int kMeteorAnimationSpeed = 5;
	const int kMeteorFrameWidth = 64;
	const int kMeteorFrameHeight = 64;

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
	Boss boss;
	DamagePopup popups[kMaxDamagePopups] = {};
	int selectedStage = 0;
	int currentStage = 0;
	int bestFrames[kTotalStages] = { 0, 0, 0 };

	ResetGame(player, ebullets, boss, groundY, selectedStage);

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
				ResetGame(player, ebullets, boss, groundY, currentStage);
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

			// ジャンプキーのトリガー・長押しによる高さ変更判定
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

			// ジャンプ中、キーを離していたら上昇を強く抑える（小ジャンプ対応：調整幅拡大）
			if (!player.onGround && player.vy < 0.0f && !jumpHeld) {
				player.vy += 1.2f;
			}

			// ダッシュ：左シフト
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

			// パリィ：右クリック (buttonNumber = 1)
			if (Novice::IsTriggerMouse(1) && player.parryCooldown == 0 && player.dashTimer == 0) {
				CancelAttack(player);
				player.parryTimer = 12;
				player.parryCooldown = 45;
			}

			// 近接攻撃：左クリック (buttonNumber = 0)
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

				// コンボの継続判定（地上は8連、空中は4連）
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
					// 攻撃中の前進移動仕様を削除（その場で停止）
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
			if (player.vy >= 0.0f && LandOnPlatform(platforms, player.x, player.w, prevBottom, player.y + player.h, &landY)) {
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
			if (boss.flash > 0) boss.flash--;
			if (shake > 0) shake--;
			playFrames++;
			for (int i = 0; i < kMaxDamagePopups; i++) {
				if (popups[i].timer > 0) popups[i].timer--;
			}

			// 近接攻撃のヒット判定
			if (player.atkTimer > 0 && player.atkTimer <= player.atkHi && player.atkTimer >= player.atkLo) {
				float ax, ay, aw, ah;
				MeleeBox(player, &ax, &ay, &aw, &ah);

				if (introTimer == 0 && !player.atkHit && HitBox(ax, ay, aw, ah, boss.x, boss.y, boss.w, boss.h)) {
					bool finisher = (!player.atkAir && player.atkStep == 8) || (player.atkAir && player.atkStep == 4);
					player.atkHit = true;

					// ブレイク中はダメージ1.5倍
					int dmg = boss.isBroken ? static_cast<int>(player.atkDmg * 1.5f) : player.atkDmg;
					boss.hp -= dmg;
					SpawnDamagePopup(popups, boss, dmg, boss.isBroken);

					boss.flash = 6;
					shake = finisher ? 16 : 6;
					if (player.invincible < 15) player.invincible = 15;

					// ステージ3（ウィザード型：boss.type == 2）のみノックバックしない
					if (boss.type != 2) {
						float knock = finisher ? 60.0f : (!player.atkAir ? 20.0f : 6.0f);
						boss.x = Clamp(boss.x + player.dir * knock, 0.0f, kScreenW - boss.w);
					}
				}

				for (int i = 0; i < kMaxEBullets; i++) {
					EBullet& eb = ebullets[i];
					if (!eb.active || eb.wave || eb.reflected) continue;
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

				// ブレイク中の行動停止処理
				if (boss.isBroken) {
					boss.breakTimer--;
					// 空中でブレイクした場合は地面まで落下させる
					if (boss.y + boss.h < groundY) {
						boss.vy += kGravity;
						boss.y += boss.vy;
						if (boss.y + boss.h >= groundY) {
							boss.y = groundY - boss.h;
							boss.vy = 0.0f;
						}
					}
					// ブレイク終了で復帰
					if (boss.breakTimer <= 0) {
						boss.isBroken = false;
						boss.breakGauge = boss.maxBreakGauge;
						boss.state = 0;
						boss.timer = 60;
					}
				}
				else {
					// 通常時のボスAI処理
					if (boss.type == 0) {
						switch (boss.state) {
						case 0:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 1.5f;
							boss.timer--;
							if (boss.timer <= 0) {
								int pick = rand() % 3;

								if (pick == 0)
								{
									boss.state = 1;
									boss.timer = 50;
								}
								else if (pick == 1)
								{
									boss.state = 2;
									boss.timer = 70;
								}
								else
								{
									// ステージ１の流星群攻撃のチャージ
									boss.state = 3;
									boss.timer = 100;
								}
							}
							break;
						case 1:
							boss.timer--;
							if (boss.timer % 15 == 0) {
								FireAngle(ebullets, bcx, bcy - 20.0f, aim, 9.0f, 8.0f);
							}
							if (boss.timer <= 0) {
								boss.state = 0;
								boss.timer = 70;
							}
							break;
						case 2:
							boss.timer--;
							if (boss.timer == 35) {
								for (int i = -1; i <= 1; i++) {
									FireAngle(ebullets, bcx, bcy, aim + i * 0.2f, 7.0f, 10.0f);
								}
							}
							if (boss.timer <= 0) {
								boss.state = 0;
								boss.timer = 90;
							}
							break;
						case 3:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 1.5f;
							boss.timer--;
							if (boss.timer <= 40 && boss.timer > 0 && boss.timer % 4 == 0)
							{
								float startX = static_cast<float>(rand() % kScreenW);
								float targetX = static_cast<float>(rand() % kScreenW);
								float meteorVx = (targetX - startX) / 55.0f;
								FireEBullet(
									ebullets,
									startX,
									-40.0f,
									meteorVx,
									10.0f,
									15.0f,
									false,
									true
								);
							}
							if (boss.timer <= 0) {
								boss.state = 0;
								boss.timer = 70;
							}
							break;
						case 4:
							boss.timer--;
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 60; }
							break;
						}
					}
					else if (boss.type == 1) {
						switch (boss.state) {
						case 0:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 2.5f;
							boss.timer--;
							if (boss.timer <= 0) {
								boss.state = (rand() % 2 == 0) ? 1 : 2;
								if (boss.state == 1) { boss.timer = 30; boss.dir = (pcx > bcx) ? 1 : -1; }
								else { boss.timer = 25; }
							}
							break;
						case 1:
							boss.x += boss.dir * 16.0f;
							boss.timer--;
							if (boss.x <= 0.0f || boss.x + boss.w >= kScreenW || boss.timer <= 0) {
								shake = 12;
								boss.state = 0;
								boss.timer = 80;
							}
							break;
						case 2:
							boss.timer--;
							if (boss.timer <= 0) {
								boss.vy = -22.0f;
								boss.vx = Clamp((pcx - bcx) / 30.0f, -8.0f, 8.0f);
								boss.state = 3;
							}
							break;
						case 3:
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
								boss.state = 0;
								boss.timer = 90;
							}
							break;
						case 4:
							boss.timer--;
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 60; }
							break;
						}
					}
					else {
						switch (boss.state) {
						case 0:
							boss.dir = (pcx > bcx) ? 1 : -1;
							boss.x += boss.dir * 2.0f;
							boss.y = 400.0f + sinf(frame * 0.08f) * 120.0f;
							boss.timer--;
							if (boss.timer <= 0) {
								int pick = rand() % 3;
								if (pick == 0) boss.state = 1;
								else if (pick == 1) boss.state = 2;
								else boss.state = 3;
								boss.timer = 60;
							}
							break;
						case 1:
							boss.timer--;
							if (boss.timer == 30) {
								for (int i = 0; i < 12; i++) {
									float ang = i * (2.0f * kPi / 12.0f);
									FireAngle(ebullets, bcx, bcy, ang, 5.5f, 10.0f);
								}
							}
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 70; }
							break;
						case 2:
							boss.timer--;
							if (boss.timer % 10 == 0) {
								FireAngle(ebullets, bcx, bcy, aim, 8.0f, 12.0f);
							}
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 80; }
							break;
						case 3:
							boss.timer--;
							if (boss.timer == 30) {
								boss.x = kScreenW * 0.5f - boss.w * 0.5f;
								shake = 10;
								for (int i = 0; i < 16; i++) {
									float ang = i * (2.0f * kPi / 16.0f);
									FireAngle(ebullets, bcx, bcy, ang, 6.0f, 8.0f);
								}
							}
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 90; }
							break;
						case 4:
							boss.timer--;
							if (boss.timer <= 0) { boss.state = 0; boss.timer = 60; }
							break;
						}
						if (boss.type == 2 && boss.state != 3) {
							boss.y = Clamp(boss.y, 200.0f, groundY - boss.h);
						}
					}
				}

				boss.x = Clamp(boss.x, 0.0f, kScreenW - boss.w);
				// ボスの弾の更新とパリィ・被弾判定
				for (int i = 0; i < kMaxEBullets; i++) {
					EBullet& b = ebullets[i];
					if (!b.active) continue;
					b.x += b.vx;
					b.y += b.vy;
					if (b.x < -60.0f || b.x > kScreenW + 60.0f || b.y < -60.0f || b.y > kScreenH + 60.0f) {
						b.active = false;
						continue;
					}
					if (b.reflected) {
						if (HitBox(b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f, boss.x, boss.y, boss.w, boss.h)) {
							b.active = false;
							// パリィ威力は25倍 (4 * 25 = 100)、ブレイク中はさらに1.5倍
							int baseParryDmg = 100;
							int dmg = boss.isBroken ? static_cast<int>(baseParryDmg * 1.5f) : baseParryDmg;
							boss.hp -= dmg;
							SpawnDamagePopup(popups, boss, dmg, boss.isBroken);

							// パリィした弾がボスに当たった時のみブレイクゲージを削る
							DamageBossBreak(boss, 150, shake);

							boss.flash = 10;
							shake = 8;
						}
						continue;
					}
					if (player.parryTimer > 0 && HitBox(player.x - 24.0f, player.y - 24.0f, player.w + 48.0f, player.h + 48.0f, b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f)) {
						if (b.wave) {
							b.active = false;
						}
						else {
							b.reflected = true;
							float ang = atan2f(bcy - b.y, bcx - b.x);
							b.vx = cosf(ang) * 14.0f;
							b.vy = sinf(ang) * 14.0f;
							b.r = 14.0f;
						}
						// ※ここでのブレイクゲージ削り処理を削除

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

				// ボス本体との当たり判定（ブレイク中は接触ダメージなし）
				if (!boss.isBroken && HitBox(player.x, player.y, player.w, player.h, boss.x, boss.y, boss.w, boss.h)) {
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

		//==========================================
		// アニメーション更新処理
		//==========================================
		meteorAnimationTimer++;
		if (meteorAnimationTimer >= kMeteorAnimationSpeed)
		{
			meteorAnimationTimer = 0;
			meteorAnimationFrame++;
			if (meteorAnimationFrame >= 4)
			{
				meteorAnimationFrame = 0;
			}
		}

		// ==========================================
		// 描画処理
		// ==========================================
		if (shake > 0) {
			gShakeX = (rand() % 11) - 5;
			gShakeY = (rand() % 11) - 5;
		}
		else {
			gShakeX = gShakeY = 0;
		}

		Novice::DrawBox(0, 0, kScreenW, kScreenH, 0.0f, 0x1B1F3BFF, kFillModeSolid);

		for (int i = 0; i < kPlatformNum; i++) {
			float h = (i == 0) ? (kScreenH - platforms[i].y) : 20.0f;
			Rect(platforms[i].x, platforms[i].y, platforms[i].w, h, (i == 0) ? 0x3E6B48FF : 0x8B6B4AFF);
		}

		if (scene == kPlay) {
			unsigned int bodyColor = 0;
			if (boss.type == 0) bodyColor = 0x33CC99FF;
			else if (boss.type == 1) bodyColor = 0xCC4433FF;
			else bodyColor = 0x9933CCFF;

			// ブレイク中は色を変えて行動停止を視覚化
			if (boss.isBroken) {
				bodyColor = ((boss.breakTimer / 5) % 2 == 0) ? 0xFFCC00FF : 0x888899FF;
			}
			if (boss.flash > 0) bodyColor = 0xFFFFFFFF;
			if (!boss.isBroken && boss.state == 4 && boss.flash == 0 && (boss.timer / 6) % 2 == 0) bodyColor = 0x66CCFFFF;

			float drawH = boss.h;
			float drawY = boss.y;
			// ブレイク中は少し姿勢を低くする
			if (boss.isBroken) {
				drawH = boss.h - 20.0f;
				drawY = boss.y + 20.0f;
			}
			else if (boss.type == 0 && boss.state == 3) {
				drawH = boss.h - 15.0f;
				drawY = boss.y + 15.0f;
				bodyColor = 0xBB66FFFF;
			}
			else if (boss.type == 1 && boss.state == 2) {
				drawH = boss.h - 30.0f;
				drawY = boss.y + 30.0f;
			}

			Rect(boss.x, drawY, boss.w, drawH, bodyColor);

			float eyeY = drawY + 40.0f;
			float eyeX = (boss.dir == 1) ? boss.x + boss.w - 70.0f : boss.x + 20.0f;
			Rect(eyeX, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
			Rect(eyeX + 30.0f, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
			Rect(eyeX + 6.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);
			Rect(eyeX + 36.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);

			if (boss.type == 0) {
				Rect(boss.x + boss.w * 0.5f - 5.0f, drawY - 30.0f, 10.0f, 30.0f, 0xCCCCCCFF);
			}
			else if (boss.type == 1) {
				Rect(boss.x + 10.0f, drawY - 20.0f, 20.0f, 20.0f, 0xCCCCCCFF);
				Rect(boss.x + boss.w - 30.0f, drawY - 20.0f, 20.0f, 20.0f, 0xCCCCCCFF);
			}
			else {
				Rect(boss.x + boss.w * 0.5f - 15.0f, drawY - 35.0f, 30.0f, 35.0f, 0xDD99FFFF);
			}

			// ブレイク中の頭上テキスト
			if (boss.isBroken) {
				PrintfCenter(
					static_cast<int>(boss.x + boss.w * 0.5f),
					static_cast<int>(drawY - 55.0f),
					"BREAK!! (1.5x DMG)"
				);
			}
			else if (boss.type == 0 && boss.state == 3) {
				float centerX = boss.x + boss.w * 0.5f;
				float centerY = boss.y + boss.h * 0.5f;
				float effectSize = 60.0f + (frame % 20) * 2.0f;
				Circle(centerX, centerY, effectSize, 0xAA66FF55);
				PrintfCenter(static_cast<int>(centerX), static_cast<int>(boss.y - 45.0f), "CHARGING...");
			}

			for (int i = 0; i < kMaxEBullets; i++) {
				const EBullet& b = ebullets[i];
				if (!b.active) continue;
				if (b.reflected) {
					Circle(b.x, b.y, b.r, 0x66FFFFFF);
					Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
				}
				else if (b.meteor)
				{
					Novice::DrawSpriteRect(
						static_cast<int>(b.x - kMeteorFrameWidth * 0.5f) + gShakeX,
						static_cast<int>(b.y - kMeteorFrameHeight * 0.5f) + gShakeY,
						meteorAnimationFrame * kMeteorFrameWidth,
						0,
						kMeteorFrameWidth,
						kMeteorFrameHeight,
						meteorTexture,
						1.0f / 4.0f,
						1.0f,
						0.0f,
						0xFFFFFFFF
					);
				}
				else {
					Circle(
						b.x,
						b.y,
						b.r,
						boss.type == 0 ? 0x00FF88FF :
						(boss.type == 1 ? 0xFF8800FF : 0xFF44FFFF)
					);
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

			if (!player.atkAir && player.atkStep > 0 && (player.atkTimer > 0 || player.comboWindow > 0)) {
				GetFont().Printf(static_cast<int>(player.x) - 10, static_cast<int>(player.y) - 30, "COMBO %d/8", player.atkStep);
			}
			else if (player.atkAir && player.atkStep > 0 && (player.atkTimer > 0 || player.comboWindow > 0)) {
				GetFont().Printf(static_cast<int>(player.x) - 10, static_cast<int>(player.y) - 30, "AIR %d/4", player.atkStep);
			}

			float pcx2 = player.x + player.w * 0.5f;
			float pcy2 = player.y + player.h * 0.5f;
			if (player.parryTimer > 0) {
				Circle(pcx2, pcy2, 46.0f, 0x66FFFF80);
			}
			if (player.parryFlash > 0) {
				Circle(pcx2, pcy2, 50.0f + (10 - player.parryFlash) * 5.0f, 0xFFFFFF60);
			}

			// ---- ダメージ表示(通常は赤、ブレイク中の1.5倍ダメージは黄色で表示) ----
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

			// ---- HUD ----
			GetFont().Printf(20, 10, "PLAYER");
			for (int i = 0; i < kPlayerMaxHp; i++) {
				Novice::DrawBox(20 + i * 26, 42, 20, 14, 0.0f, (i < player.hp) ? 0xFF5577FF : 0x55555588, kFillModeSolid);
			}
			GetFont().Printf(20, 62, "TIME: %.1f", playFrames / 60.0f);

			bool dashReady = player.dashCooldown == 0 && (player.onGround || player.airDashLeft > 0);
			bool parryReady = player.parryCooldown == 0;
			Novice::DrawBox(20, 96, 70, 26, 0.0f, dashReady ? 0x44DD66FF : 0x55555588, kFillModeSolid);
			GetFont().Printf(26, 96, "DASH");
			Novice::DrawBox(100, 96, 80, 26, 0.0f, parryReady ? 0x44CCFFFF : 0x55555588, kFillModeSolid);
			GetFont().Printf(106, 96, "PARRY");

			const char* bossNames[3] = { "SNIPER BOSS", "CRUSHER BOSS", "WIZARD BOSS" };
			PrintfCenter(640, 6, "STAGE %d : %s", currentStage + 1, bossNames[currentStage]);
			// ボスHPバー
			Novice::DrawBox(440, 36, 400, 18, 0.0f, 0x444444FF, kFillModeSolid);
			Novice::DrawBox(440, 36, 400 * boss.hp / boss.maxHp, 18, 0.0f, 0xE03030FF, kFillModeSolid);
			Novice::DrawBox(440, 36, 400, 18, 0.0f, 0xFFFFFFFF, kFillModeWireFrame);

			// ボスブレイクゲージバー (HPバーの直下)
			int breakW = boss.isBroken
				? (400 * boss.breakTimer / kBreakDuration)
				: (400 * boss.breakGauge / boss.maxBreakGauge);
			unsigned int breakColor = boss.isBroken ? 0x44EEFFFF : 0xFFCC22FF;
			Novice::DrawBox(440, 58, 400, 10, 0.0f, 0x333333FF, kFillModeSolid);
			Novice::DrawBox(440, 58, breakW, 10, 0.0f, breakColor, kFillModeSolid);
			Novice::DrawBox(440, 58, 400, 10, 0.0f, 0xCCCCCCFF, kFillModeWireFrame);

			if (introTimer > 0) {
				if ((introTimer / 10) % 2 == 0) {
					PrintfCenter(640, 250, "!! WARNING !!");
				}
				PrintfCenter(640, 284, "%s APPEARS", bossNames[currentStage]);
			}
		}

		if (scene == kTitle) {
			PrintfCenter(640, 150, "=== BOSS BATTLE ===");
			if ((frame / 30) % 2 == 0) {
				PrintfCenter(640, 360, "PRESS SPACE TO START");
			}
		}
		else if (scene == kStageSelect) {
			PrintfCenter(640, 80, "=== STAGE SELECT ===");
			PrintfCenter(640, 120, "Use A / D or Left / Right to select, SPACE to start");

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

				if (isSelected) {
					PrintfCenter(centerX, boxY + 160, "^^ SELECT");
				}
			}

			const int descX = 334;
			GetFont().Printf(descX, 480, "STAGE 1 : Sniper (Precise shots & spread bullets)");
			GetFont().Printf(descX, 510, "STAGE 2 : Crusher (High-speed charge & shockwaves)");
			GetFont().Printf(descX, 540, "STAGE 3 : Wizard (Omnidirectional magic & teleport)");
		}
		else if (scene == kGameOver) {
			Novice::DrawBox(400, 240, 480, 200, 0.0f, 0x000000CC, kFillModeSolid);
			PrintfCenter(640, 270, "YOU DIED");
			PrintfCenter(640, 310, "BOSS HP LEFT: %d / %d", boss.hp, boss.maxHp);
			PrintfCenter(640, 390, "PRESS SPACE TO RETURN TO SELECT");
		}
		else if (scene == kVictory) {
			Novice::DrawBox(400, 240, 480, 200, 0.0f, 0x000000CC, kFillModeSolid);
			PrintfCenter(640, 270, "STAGE CLEAR!");
			PrintfCenter(640, 310, "TIME: %.1f sec", playFrames / 60.0f);
			PrintfCenter(640, 344, "BEST: %.1f sec", bestFrames[currentStage] / 60.0f);
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