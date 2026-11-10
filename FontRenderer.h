#pragma once

#include <Novice.h>
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

class FontRenderer {
public:
	// Novice::Initialize の後に、1回だけ呼ぶこと
	// faceName は ttf ファイル名ではなく「フォントのファミリー名」
	// extraChars には、使う漢字などを書く(ASCII・ひらがな・カタカナは自動で入る)
	bool Create(const wchar_t* ttfPath, const wchar_t* faceName, int pixelSize,
		const std::wstring& extraChars = L"") {
		AddFontResourceExW(ttfPath, FR_PRIVATE, 0);

		// 使う文字の集合: ASCII + ひらがな + カタカナ + 句読点 + 追加分
		std::set<wchar_t> chars;
		for (wchar_t c = 0x20; c <= 0x7E; ++c) chars.insert(c);
		for (wchar_t c = 0x3041; c <= 0x3096; ++c) chars.insert(c);
		for (wchar_t c = 0x30A1; c <= 0x30FC; ++c) chars.insert(c);
		for (wchar_t c : std::wstring(L"、。「」・！？")) chars.insert(c);
		for (wchar_t c : extraChars) chars.insert(c);

		HDC screen = GetDC(nullptr);
		HDC dc = CreateCompatibleDC(screen);
		HFONT font = CreateFontW(-pixelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
		HGDIOBJ oldFont = SelectObject(dc, font);

		TEXTMETRICW tm;
		GetTextMetricsW(dc, &tm);
		cellH_ = tm.tmHeight + 2;

		// 1) 各文字の幅を測り、幅1024の棚詰めでレイアウトを決める
		const int kAtlasW = 1024;
		int x = 0, y = 0;
		for (wchar_t c : chars) {
			SIZE sz;
			GetTextExtentPoint32W(dc, &c, 1, &sz);
			int w = sz.cx + 2;
			if (x + w > kAtlasW) { x = 0; y += cellH_; }
			glyphs_[c] = { x, y, w };
			x += w;
		}
		atlasW_ = kAtlasW;
		atlasH_ = y + cellH_;

		// 2) 24bit DIB に白文字を描く(背景は黒)
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = atlasW_;
		bi.bmiHeader.biHeight = atlasH_; // 正 = 下から上
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 24;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		HGDIOBJ oldBmp = SelectObject(dc, bmp);

		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 255, 255));
		for (auto& [c, g] : glyphs_) {
			TextOutW(dc, g.x + 1, g.y + 1, &c, 1);
		}
		GdiFlush();

		// 3) BMP として書き出して Novice に読ませる
		// このエンジンは、同階層のファイルを読むとき、名前の先頭に "./" が必要
		static int counter = 0;
		std::string path = "./fontatlas_" + std::to_string(counter++) + ".bmp";

		int stride = ((atlasW_ * 3 + 3) & ~3);
		DWORD imageSize = stride * atlasH_;
		BITMAPFILEHEADER bf = {};
		bf.bfType = 0x4D42; // 'BM'
		bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
		bf.bfSize = bf.bfOffBits + imageSize;
		std::ofstream ofs(path, std::ios::binary);
		ofs.write(reinterpret_cast<char*>(&bf), sizeof(bf));
		ofs.write(reinterpret_cast<char*>(&bi.bmiHeader), sizeof(BITMAPINFOHEADER));
		ofs.write(static_cast<char*>(bits), imageSize);
		ofs.close();

		texture_ = Novice::LoadTexture(path.c_str());

		// 後片付け
		SelectObject(dc, oldBmp);
		SelectObject(dc, oldFont);
		DeleteObject(bmp);
		DeleteObject(font);
		DeleteDC(dc);
		ReleaseDC(nullptr, screen);
		RemoveFontResourceExW(ttfPath, FR_PRIVATE, 0);
		return true;
	}

	// 文字列を描画する(text は UTF-8 として扱う。ソースが Shift-JIS なら CP_ACP を渡す)
	void Draw(int x, int y, const std::string& text, unsigned int color = 0xFFFFFFFF,
		float scale = 1.0f, UINT codePage = CP_UTF8) {
		std::wstring w = ToWide(text, codePage);
		Novice::SetBlendMode(kBlendModeAdd); // 黒背景を透明扱いにする
		float cx = static_cast<float>(x);
		for (wchar_t c : w) {
			auto it = glyphs_.find(c);
			if (it == glyphs_.end()) { // 未登録文字はスキップ(幅だけ空ける)
				cx += cellH_ * 0.5f * scale;
				continue;
			}
			const Glyph& g = it->second;
			// DrawSpriteRect の scale は「切り出し幅 / 画像全体の幅」で指定する
			Novice::DrawSpriteRect(static_cast<int>(cx), y, g.x, g.y, g.w, cellH_, texture_,
				static_cast<float>(g.w) / atlasW_ * scale,
				static_cast<float>(cellH_) / atlasH_ * scale, 0.0f, color);
			cx += (g.w - 2) * scale;
		}
		Novice::SetBlendMode(kBlendModeNormal);
	}

	// Novice::ScreenPrintf と同じ書式で使える(文字色は白)
	void Printf(int x, int y, const char* format, ...) {
		char buf[512];
		va_list args;
		va_start(args, format);
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		Draw(x, y, buf);
	}

private:
	struct Glyph { int x, y, w; };

	static std::wstring ToWide(const std::string& s, UINT cp) {
		if (s.empty()) return L"";
		int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), w.data(), n);
		return w;
	}

	std::map<wchar_t, Glyph> glyphs_;
	int texture_ = -1;
	int atlasW_ = 0, atlasH_ = 0, cellH_ = 0;
};

// ゲーム全体で共有する FontRenderer を返す
inline FontRenderer& GetFont() {
	static FontRenderer font;
	return font;
}