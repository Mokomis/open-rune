
#include "Precomp.h"
#include "UFont.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "surrealwidgets/core/resourcedata.h"
// Use the font library method, not the Win32 GDI macro.
#ifdef GetTextMetrics
#undef GetTextMetrics
#endif
#include "../../../../SurrealWidgets/src/core/truetypefont.h"
#include <algorithm>
#include <cmath>
#include "Textures/UTexture.h"

void UFont::Load(ObjectStream* stream)
{
	// Note: 63 package and older inherited from UTexture. Newer versions inherit from UObject
	// We must keep the old relationship intact to load the older games.
	// If the game is newer then this is not a valid texture and any properties on UTexture do not exist.

	if (stream->GetVersion() <= 63)
	{
		UTexture::Load(stream);

		pages.resize(1);
		FontPage& page = pages.front();
		page.Texture = this;
		page.Characters.resize(stream->ReadIndex());
		for (FontCharacter& character : page.Characters)
		{
			character.StartU = stream->ReadInt32();
			character.StartV = stream->ReadInt32();
			character.USize = stream->ReadInt32();
			character.VSize = stream->ReadInt32();
		}
	}
	else
	{
		UObject::Load(stream);

		pages.resize(stream->ReadIndex());
		for (FontPage& page : pages)
		{
			page.Texture = stream->ReadObject<UTexture>();
			page.Characters.resize(stream->ReadIndex());
			for (FontCharacter& character : page.Characters)
			{
				character.StartU = stream->ReadInt32();
				character.StartV = stream->ReadInt32();
				character.USize = stream->ReadInt32();
				character.VSize = stream->ReadInt32();
			}
		}

		charactersPerPage = stream->ReadUInt32();
	}
}

void UFont::Save(PackageStreamWriter* stream)
{
	if (stream->GetVersion() <= 63)
	{
		UTexture::Save(stream);

		FontPage& page = pages.front();
		stream->WriteIndex((int)page.Characters.size());
		for (FontCharacter& character : page.Characters)
		{
			stream->WriteInt32(character.StartU);
			stream->WriteInt32(character.StartV);
			stream->WriteInt32(character.USize);
			stream->WriteInt32(character.VSize);
		}
	}
	else
	{
		UObject::Save(stream);

		stream->WriteIndex((int)pages.size());
		for (FontPage& page : pages)
		{
			stream->WriteObject(page.Texture);
			stream->WriteIndex((int)page.Characters.size());
			for (FontCharacter& character : page.Characters)
			{
				stream->WriteInt32(character.StartU);
				stream->WriteInt32(character.StartV);
				stream->WriteInt32(character.USize);
				stream->WriteInt32(character.VSize);
			}
		}

		stream->WriteUInt32(charactersPerPage);
	}
}

FontGlyph UFont::GetDisplayGlyph(uint32_t c) const
{
	if (engine && engine->LaunchInfo.IsRune())
	{
		if (!runeFontAttempted) BuildRuneFont();
		if (!runeGlyphs.empty())
		{
			auto it = runeGlyphs.find(c);
			return it != runeGlyphs.end() ? it->second : runeGlyphs.at('?');
		}
	}
	return GetGlyph(c);
}

FontGlyph UFont::GetGlyph(uint32_t c) const
{
	FontGlyph glyph = FindGlyph(c);
	if (glyph.USize == 0)
	{
		if (c >= 'a' && c <= 'z')
		{
			c += 'A' - 'a';
			glyph = FindGlyph(c);
		}

		if (glyph.USize == 0)
			glyph = FindGlyph(32);
	}
	return glyph;
}

FontGlyph UFont::FindGlyph(uint32_t c) const
{
	size_t index = c;
	for (auto& page : pages)
	{
		if (index < page.Characters.size())
		{
			return { page.Texture, page.Characters[index] };
		}
		index -= page.Characters.size();
	}
	return {};
}

// Runtime replacement only: serialized Rune fonts and the original assets stay intact.
void UFont::BuildRuneFont() const
{
    runeFontAttempted = true;
    try
    {
        TrueTypeFont font(TTFDataBuffer::create(ResourceData::ReadAllBytes("noto/notosans-regular.ttf")));
        const int scale = 3, atlasSize = 2048;
        const int height = std::clamp(FindGlyph('M').VSize, 22, 48);
        auto metrics = font.GetTextMetrics(1.0);
        double em = (height * scale - 2.0) / (metrics.ascent + metrics.descent);
        int baseline = int(std::ceil(font.GetTextMetrics(em).ascent));
        UnrealMipmap mip;
        mip.Width = mip.Height = atlasSize;
        mip.Data.resize(atlasSize * atlasSize * 4);
        std::map<uint32_t, FontGlyph> glyphs;
        std::vector<uint32_t> codepoints;
        for (uint32_t c = 32; c < 256; ++c) codepoints.push_back(c);
        for (uint32_t c : {0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201cu, 0x201du, 0x2026u, 0x2190u, 0x2191u, 0x2192u, 0x2193u}) codepoints.push_back(c);
        int x = 2, y = 2;
        for (auto c : codepoints)
        {
            auto index = font.GetGlyphIndex(c);
            if (!index) continue;
            auto raster = font.LoadGlyph(index, em);
            int bearing = int(std::floor(raster.leftSideBearing / 3.0));
            int rasterWidth = (raster.width + 2) / 3;
            int advance = int(std::ceil(font.GetAdvanceWidth(index, em)));
            int width = (std::max(advance, bearing + rasterWidth) + std::max(0, -bearing) + scale - 1) / scale;
            width = std::max(1, width);
            if (x + width * scale + 2 > atlasSize) { x = 2; y += height * scale + 4; }
            if (y + height * scale + 2 > atlasSize) throw std::runtime_error("Rune font atlas overflow");
            FontGlyph glyph;
            glyph.StartU = x; glyph.StartV = y; glyph.USize = width; glyph.VSize = height; glyph.TexelScale = scale;
            glyphs[c] = glyph;
            for (int ry = 0; ry < raster.height; ++ry)
                for (int rx = 0; rx < rasterWidth; ++rx)
                {
                    int dx = std::max(0, bearing) + rx, dy = baseline + raster.yOffset + ry;
                    if (dx >= width * scale || dy < 0 || dy >= height * scale) continue;
                    unsigned coverage = 0;
                    for (int sub = 0; sub < 3; ++sub)
                        if (rx * 3 + sub < raster.width) coverage += raster.grayscale[ry * raster.width + rx * 3 + sub];
                    auto offset = ((y + dy) * atlasSize + x + dx) * 4;
                    // Highlighted uses premultiplied alpha, like the existing menu renderer.
                    for (int channel = 0; channel < 4; ++channel) mip.Data[offset + channel] = uint8_t(coverage / 3);
                }
            x += width * scale + 4;
        }
        if (!glyphs.count('?')) return;
        auto* texture = UObject::Cast<UTexture>(engine->packages->GetTransientPackage()->NewObject("RuneReadableFont", engine->packages->GetPackage("Engine")->GetClass("Texture"), ObjectFlags::Transient));
        int atlasHeight = 1, heightBits = 0;
        while (atlasHeight < y + height * scale + 2) { atlasHeight *= 2; ++heightBits; }
        mip.Height = atlasHeight;
        mip.Data.resize(atlasSize * atlasHeight * 4);
        texture->USize() = atlasSize; texture->VSize() = atlasHeight;
        texture->UBits() = 11; texture->VBits() = heightBits;
        texture->UsedFormat = TextureFormat::BGRA8;
        texture->UsedMipmaps.push_back(std::move(mip));
        for (auto& entry : glyphs) entry.second.Texture = texture;
        runeAtlas.set(texture);
        runeGlyphs = std::move(glyphs);
        LogMessage("RUNE_READABLE_FONT name=" + Name.ToString() + " height=" + std::to_string(height) + " glyphs=" + std::to_string(runeGlyphs.size()));
    }
    catch (const std::exception& error)
    {
        LogMessage(std::string("RUNE_READABLE_FONT_FALLBACK ") + error.what());
        // Original font remains available if packaged font resources cannot be read.
    }
}
