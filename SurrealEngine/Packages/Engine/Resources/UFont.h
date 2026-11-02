#pragma once

#include "Textures/UTexture.h"
#include <map>

class UTexture;

class FontCharacter
{
public:
	int StartU = 0;
	int StartV = 0;
	int USize = 0;
	int VSize = 0;
};

class FontPage
{
public:
	UTexture* Texture = nullptr;
	Array<FontCharacter> Characters;
};

class FontGlyph
{
public:
	FontGlyph() = default;
	FontGlyph(UTexture* texture, const FontCharacter& c) : Texture(texture), StartU(c.StartU), StartV(c.StartV), USize(c.USize), VSize(c.VSize) {}

	UTexture* Texture = nullptr;
	int StartU = 0;
	int StartV = 0;
	int USize = 0;
	int VSize = 0;
	int TexelScale = 1;
};

class UFont : public UTexture
{
public:
	using UTexture::UTexture;

	void Load(ObjectStream* stream) override;
	void Save(PackageStreamWriter* stream) override;

	FontGlyph GetGlyph(uint32_t c) const;
	FontGlyph GetDisplayGlyph(uint32_t c) const;

private:
	FontGlyph FindGlyph(uint32_t c) const;
	void BuildRuneFont() const;
	mutable bool runeFontAttempted = false;
	mutable GCRoot<UTexture> runeAtlas;
	mutable std::map<uint32_t, FontGlyph> runeGlyphs;

	Array<FontPage> pages;
	int charactersPerPage;
};
