#pragma once

// Read-only geometry diagnostics for encounters; no map actors or saves change.
inline void RunRuneEnemyGeometryProbe(Engine& game)
{
    for (const char* name : {"DwarfMech", "SnowBeast", "StoneGuard"})
    {
        auto* actor = UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject(
            std::string("GeometryProbe") + name, game.packages->GetPackage("RuneI")->GetClass(name), ObjectFlags::Transient));
        auto* model = UObject::Cast<USkelModel>(GetRuneDisplayMesh(actor));
        std::cout << "RUNE_ENEMY_MODEL class=" << name << " model=" << model->Name.ToString()
            << " root=" << model->RootJoint << " scale=" << actor->DrawScale()
            << " rotation=" << model->RotationOffset.Pitch << ',' << model->RotationOffset.Yaw << ',' << model->RotationOffset.Roll
            << " origin=" << model->PositionOffset.x << ',' << model->PositionOffset.y << ',' << model->PositionOffset.z << '\n';
        // The shipped mech has a large nonzero offset. Verify that this offset
        // is added in world coordinates even when the actor rotates or scales.
        const vec3 offset = model->PositionOffset;
        const Rotator oldRotation = actor->Rotation();
        const float oldScale = actor->DrawScale();
        for (const Rotator rotation : {Rotator(0, 0, 0), Rotator(0, 16384, 0), Rotator(4096, 8192, 2048)})
            for (float scale : {0.5f, 2.0f})
            {
                actor->Rotation() = rotation; actor->DrawScale() = scale;
                const vec3 actual = (GetRuneMeshToWorld(actor) * vec4(0, 0, 0, 1)).xyz();
                if (length(actual - actor->Location() - actor->PrePivot() - offset) > 0.001f)
                    throw std::runtime_error("Rune model offset rotated, scaled or negated");
            }
        actor->Rotation() = oldRotation; actor->DrawScale() = oldScale;
        for (size_t sequence = 0; sequence < model->Sequences.size(); ++sequence)
        {
            const auto& info = model->Sequences[sequence].Info;
            if (info.Name != "spin_cycle" && info.Name != "Idle" && info.Name != "Base") continue;
            auto bones = model->JointMatrices(model->DecodeFrame(sequence, 0));
            auto verts = model->SkinFrame(actor->GetByte("SkelMesh"), sequence, 0);
            vec3 lo(1e10f), hi(-1e10f);
            for (auto p : verts)
            {
                p = (GetRuneMeshToWorld(actor) * vec4(p, 1)).xyz();
                for (int axis = 0; axis < 3; ++axis) { lo[axis] = std::min(lo[axis], p[axis]); hi[axis] = std::max(hi[axis], p[axis]); }
            }
            auto& bounds = model->Frames[info.StartFrame].Bounds;
            std::cout << "RUNE_ENEMY_MODEL_POSE sequence=" << info.Name.ToString()
                << " rendered_z=" << lo.z << ',' << hi.z << " stored_z=" << bounds.min.z << ',' << bounds.max.z << '\n';
            for (size_t j = 0; j < bones.size(); ++j)
                if (model->Joints[j].Name == "rwrist" || model->Joints[j].Name == "lwrist" || model->Joints[j].Name == "torso")
                {
                    auto p = (GetRuneMeshToWorld(actor) * bones[j] * vec4(0, 0, 0, 1)).xyz();
                    std::cout << "RUNE_ENEMY_MODEL_JOINT name=" << model->Joints[j].Name.ToString() << " xyz=" << p.x << ',' << p.y << ',' << p.z << '\n';
                }
        }
    }
    std::cout << "RUNE_SKELETON_PLACEMENT_OK models=3 rotations=3 scales=2\n";
}
