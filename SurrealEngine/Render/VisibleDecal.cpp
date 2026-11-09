
#include "Precomp.h"
#include "VisibleDecal.h"
#include "VisibleFrame.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Actors/UDecal.h"
#include "Engine.h"

void VisibleDecal::DrawDecals(VisibleFrame* frame, BspNode* node)
{
	for (auto& leveldecal : node->Decals)
	{
		if (leveldecal.Decal->Texture())
		{
			engine->render->UpdateTexture(leveldecal.Decal->Texture());

			UTexture* texture = leveldecal.Decal->Texture()->GetAnimTexture();
			engine->render->UpdateTexture(texture);

			// Rune places opaque/masked artwork with WorldDecal as well as
			// modulated blood/shadows. Treating every decal as modulation
			// turns transparent palette borders into black rectangles.
			uint32_t flags = PF_Modulated;
			if (engine->LaunchInfo.IsRune())
			{
				flags = texture->PolyFlags();
				switch (leveldecal.Decal->Style())
				{
				case STY_Normal: break;
				case STY_Masked: flags |= PF_Masked; break;
				case STY_Translucent: flags |= PF_Translucent; break;
				default: flags |= PF_Modulated; break;
				}
				if (leveldecal.Decal->bNoSmooth()) flags |= PF_NoSmooth;
				if (flags & PF_Invisible) continue;
			}

			TextureInfo texinfo;
			texinfo.CacheID = (uint64_t)(ptrdiff_t)texture;
			texinfo.Texture = texture;
			texinfo.Format = texinfo.Texture->UsedFormat;
			texinfo.Mips = texinfo.Texture->UsedMipmaps.data();
			texinfo.NumMips = (int)texinfo.Texture->UsedMipmaps.size();
			texinfo.USize = texinfo.Texture->USize();
			texinfo.VSize = texinfo.Texture->VSize();
			if (texinfo.Texture->Palette())
				texinfo.Palette = (TextureColor*)texinfo.Texture->Palette()->Colors.data();

			vec3 depthOffset = -frame->ViewRotation.XAxis;

			int count = (int)leveldecal.Positions.size();
			GouraudVertex* points = engine->render->GetTempGouraudVertexBuffer(count);
			for (int i = 0; i < count; i++)
			{
				points[i] = GouraudVertex{};
				points[i].Fog = vec4(0.0f);
				points[i].Light = vec3(1.0f);
				points[i].Point = leveldecal.Positions[i] + depthOffset;
				points[i].UV = leveldecal.UVs[i];
			}

			frame->Device->DrawGouraudPolygon(&frame->Frame, texinfo, points, count, flags);
		}
	}
}

