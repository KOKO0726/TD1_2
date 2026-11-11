#pragma once

#include <Novice.h>
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

#include "FontAtlasData.h"

// 文字画像(font_atlas.png)を切り出して文字を表示するクラス。
class FontRenderer {
public:
	// Novice::Initialize の後に、1回だけ呼ぶこと
	// font_atlas.png は、exe を実行するフォルダ(Novice の画像と同じ場所)に置く
	// このエンジンは、同階層のファイルを読むとき、名前の先頭に "./" が必要
	bool Create(const char* atlasPath = "./font_atlas.png") {
		texture_ = Novice::LoadTexture(atlasPath);

		// 文字コード -> 画像の何番目のマスか
		index_.clear();
		for (int i = 0; i < FontAtlas::kCount; ++i) {
			index_[static_cast<wchar_t>(FontAtlas::kCodes[i])] = i;
		}
		return true;
	}

	// 文字列を描画する(text は UTF-8 として扱う。ソースが Shift-JIS なら CP_ACP を渡す)
	// 文字の大きさは 24px。scale は 1.0f / 2.0f など整数倍にするとにじまない
	// "\n" で改行できる
	void Draw(int x, int y, const std::string& text, unsigned int color = 0xFFFFFFFF,
		float scale = 1.0f, UINT codePage = CP_UTF8) {
		std::wstring w = ToWide(text, codePage);
		const int pad = static_cast<int>(FontAtlas::kPad * scale);
		float cx = static_cast<float>(x);
		float cy = static_cast<float>(y);
		for (wchar_t c : w) {
			if (c == L'\n') { // 改行
				cx = static_cast<float>(x);
				cy += FontAtlas::kLineH * scale;
				continue;
			}
			auto it = index_.find(c);
			if (it == index_.end()) { // 画像に無い文字は、半角分だけ空ける
				cx += FontAtlas::kLineH * 0.5f * scale;
				continue;
			}
			const int i = it->second;
			const int srcX = (i % FontAtlas::kCols) * FontAtlas::kCellW;
			const int srcY = (i / FontAtlas::kCols) * FontAtlas::kCellH;
			// DrawSpriteRect の scale は「切り出し幅 / 画像全体の幅」で指定する
			Novice::DrawSpriteRect(
				static_cast<int>(cx) - pad, static_cast<int>(cy) - pad, srcX, srcY,
				FontAtlas::kCellW, FontAtlas::kCellH, texture_,
				static_cast<float>(FontAtlas::kCellW) / FontAtlas::kAtlasW * scale,
				static_cast<float>(FontAtlas::kCellH) / FontAtlas::kAtlasH * scale, 0.0f, color);
			cx += FontAtlas::kAdvance[i] * scale;
		}
	}

	void Printf(int x, int y, const char* format, ...) {
		char buf[512];
		va_list args;
		va_start(args, format);
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		Draw(x, y, buf);
	}

	// 文字列の横幅(px)。中央そろえなどに使う
	int Width(const std::string& text, float scale = 1.0f, UINT codePage = CP_UTF8) const {
		std::wstring w = ToWide(text, codePage);
		float width = 0.0f;
		for (wchar_t c : w) {
			auto it = index_.find(c);
			width += (it == index_.end()) ? FontAtlas::kLineH * 0.5f * scale
				: FontAtlas::kAdvance[it->second] * scale;
		}
		return static_cast<int>(width);
	}

private:
	static std::wstring ToWide(const std::string& s, UINT cp) {
		if (s.empty()) return L"";
		int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), w.data(), n);
		return w;
	}

	std::map<wchar_t, int> index_;
	int texture_ = -1;
};

// ゲーム全体で共有する FontRenderer を返す
inline FontRenderer& GetFont() {
	static FontRenderer font;
	return font;
}