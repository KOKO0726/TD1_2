#include <Novice.h>
#include <math.h>
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

const int kMaxBullets = 16;   // プレイヤーの弾
const int kMaxEBullets = 96;  // ボスの弾
const int kPlatformNum = 4;

const int kPlayerMaxHp = 5;
const int kBossMaxHp = 60;
const int kIntroFrames = 120;

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
	int shotCooldown;
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
	int atkStep;        // 空中コンボの段数(1〜4) / 地上は1
	bool atkAir;        // 空中攻撃か
	bool atkHit;        // この一振りがボスに当たったか
	int atkHi, atkLo;   // 判定が出るフレーム範囲(atkTimerがこの範囲内)
	int atkDmg;
	int comboWindow;    // 次の段を入力できる猶予(この間は空中に留まる)
	int airComboLeft;   // 空中コンボ開始権(着地で回復)
};

struct Bullet {
	float x, y, vx;
	bool active;
};

// ボスの弾(wave=trueは地面を走る衝撃波)
struct EBullet {
	float x, y, vx, vy, r;
	bool active;
	bool wave;
	bool reflected; // パリィで跳ね返された弾(ボスに当たる)
};

struct Platform {
	float x, y, w; // 上面のみ判定
};

enum BossState {
	kBWalk,       // 歩いて接近
	kBShoot,      // 弾を撃つ
	kBJumpWind,   // ジャンプ前の溜め
	kBAir,        // 空中(着地で衝撃波)
	kBChargeWind, // 突進前の溜め
	kBCharge,     // 突進
	kBBurst,      // 全方位弾
	kBStun        // パリィで怯み中
};

struct Boss {
	float x, y, w, h;
	float vx, vy;
	int hp;
	int state;
	int timer;
	int dir;
	int flash;
};

enum Scene { kTitle, kPlay, kGameOver, kVictory };

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

static void FireEBullet(EBullet* arr, float x, float y, float vx, float vy, float r, bool wave) {
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
			return;
		}
	}
}

// 角度指定で撃つ
static void FireAngle(EBullet* arr, float x, float y, float angle, float speed, float r) {
	FireEBullet(arr, x, y, cosf(angle) * speed, sinf(angle) * speed, r, false);
}

static void ResetGame(Player& pl, Bullet* bullets, EBullet* ebullets, Boss& boss, float groundY) {
	pl.x = 200.0f;
	pl.y = groundY - 48.0f;
	pl.vx = pl.vy = 0.0f;
	pl.w = 32.0f;
	pl.h = 48.0f;
	pl.onGround = true;
	pl.dir = 1;
	pl.hp = kPlayerMaxHp;
	pl.invincible = 0;
	pl.shotCooldown = 0;
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

	for (int i = 0; i < kMaxBullets; i++) bullets[i].active = false;
	for (int i = 0; i < kMaxEBullets; i++) ebullets[i].active = false;

	boss.w = 140.0f;
	boss.h = 140.0f;
	boss.x = 1000.0f;
	boss.y = groundY - boss.h;
	boss.vx = boss.vy = 0.0f;
	boss.hp = kBossMaxHp;
	boss.state = kBWalk;
	boss.timer = 60;
	boss.dir = -1;
	boss.flash = 0;
}

// 近接攻撃を中断(被弾・ジャンプ・ダッシュ・パリィ・着地)
static void CancelAttack(Player& pl) {
	pl.atkTimer = 0;
	pl.atkStep = 0;
	pl.atkHit = false;
	pl.comboWindow = 0;
}

// 近接攻撃の開始
//  地上: 踏み込みながらの重い一撃(ダメージ3)
//  空中: 1〜3段目は素早い斬り(ダメージ2)、4段目は大振りの締め(ダメージ4)。
//        いずれも攻撃中は空中に留まる
static void StartAttack(Player& pl, bool air, int step, int dir) {
	pl.dir = dir;
	pl.atkAir = air;
	pl.atkStep = step;
	pl.atkHit = false;
	pl.comboWindow = 0;
	if (!air) {
		pl.atkTimer = 20;
		pl.atkHi = 14;
		pl.atkLo = 6;
		pl.atkDmg = 3;
	}
	else if (step < 4) {
		pl.atkTimer = 14;
		pl.atkHi = 10;
		pl.atkLo = 3;
		pl.atkDmg = 2;
		pl.vy = 0.0f;
	}
	else {
		pl.atkTimer = 24;
		pl.atkHi = 18;
		pl.atkLo = 5;
		pl.atkDmg = 4;
		pl.vy = 0.0f;
	}
}

// 現在の攻撃の判定ボックス
static void MeleeBox(const Player& pl, float* x, float* y, float* w, float* h) {
	float ov = 0.0f;
	if (!pl.atkAir) {
		*w = 90.0f;
		*h = pl.h + 10.0f;
		*y = pl.y - 5.0f;
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

// ボスのフェーズ(1〜3)
static int BossPhase(const Boss& b) {
	if (b.hp > kBossMaxHp * 2 / 3) return 1;
	if (b.hp > kBossMaxHp / 3) return 2;
	return 3;
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// ライブラリの初期化
	Novice::Initialize(kWindowTitle, kScreenW, kScreenH);

	// フォントの初期化(Novice::Initialize の後に1回だけ)
	// EnkaDotMincho24.ttf は exe を実行するフォルダ(プロジェクトフォルダ)に置くこと
	// 画面に出す文字は半角英数字だけなので、追加の漢字は不要
	GetFont().Create(L"PixelMplus12-Bold.ttf", L"PixelMplus12", 28);

	// キー入力結果を受け取る箱
	char keys[256] = { 0 };
	char preKeys[256] = { 0 };

	srand(static_cast<unsigned int>(time(nullptr)));

	// ---------- ゲームデータ ----------
	Platform platforms[kPlatformNum] = {
		{0.0f, 640.0f, 1280.0f}, // 地面
		{120.0f, 500.0f, 260.0f},
		{900.0f, 500.0f, 260.0f},
		{510.0f, 370.0f, 260.0f},
	};
	const float groundY = platforms[0].y;

	Player player;
	Bullet bullets[kMaxBullets];
	EBullet ebullets[kMaxEBullets];
	Boss boss;
	ResetGame(player, bullets, ebullets, boss, groundY);

	Scene scene = kTitle;
	int introTimer = 0;
	int shake = 0;
	int playFrames = 0;
	int bestFrames = 0; // 0 = 未クリア
	int frame = 0;

	// ウィンドウの×ボタンが押されるまでループ
	while (Novice::ProcessMessage() == 0) {
		// フレームの開始
		Novice::BeginFrame();

		// キー入力を受け取る
		memcpy(preKeys, keys, 256);
		Novice::GetHitKeyStateAll(keys);

		frame++;

		///
		/// ↓更新処理ここから
		///

		bool pressStart = (preKeys[DIK_SPACE] == 0 && keys[DIK_SPACE] != 0) || (preKeys[DIK_RETURN] == 0 && keys[DIK_RETURN] != 0);

		if (scene == kTitle || scene == kGameOver || scene == kVictory) {
			if (pressStart) {
				ResetGame(player, bullets, ebullets, boss, groundY);
				introTimer = kIntroFrames;
				playFrames = 0;
				shake = 0;
				scene = kPlay;
			}
		}
		else if (scene == kPlay) {

			// ----- プレイヤー操作 -----
			player.vx = 0.0f;
			if (keys[DIK_A] || keys[DIK_LEFT]) {
				player.vx = -kMoveSpeed;
				player.dir = -1;
			}
			if (keys[DIK_D] || keys[DIK_RIGHT]) {
				player.vx = kMoveSpeed;
				player.dir = 1;
			}

			bool jumpTrigger = (preKeys[DIK_SPACE] == 0 && keys[DIK_SPACE] != 0) || (preKeys[DIK_W] == 0 && keys[DIK_W] != 0) ||
				(preKeys[DIK_UP] == 0 && keys[DIK_UP] != 0);
			// クールダウン類
			if (player.dashCooldown > 0) player.dashCooldown--;
			if (player.parryCooldown > 0) player.parryCooldown--;
			if (player.parryTimer > 0) player.parryTimer--;
			if (player.parryFlash > 0) player.parryFlash--;

			// ジャンプ(地上で1回 + 空中で1回 = 二段ジャンプ)
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

			// ダッシュ (L / LSHIFT / C) 空中でも可、空中では着地までに1回
			bool dashTrigger = (preKeys[DIK_L] == 0 && keys[DIK_L] != 0) || (preKeys[DIK_LSHIFT] == 0 && keys[DIK_LSHIFT] != 0) ||
				(preKeys[DIK_C] == 0 && keys[DIK_C] != 0);
			if (dashTrigger && player.dashTimer == 0 && player.dashCooldown == 0 && player.parryTimer == 0 && (player.onGround || player.airDashLeft > 0)) {
				CancelAttack(player);
				player.dashTimer = 10;
				player.dashCooldown = 35;
				int d = player.dir;
				if (keys[DIK_A] || keys[DIK_LEFT]) d = -1;
				if (keys[DIK_D] || keys[DIK_RIGHT]) d = 1;
				player.dashDir = d;
				player.dir = d;
				if (!player.onGround) player.airDashLeft--;
			}

			// パリィ (I / X)
			bool parryTrigger = (preKeys[DIK_I] == 0 && keys[DIK_I] != 0) || (preKeys[DIK_X] == 0 && keys[DIK_X] != 0);
			if (parryTrigger && player.parryCooldown == 0 && player.dashTimer == 0) {
				CancelAttack(player);
				player.parryTimer = 12;
				player.parryCooldown = 45;
			}

			// 近接攻撃 (U / V)
			bool atkTrigger = (preKeys[DIK_U] == 0 && keys[DIK_U] != 0) || (preKeys[DIK_V] == 0 && keys[DIK_V] != 0);
			if (atkTrigger && player.dashTimer == 0 && player.parryTimer == 0) {
				// 向き: 方向キー優先、無ければボスの方を向く
				int adir = player.dir;
				if (keys[DIK_A] || keys[DIK_LEFT]) {
					adir = -1;
				}
				else if (keys[DIK_D] || keys[DIK_RIGHT]) {
					adir = 1;
				}
				else {
					adir = (boss.x + boss.w * 0.5f > player.x + player.w * 0.5f) ? 1 : -1;
				}

				// 空中コンボの続き(当たっていれば、振り終わり際か猶予中に入力で次の段へ)
				bool canNext = player.atkAir && player.atkHit && player.atkStep > 0 && player.atkStep < 4 && (player.atkTimer <= 6 || player.comboWindow > 0);
				if (!player.onGround && canNext) {
					StartAttack(player, true, player.atkStep + 1, adir);
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

			// 攻撃状態の進行
			if (player.atkTimer > 0) {
				player.atkTimer--;
				if (player.atkTimer == 0) {
					if (player.atkAir && player.atkHit && player.atkStep < 4) {
						player.comboWindow = 22; // 次の入力待ち(空中に留まる)
					}
					else {
						player.atkStep = 0; // 空振り・最終段・地上攻撃はここで終了
					}
				}
			}
			else if (player.comboWindow > 0) {
				player.comboWindow--;
				if (player.comboWindow == 0) player.atkStep = 0; // コンボ終了
			}

			// 弾の発射 (J / Z / K)
			if (player.shotCooldown > 0) player.shotCooldown--;
			if ((keys[DIK_J] || keys[DIK_Z] || keys[DIK_K]) && player.shotCooldown == 0) {
				for (int i = 0; i < kMaxBullets; i++) {
					if (!bullets[i].active) {
						bullets[i].active = true;
						bullets[i].vx = 14.0f * player.dir;
						bullets[i].x = player.x + player.w * 0.5f - 8.0f;
						bullets[i].y = player.y + 18.0f;
						player.shotCooldown = 10;
						break;
					}
				}
			}

			// ----- プレイヤー移動 -----
			// 近接攻撃中の移動
			//  地上: 操作不能で、振り始めに前へ踏み込む
			//  空中: 横移動は鈍り、コンボ中は重力を受けず空中に留まる
			bool hover = false;
			if (player.atkTimer > 0) {
				if (!player.atkAir) {
					player.vx = (player.atkTimer > 10) ? player.dir * 5.0f : 0.0f;
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
				if (player.atkAir) CancelAttack(player); // 空中コンボは着地で終了
			}
			// 足場から歩いて落ちた場合は1回目のジャンプを消費済みにする
			if (!player.onGround && player.jumpCount == 0) player.jumpCount = 1;

			if (player.invincible > 0) player.invincible--;
			if (boss.flash > 0) boss.flash--;
			if (shake > 0) shake--;
			playFrames++;

			// ----- プレイヤーの弾 -----
			for (int i = 0; i < kMaxBullets; i++) {
				if (!bullets[i].active) continue;
				bullets[i].x += bullets[i].vx;
				if (bullets[i].x < -20.0f || bullets[i].x > kScreenW + 20.0f) {
					bullets[i].active = false;
					continue;
				}
				if (introTimer == 0 && HitBox(bullets[i].x, bullets[i].y, 16.0f, 8.0f, boss.x, boss.y, boss.w, boss.h)) {
					bullets[i].active = false;
					boss.hp -= 1;
					boss.flash = 4;
				}
			}

			// ----- 近接攻撃の当たり判定 -----
			if (player.atkTimer > 0 && player.atkTimer <= player.atkHi && player.atkTimer >= player.atkLo) {
				float ax, ay, aw, ah;
				MeleeBox(player, &ax, &ay, &aw, &ah);

				// ボスへのヒット(1振りにつき1回)
				if (introTimer == 0 && !player.atkHit && HitBox(ax, ay, aw, ah, boss.x, boss.y, boss.w, boss.h)) {
					bool finisher = player.atkAir && player.atkStep == 4;
					player.atkHit = true;
					boss.hp -= player.atkDmg;
					boss.flash = 6;
					shake = finisher ? 14 : 6;
					if (player.invincible < 15) player.invincible = 15; // ヒット確認中の安全確保
					// ノックバック(締めと地上攻撃は大きめ)
					float knock = finisher ? 50.0f : (!player.atkAir ? 20.0f : 6.0f);
					boss.x = Clamp(boss.x + player.dir * knock, 0.0f, kScreenW - boss.w);
				}

				// 小さな弾は斬り落とせる(衝撃波と跳ね返し弾は対象外)
				for (int i = 0; i < kMaxEBullets; i++) {
					EBullet& eb = ebullets[i];
					if (!eb.active || eb.wave || eb.reflected) continue;
					if (HitBox(ax, ay, aw, ah, eb.x - eb.r, eb.y - eb.r, eb.r * 2.0f, eb.r * 2.0f)) {
						eb.active = false;
					}
				}
			}

			// ----- ボスAI -----
			if (introTimer > 0) {
				introTimer--;
			}
			else {
				int phase = BossPhase(boss);
				float bcx = boss.x + boss.w * 0.5f;
				float bcy = boss.y + boss.h * 0.5f;
				float pcx = player.x + player.w * 0.5f;
				float pcy = player.y + player.h * 0.5f;
				float aim = atan2f(pcy - bcy, pcx - bcx);

				switch (boss.state) {
				case kBWalk: {
					boss.dir = (pcx > bcx) ? 1 : -1;
					boss.x += boss.dir * (1.2f + 0.5f * phase);
					boss.timer--;
					if (boss.timer <= 0) {
						int choices = phase + 1; // P1:2種 P2:3種 P3:4種
						int pick = rand() % choices;
						if (pick == 0) {
							boss.state = kBShoot;
							boss.timer = 60;
						}
						else if (pick == 1) {
							boss.state = kBJumpWind;
							boss.timer = 25;
						}
						else if (pick == 2) {
							boss.state = kBChargeWind;
							boss.timer = 40;
							boss.dir = (pcx > bcx) ? 1 : -1;
						}
						else {
							boss.state = kBBurst;
							boss.timer = 70;
						}
					}
					break;
				}
				case kBShoot: {
					boss.dir = (pcx > bcx) ? 1 : -1;
					boss.timer--;
					if (boss.timer == 45 || boss.timer == 30 || boss.timer == 15) {
						int n = (phase == 1) ? 1 : (phase == 2 ? 3 : 5);
						for (int i = 0; i < n; i++) {
							float a = aim + (i - (n - 1) * 0.5f) * 0.25f;
							FireAngle(ebullets, bcx, bcy - 20.0f, a, 6.0f + phase, 10.0f);
						}
					}
					if (boss.timer <= 0) {
						boss.state = kBWalk;
						boss.timer = 80 - phase * 12;
					}
					break;
				}
				case kBJumpWind: {
					boss.timer--;
					if (boss.timer <= 0) {
						boss.vy = -23.0f;
						boss.vx = Clamp((pcx - bcx) / 35.0f, -9.0f, 9.0f);
						boss.state = kBAir;
					}
					break;
				}
				case kBAir: {
					boss.vy += kGravity;
					boss.x += boss.vx;
					boss.y += boss.vy;
					boss.x = Clamp(boss.x, 0.0f, kScreenW - boss.w);
					if (boss.y + boss.h >= groundY) {
						boss.y = groundY - boss.h;
						boss.vy = 0.0f;
						shake = 16;
						float ws = 6.0f + phase * 1.5f;
						FireEBullet(ebullets, boss.x - 10.0f, groundY - 14.0f, -ws, 0.0f, 14.0f, true);
						FireEBullet(ebullets, boss.x + boss.w + 10.0f, groundY - 14.0f, ws, 0.0f, 14.0f, true);
						boss.state = kBWalk;
						boss.timer = 80 - phase * 12;
					}
					break;
				}
				case kBChargeWind: {
					boss.timer--;
					if (boss.timer <= 0) {
						boss.state = kBCharge;
						boss.timer = 60;
					}
					break;
				}
				case kBCharge: {
					boss.x += boss.dir * 14.0f;
					boss.timer--;
					bool wall = false;
					if (boss.x <= 0.0f) {
						boss.x = 0.0f;
						wall = true;
					}
					if (boss.x + boss.w >= kScreenW) {
						boss.x = kScreenW - boss.w;
						wall = true;
					}
					if (wall) {
						shake = 14;
						// 壁にぶつかると天井から弾が降ってくる
						for (int i = 0; i < 5; i++) {
							FireEBullet(ebullets, 200.0f + static_cast<float>(rand() % 880), -10.0f, 0.0f, 5.0f, 12.0f, false);
						}
					}
					if (wall || boss.timer <= 0) {
						boss.state = kBWalk;
						boss.timer = 90 - phase * 12;
					}
					break;
				}
				case kBBurst: {
					boss.timer--;
					if (boss.timer == 50 || boss.timer == 25) {
						float off = (boss.timer == 50) ? 0.0f : kPi / 14.0f;
						for (int i = 0; i < 14; i++) {
							FireAngle(ebullets, bcx, bcy, off + i * (2.0f * kPi / 14.0f), 5.0f, 10.0f);
						}
					}
					if (boss.timer <= 0) {
						boss.state = kBWalk;
						boss.timer = 80 - phase * 12;
					}
					break;
				}
				case kBStun: {
					boss.timer--;
					if (boss.timer <= 0) {
						boss.state = kBWalk;
						boss.timer = 60;
					}
					break;
				}
				}
				boss.x = Clamp(boss.x, 0.0f, kScreenW - boss.w);

				// ボスの弾の更新と当たり判定
				for (int i = 0; i < kMaxEBullets; i++) {
					EBullet& b = ebullets[i];
					if (!b.active) continue;
					b.x += b.vx;
					b.y += b.vy;
					if (b.x < -60.0f || b.x > kScreenW + 60.0f || b.y < -60.0f || b.y > kScreenH + 60.0f) {
						b.active = false;
						continue;
					}
					// パリィで跳ね返された弾はボスにダメージを与える
					if (b.reflected) {
						if (HitBox(b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f, boss.x, boss.y, boss.w, boss.h)) {
							b.active = false;
							boss.hp -= 4;
							boss.flash = 10;
							shake = 8;
						}
						continue;
					}
					if (player.parryTimer > 0 && HitBox(player.x - 24.0f, player.y - 24.0f, player.w + 48.0f, player.h + 48.0f, b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f)) {
						// パリィ成功
						if (b.wave) {
							b.active = false; // 衝撃波は消せる
						}
						else {
							b.reflected = true; // 弾はボスへ跳ね返す
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

				// ボス本体との当たり判定
				if (HitBox(player.x, player.y, player.w, player.h, boss.x, boss.y, boss.w, boss.h)) {

					if (player.parryTimer > 0) {
						// ボス本体へのパリィ
						if (boss.state != kBAir) {
							boss.state = kBStun;
							boss.timer = 70;
							boss.flash = 8;
						}

						player.invincible = 30;
						player.parryCooldown = 0;
						player.parryFlash = 10;
						player.vy = -9.0f;
						player.onGround = false;
						shake = 10;
					}
					else if (player.invincible == 0 && player.dashTimer == 0) {
						// ボスに接触するとダメージ
						HurtPlayer(player);
					}
				}

				// ----- 勝敗判定 -----
				if (boss.hp <= 0) {
					boss.hp = 0;
					for (int i = 0; i < kMaxEBullets; i++) ebullets[i].active = false;
					if (bestFrames == 0 || playFrames < bestFrames) bestFrames = playFrames;
					scene = kVictory;
				}
				else if (player.hp <= 0) {
					player.hp = 0;
					scene = kGameOver;
				}
			}

			///
			/// ↑更新処理ここまで
			///

			///
			/// ↓描画処理ここから
			///

			// 画面シェイク
			if (shake > 0) {
				gShakeX = (rand() % 11) - 5;
				gShakeY = (rand() % 11) - 5;
			}
			else {
				gShakeX = gShakeY = 0;
			}

			// 背景
			Novice::DrawBox(0, 0, kScreenW, kScreenH, 0.0f, 0x1B1F3BFF, kFillModeSolid);

			// 足場
			for (int i = 0; i < kPlatformNum; i++) {
				float h = (i == 0) ? (kScreenH - platforms[i].y) : 20.0f;
				Rect(platforms[i].x, platforms[i].y, platforms[i].w, h, (i == 0) ? 0x3E6B48FF : 0x8B6B4AFF);
			}

			if (scene != kTitle) {
				int phase = BossPhase(boss);

				// 突進の予告ライン
				if (boss.state == kBChargeWind && (boss.timer / 4) % 2 == 0) {
					float lx = (boss.dir == 1) ? boss.x + boss.w : 0.0f;
					float lw = (boss.dir == 1) ? kScreenW - lx : boss.x;
					Rect(lx, groundY - 60.0f, lw, 60.0f, 0xFF000040);
				}

				// ボス
				unsigned int bodyColor = (phase == 1) ? 0x8844CCFF : (phase == 2 ? 0xE08830FF : 0xE03030FF);
				if (boss.flash > 0) bodyColor = 0xFFFFFFFF;
				if (boss.state == kBStun && boss.flash == 0 && (boss.timer / 6) % 2 == 0) bodyColor = 0x66CCFFFF;
				float drawH = boss.h;
				float drawY = boss.y;
				if (boss.state == kBJumpWind) { // 溜めで縮む
					drawH = boss.h - 30.0f;
					drawY = boss.y + 30.0f;
				}
				bool warnBlink = (boss.state == kBChargeWind || boss.state == kBBurst) && (boss.timer / 4) % 2 == 0;
				if (warnBlink && boss.flash == 0) bodyColor = 0xFFFF66FF;
				Rect(boss.x, drawY, boss.w, drawH, bodyColor);
				// 目
				float eyeY = drawY + 40.0f;
				float eyeX = (boss.dir == 1) ? boss.x + boss.w - 70.0f : boss.x + 20.0f;
				Rect(eyeX, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
				Rect(eyeX + 30.0f, eyeY, 20.0f, 20.0f, 0xFFFFFFFF);
				Rect(eyeX + 6.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);
				Rect(eyeX + 36.0f, eyeY + 6.0f, 8.0f, 8.0f, 0x000000FF);
				// ツノ
				Rect(boss.x + 10.0f, drawY - 20.0f, 20.0f, 20.0f, 0xCCCCCCFF);
				Rect(boss.x + boss.w - 30.0f, drawY - 20.0f, 20.0f, 20.0f, 0xCCCCCCFF);

				// ボスの弾
				for (int i = 0; i < kMaxEBullets; i++) {
					const EBullet& b = ebullets[i];
					if (!b.active) continue;
					if (b.reflected) {
						Circle(b.x, b.y, b.r, 0x66FFFFFF);
						Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
					}
					else if (b.wave) {
						Rect(b.x - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f, 0xFF8800FF);
					}
					else {
						Circle(b.x, b.y, b.r, 0xFF4488FF);
						Circle(b.x, b.y, b.r * 0.5f, 0xFFFFFFFF);
					}
				}

				// プレイヤーの弾
				for (int i = 0; i < kMaxBullets; i++) {
					if (!bullets[i].active) continue;
					Rect(bullets[i].x, bullets[i].y, 16.0f, 8.0f, 0xFFE94AFF);
				}

				// プレイヤー(無敵中は点滅)
				if (scene == kPlay) {
					// ダッシュの残像
					if (player.dashTimer > 0) {
						for (int i = 1; i <= 3; i++) {
							Rect(player.x - player.dashDir * i * 18.0f, player.y, player.w, player.h, 0x4AA8FF40);
						}
					}
					// 本体(無敵中は点滅、ダッシュ中は半透明)
					if (!(player.invincible > 0 && (player.invincible / 4) % 2 == 0)) {
						Rect(player.x, player.y, player.w, player.h, (player.dashTimer > 0) ? 0x4AA8FF99 : 0x4AA8FFFF);
						float eyeXp = (player.dir == 1) ? player.x + 20.0f : player.x + 4.0f;
						Rect(eyeXp, player.y + 10.0f, 8.0f, 8.0f, 0xFFFFFFFF);
					}
					// 近接攻撃のエフェクト(判定が出ている間)
					if (player.atkTimer > 0 && player.atkTimer <= player.atkHi && player.atkTimer >= player.atkLo) {
						float sx, sy, sw, sh;
						MeleeBox(player, &sx, &sy, &sw, &sh);
						bool fin = player.atkAir && player.atkStep == 4;
						Rect(sx, sy, sw, sh, fin ? 0xFFAA44A0 : (player.atkAir ? 0xFFFFFFA0 : 0xFFE060A0));
						// 刃の芯
						float cy = sy + sh * 0.5f;
						Rect(sx, cy - 3.0f, sw, 6.0f, 0xFFFFFFFF);
					}
					// 空中コンボ表示
					if (player.atkAir && player.atkStep > 0 && (player.atkTimer > 0 || player.comboWindow > 0)) {
						GetFont().Printf(static_cast<int>(player.x) - 10, static_cast<int>(player.y) - 30, "AIR %d/4", player.atkStep);
					}
					// パリィのシールド
					float pcx2 = player.x + player.w * 0.5f;
					float pcy2 = player.y + player.h * 0.5f;
					if (player.parryTimer > 0) {
						Circle(pcx2, pcy2, 46.0f, 0x66FFFF80);
					}
					if (player.parryFlash > 0) {
						Circle(pcx2, pcy2, 50.0f + (10 - player.parryFlash) * 5.0f, 0xFFFFFF60);
					}
				}

				// ---- HUD ----
				// (フォントが24pxなので、元の ScreenPrintf より行の間隔を広げてある)
				GetFont().Printf(20, 10, "PLAYER");
				for (int i = 0; i < kPlayerMaxHp; i++) {
					Novice::DrawBox(20 + i * 26, 42, 20, 14, 0.0f, (i < player.hp) ? 0xFF5577FF : 0x55555588, kFillModeSolid);
				}
				GetFont().Printf(20, 62, "TIME: %.1f", playFrames / 60.0f);

				// ダッシュ/パリィの使用可能表示
				bool dashReady = player.dashCooldown == 0 && (player.onGround || player.airDashLeft > 0);
				bool parryReady = player.parryCooldown == 0;
				Novice::DrawBox(20, 96, 70, 26, 0.0f, dashReady ? 0x44DD66FF : 0x55555588, kFillModeSolid);
				GetFont().Printf(26, 96, "DASH");
				Novice::DrawBox(100, 96, 80, 26, 0.0f, parryReady ? 0x44CCFFFF : 0x55555588, kFillModeSolid);
				GetFont().Printf(106, 96, "PARRY");

				// ボスHPバー
				GetFont().Printf(440, 6, "BOSS  PHASE %d", phase);
				Novice::DrawBox(440, 36, 400, 18, 0.0f, 0x444444FF, kFillModeSolid);
				Novice::DrawBox(440, 36, 400 * boss.hp / kBossMaxHp, 18, 0.0f, 0xE03030FF, kFillModeSolid);
				Novice::DrawBox(440, 36, 400, 18, 0.0f, 0xFFFFFFFF, kFillModeWireFrame);

				// 登場演出
				if (scene == kPlay && introTimer > 0) {
					if ((introTimer / 10) % 2 == 0) {
						GetFont().Printf(570, 250, "!! WARNING !!");
					}
					GetFont().Printf(560, 284, "A BOSS APPEARS");
				}
			}

			if (scene == kTitle) {
				GetFont().Printf(526, 30, "=== BOSS BATTLE ===");
				GetFont().Printf(420, 80, "Move : A / D  or  Left / Right");
				GetFont().Printf(420, 108, "Jump : SPACE / W / Up");
				GetFont().Printf(420, 136, "Shot : J / Z / K  (1 damage)");
				GetFont().Printf(420, 164, "Avoid touching the boss!");
				GetFont().Printf(420, 208, "Double Jump : press jump again in the air");
				GetFont().Printf(420, 236, "Dash  : L / LSHIFT / C  (air OK, 1 per air, i-frames)");
				GetFont().Printf(420, 264, "Parry : I / X  (reflect bullets = 4 damage!)");
				GetFont().Printf(420, 292, "        Parry the boss's body = stun");
				GetFont().Printf(420, 320, "        Ground shockwaves are destroyed.");
				GetFont().Printf(420, 364, "Melee : U / V");
				GetFont().Printf(420, 392, "  Ground : lunging heavy slash (3 dmg)");
				GetFont().Printf(420, 420, "  Air    : hit to chain up to 4, you float while combo-ing");
				GetFont().Printf(420, 448, "           2 / 2 / 2 / 4 dmg, landing ends the combo");
				if ((frame / 30) % 2 == 0) {
					GetFont().Printf(520, 590, "PRESS SPACE TO START");
				}
				if (bestFrames > 0) {
					GetFont().Printf(520, 630, "BEST TIME: %.1f sec", bestFrames / 60.0f);
				}
			}
			else if (scene == kGameOver) {
				Novice::DrawBox(440, 240, 400, 200, 0.0f, 0x000000CC, kFillModeSolid);
				GetFont().Printf(580, 270, "YOU DIED");
				GetFont().Printf(540, 310, "BOSS HP LEFT: %d / %d", boss.hp, kBossMaxHp);
				GetFont().Printf(520, 390, "PRESS SPACE TO RETRY");
			}
			else if (scene == kVictory) {
				Novice::DrawBox(440, 240, 400, 200, 0.0f, 0x000000CC, kFillModeSolid);
				GetFont().Printf(570, 270, "BOSS DEFEATED!");
				GetFont().Printf(560, 310, "TIME: %.1f sec", playFrames / 60.0f);
				GetFont().Printf(550, 344, "BEST: %.1f sec", bestFrames / 60.0f);
				GetFont().Printf(520, 390, "PRESS SPACE TO PLAY AGAIN");
			}

			///
			/// ↑描画処理ここまで
			///

			// フレームの終了
			Novice::EndFrame();

			// ESCキーが押されたらループを抜ける
			if (preKeys[DIK_ESCAPE] == 0 && keys[DIK_ESCAPE] != 0) {
				break;
			}
		}
	}
	// ライブラリの終了
	Novice::Finalize();
	return 0;
}