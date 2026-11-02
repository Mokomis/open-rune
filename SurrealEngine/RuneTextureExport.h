#pragma once

#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Utils/JsonValue.h"
#include "Utils/File.h"
#include "miniz/miniz.h"
#include <filesystem>
#include <set>

// Offline extraction only. Never rewrites packages, initializes rendering or runs actors.
inline void ExportRuneTextures(Engine& game, const std::string& packageName, const std::string& destination)
{
    namespace fs = std::filesystem;
    if (packageName.empty() || destination.empty()) throw std::runtime_error("Texture export requires --texture-package and --texture-output");
    fs::path out = fs::absolute(destination);
    if (fs::exists(out)) throw std::runtime_error("Texture output must be a new directory");
    Package* pkg = fs::path(packageName).extension() == ".run"
        ? game.packages->LoadMap(packageName) : game.packages->GetPackage(packageName);
    auto manifest = JsonValue::object();
    manifest.prop("package") = JsonValue::string(pkg->GetPackageName().ToString());
    manifest.prop("source") = JsonValue::string(pkg->GetPackageFilePath());
    manifest.prop("textures") = JsonValue::array();
    fs::create_directories(out);
    std::set<std::string> paths;
    for (int ref = 1; ref <= pkg->GetExportCount(); ++ref)
    {
        auto entry = pkg->GetExportEntry(ref);
        int cls = entry->ObjClass;
        if (!cls) continue;
        auto className = pkg->GetName(cls < 0 ? pkg->GetImportEntry(cls)->ObjName : pkg->GetExportEntry(cls)->ObjName).ToString();
        // All texture classes in the supported Rune packages. Explicitly inventory procedural classes.
        if (className != "Texture" && className != "FireTexture" && className != "WaterTexture" &&
            className != "WetTexture" && className != "WaveTexture" && className != "IceTexture" &&
            className != "ScriptedTexture" && className != "FractalTexture") continue;
        auto tex = UObject::Cast<UTexture>(pkg->GetUObject(ref));
        std::string identity;
        std::set<int> visited;
        for (int cur = ref; cur > 0; cur = pkg->GetExportEntry(cur)->ObjOuter)
        {
            if (!visited.insert(cur).second) throw std::runtime_error("Cyclic object group");
            auto part = pkg->GetName(pkg->GetExportEntry(cur)->ObjName).ToString();
            if (part.empty() || part == "." || part == ".." || part.find_first_of("/\\") != std::string::npos)
                throw std::runtime_error("Unsafe object name");
            identity = part + (identity.empty() ? "" : "." + identity);
        }
        if (!paths.insert(identity).second) throw std::runtime_error("Duplicate texture identity");
        auto row = JsonValue::object();
        row.prop("id") = JsonValue::string(pkg->GetPackageName().ToString() + "." + identity);
        row.prop("class") = JsonValue::string(className);
        row.prop("flags") = JsonValue::number(tex->PolyFlags());
        row.prop("animated") = JsonValue::boolean(tex->AnimNext() != nullptr);
        row.prop("masked") = JsonValue::boolean(tex->bMasked());
        row.prop("format") = JsonValue::number(static_cast<int>(tex->UsedFormat));
        row.prop("status") = JsonValue::string("procedural-original");
        if (className == "Texture" && !tex->UncompressedMipmaps.empty() && tex->GetByte("Format") == 0)
        {
            const auto& mip = tex->UncompressedMipmaps.front();
            auto palette = tex->Palette();
            if (mip.Width <= 0 || mip.Height <= 0 || mip.Width > 8192 || mip.Height > 8192 ||
                mip.Data.size() != static_cast<size_t>(mip.Width) * mip.Height || !palette || palette->Colors.size() != 256)
                throw std::runtime_error("Invalid indexed texture: " + identity);
            // Preserve palette-zero coverage separately: a surface can request masking even
            // when the texture's own bMasked property is false. RGB is always exported opaque.
            Array<uint32_t> rgba(mip.Data.size()), mask(mip.Data.size());
            for (size_t i = 0; i < mip.Data.size(); ++i)
            {
                rgba[i] = palette->Colors[mip.Data[i]] | 0xff000000u;
                mask[i] = mip.Data[i] == 0 ? 0xff000000u : 0xffffffffu;
            }
            auto writePng = [&](const std::string& name, const Array<uint32_t>& pixels)
            {
                size_t size = 0;
                void* png = tdefl_write_image_to_png_file_in_memory_ex(pixels.data(), mip.Width, mip.Height, 4, &size, 6, 0);
                if (!png) throw std::runtime_error("PNG encoding failed");
                try { File::write_all_bytes((out / name).string(), png, size); }
                catch (...) { mz_free(png); throw; }
                mz_free(png);
            };
            writePng(identity + ".png", rgba);
            writePng(identity + ".mask.png", mask);
            row.prop("file") = JsonValue::string(identity + ".png");
            row.prop("mask_file") = JsonValue::string(identity + ".mask.png");
            row.prop("width") = JsonValue::number(mip.Width);
            row.prop("height") = JsonValue::number(mip.Height);
            row.prop("status") = JsonValue::string("exported");
        }
        else if (className == "Texture") row.prop("status") = JsonValue::string("unsupported-original");
        manifest.prop("textures").items().push_back(std::move(row));
    }
    File::write_all_text((out / "manifest.json").string(), manifest.to_json(true));
}
